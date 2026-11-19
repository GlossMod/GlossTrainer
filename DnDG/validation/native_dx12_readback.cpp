#include "native_dx12_readback.hpp"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>

namespace gloss::dndg::validation {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr float ClearColor[]{28.0f / 255.0f, 44.0f / 255.0f, 36.0f / 255.0f, 1.0f};
constexpr UINT SrvCount = 64;
constexpr DWORD FenceTimeoutMs = 5000;
constexpr UINT64 RemovedFenceValue = std::numeric_limits<UINT64>::max();

void CheckHr(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return;
    char value[11]{};
    std::snprintf(value, sizeof(value), "0x%08lX", static_cast<unsigned long>(hr));
    throw std::runtime_error(std::string("Dx12Readback: ") + operation + " failed (HRESULT " + value + ")");
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

struct CloseEvent {
    void operator()(HANDLE event) const noexcept { CloseHandle(event); }
};

} // namespace

struct Dx12Readback::Impl {
    Impl(unsigned width, unsigned height) : width_(width), height_(height) {}

    ~Impl() {
        if (pending_) {
            // Render may have thrown after submission. Give live work one
            // bounded drain attempt before releasing anything it may reference.
            try {
                FinishSubmission();
            } catch (const std::exception& error) {
                if (pending_ && SUCCEEDED(device_->GetDeviceRemovedReason()) &&
                    (!signalQueued_ || fence_->GetCompletedValue() < fenceValue_)) {
                    std::fprintf(stderr, "Dx12Readback: cannot safely release in-flight work: %s\n", error.what());
                    std::terminate();
                }
            }
        }
        if (backendInitialized_) {
            auto* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context_);
            ImGui_ImplDX12_Shutdown();
            ImGui::SetCurrentContext(previous);
        }
    }

    void Initialize() {
        if (!width_ || !height_ || width_ > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height_ > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
            throw std::invalid_argument("Dx12Readback: framebuffer dimensions are out of range");
        }
        context_ = ImGui::GetCurrentContext();
        if (!context_ || ImGui::GetIO().BackendRendererUserData) {
            throw std::logic_error("Dx12Readback: a current ImGui context with no renderer backend is required");
        }

        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        CheckHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        CheckHr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice (WARP)");

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CheckHr(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        CheckHr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
            "CreateCommandAllocator");
        CheckHr(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
            IID_PPV_ARGS(&commands_)), "CreateCommandList");
        CheckHr(commands_->Close(), "Close initial command list");
        CheckHr(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
        fenceEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!fenceEvent_) CheckHr(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW for readback fence");

        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 1;
        CheckHr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_)), "CreateDescriptorHeap (RTV)");
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = SrvCount;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CheckHr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap_)), "CreateDescriptorHeap (ImGui SRV)");
        srvStride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);

        CreateFramebuffer();
        InitBackend();
    }

    void CreateFramebuffer() {
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = width_;
        texture.Height = height_;
        texture.DepthOrArraySize = 1;
        texture.MipLevels = 1;
        texture.Format = TargetFormat;
        texture.SampleDesc.Count = 1;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = TargetFormat;
        std::memcpy(clear.Color, ClearColor, sizeof(ClearColor));
        const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CheckHr(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target_)), "Create offscreen render target");
        rtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(target_.Get(), nullptr, rtv_);

        device_->GetCopyableFootprints(&texture, 0, 1, 0, &footprint_, nullptr, nullptr, &readbackSize_);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = readbackSize_;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const auto readbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
        CheckHr(device_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)), "Create framebuffer readback buffer");
    }

    void InitBackend() {
        ImGui_ImplDX12_InitInfo info{};
        info.Device = device_.Get();
        info.CommandQueue = queue_.Get();
        info.NumFramesInFlight = 1;
        info.RTVFormat = TargetFormat;
        info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        info.SrvDescriptorHeap = srvHeap_.Get();
        info.UserData = this;
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            auto& self = *static_cast<Impl*>(init->UserData);
            for (UINT slot = 0; slot < SrvCount; ++slot) {
                if (self.srvUsed_[slot]) continue;
                self.srvUsed_[slot] = true;
                *cpu = self.srvHeap_->GetCPUDescriptorHandleForHeapStart();
                *gpu = self.srvHeap_->GetGPUDescriptorHandleForHeapStart();
                const auto offset = SIZE_T{slot} * self.srvStride_;
                cpu->ptr += offset;
                gpu->ptr += offset;
                return;
            }
            throw std::runtime_error("Dx12Readback: ImGui SRV heap is exhausted");
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE) {
            auto& self = *static_cast<Impl*>(init->UserData);
            const auto base = self.srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
            if (cpu.ptr < base) return;
            const auto offset = cpu.ptr - base;
            if (offset % self.srvStride_ == 0 && offset / self.srvStride_ < SrvCount) {
                self.srvUsed_[offset / self.srvStride_] = false;
            }
        };
        backendInitialized_ = ImGui_ImplDX12_Init(&info);
        if (!backendInitialized_ || !ImGui_ImplDX12_CreateDeviceObjects()) {
            throw std::runtime_error("Dx12Readback: ImGui DX12 backend initialization failed");
        }
    }

    void CheckTextureCapacity(const ImDrawData* drawData) const {
        if (!drawData || !drawData->Textures) return;
        unsigned available{};
        for (bool used : srvUsed_) if (!used) ++available;
        for (const auto* texture : *drawData->Textures) {
            if (texture->Status != ImTextureStatus_WantCreate) continue;
            if (!available) throw std::runtime_error("Dx12Readback: not enough ImGui SRV descriptors for this frame");
            --available;
        }
    }

    void FinishSubmission() {
        if (!signalQueued_) {
            CheckHr(queue_->Signal(fence_.Get(), fenceValue_), "Signal framebuffer readback fence");
            signalQueued_ = true;
        }
        if (!eventArmed_) {
            CheckHr(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_.get()), "Arm framebuffer readback fence event");
            eventArmed_ = true;
        }
        const DWORD wait = WaitForSingleObject(fenceEvent_.get(), FenceTimeoutMs);
        if (wait == WAIT_FAILED) CheckHr(HRESULT_FROM_WIN32(GetLastError()), "Wait for framebuffer readback fence");
        if (wait == WAIT_TIMEOUT) throw std::runtime_error("Dx12Readback: framebuffer readback fence timed out (5 s)");
        if (wait != WAIT_OBJECT_0) throw std::runtime_error("Dx12Readback: unexpected framebuffer fence wait result");
        eventArmed_ = false;
        const UINT64 completed = fence_->GetCompletedValue();
        if (completed == RemovedFenceValue) {
            pending_ = false;
            const HRESULT reason = device_->GetDeviceRemovedReason();
            CheckHr(FAILED(reason) ? reason : DXGI_ERROR_DEVICE_REMOVED, "WARP device removed during framebuffer readback");
        }
        if (completed < fenceValue_) throw std::runtime_error("Dx12Readback: framebuffer event signaled before its fence");
        pending_ = false;
        signalQueued_ = false;
    }

    std::vector<std::uint8_t> Render(ImDrawData* drawData) {
        if (ImGui::GetCurrentContext() != context_) {
            throw std::logic_error("Dx12Readback: Render requires the original ImGui context to be current");
        }
        if (unusable_) throw std::logic_error("Dx12Readback: cannot reuse the helper after a rendering failure");
        CheckTextureCapacity(drawData);
        const auto rowBytes = static_cast<std::size_t>(width_) * 4;
        std::vector<std::uint8_t> pixels(rowBytes * height_);
        unusable_ = true;
        CheckHr(allocator_->Reset(), "Reset framebuffer command allocator");
        CheckHr(commands_->Reset(allocator_.Get(), nullptr), "Reset framebuffer command list");
        commands_->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
        commands_->ClearRenderTargetView(rtv_, ClearColor, 0, nullptr);
        if (drawData) {
            ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};
            commands_->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(drawData, commands_.Get());
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {target_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands_->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = target_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint_;
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        // Restore the target for a later capture on the same helper.
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands_->ResourceBarrier(1, &barrier);
        CheckHr(commands_->Close(), "Close framebuffer command list");

        ID3D12CommandList* lists[]{commands_.Get()};
        ++fenceValue_;
        pending_ = true;
        queue_->ExecuteCommandLists(1, lists);
        FinishSubmission();

        void* mapped{};
        const D3D12_RANGE readRange{static_cast<SIZE_T>(footprint_.Offset), static_cast<SIZE_T>(readbackSize_)};
        CheckHr(readback_->Map(0, &readRange, &mapped), "Map framebuffer readback buffer");
        const auto* firstRow = static_cast<const std::uint8_t*>(mapped) + footprint_.Offset;
        for (std::size_t y = 0; y < height_; ++y) {
            std::memcpy(pixels.data() + y * rowBytes, firstRow + y * footprint_.Footprint.RowPitch, rowBytes);
        }
        const D3D12_RANGE noWrites{0, 0};
        readback_->Unmap(0, &noWrites);
        unusable_ = false;
        return pixels;
    }

    const unsigned width_;
    const unsigned height_;
    ImGuiContext* context_{};
    bool backendInitialized_{};
    bool unusable_{};
    bool pending_{};
    bool signalQueued_{};
    bool eventArmed_{};
    UINT64 fenceValue_{};
    std::unique_ptr<void, CloseEvent> fenceEvent_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12Resource> target_;
    ComPtr<ID3D12Resource> readback_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    UINT64 readbackSize_{};
    UINT srvStride_{};
    std::array<bool, SrvCount> srvUsed_{};
};

Dx12Readback::Dx12Readback(unsigned width, unsigned height) : impl_(std::make_unique<Impl>(width, height)) {
    impl_->Initialize();
}

Dx12Readback::~Dx12Readback() = default;

std::vector<std::uint8_t> Dx12Readback::Render(ImDrawData* drawData) {
    return impl_->Render(drawData);
}

unsigned Dx12Readback::Width() const noexcept { return impl_->width_; }
unsigned Dx12Readback::Height() const noexcept { return impl_->height_; }

} // namespace gloss::dndg::validation
