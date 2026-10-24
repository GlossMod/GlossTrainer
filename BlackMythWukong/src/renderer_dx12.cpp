#include "renderer.hpp"
#include "runtime.hpp"
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <array>
#include <vector>

namespace gloss::graphics
{
    using Microsoft::WRL::ComPtr;
    namespace
    {
        constexpr UINT SrvCount = 128;
        class DX12Renderer final : public Renderer
        {
            struct Frame
            {
                ComPtr<ID3D12Resource> buffer;
                ComPtr<ID3D12CommandAllocator> allocator;
                D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
                UINT64 fence{};
            };

        public:
            bool Initialize(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
            {
                if (!queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
                    FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain_))) ||
                    FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device_))))
                    return false;
                ComPtr<ID3D12Device> queueDevice;
                ComPtr<IUnknown> deviceIdentity, queueIdentity;
                if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
                    FAILED(device_.As(&deviceIdentity)) || FAILED(queueDevice.As(&queueIdentity)) ||
                    deviceIdentity.Get() != queueIdentity.Get())
                    return false;
                queue_ = queue;
                DXGI_SWAP_CHAIN_DESC desc{};
                if (FAILED(swapChain->GetDesc(&desc)) || desc.BufferCount == 0 || desc.BufferCount > 16)
                    return false;
                frames_.resize(desc.BufferCount);
                D3D12_DESCRIPTOR_HEAP_DESC heap{};
                heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heap.NumDescriptors = desc.BufferCount;
                if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_))))
                    return false;
                heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heap.NumDescriptors = SrvCount;
                heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap_))))
                    return false;
                auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
                const UINT rtvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                srvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                for (UINT i = 0; i < desc.BufferCount; ++i)
                {
                    auto &frame = frames_[i];
                    if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&frame.buffer))) ||
                        FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                               IID_PPV_ARGS(&frame.allocator))))
                        return false;
                    frame.rtv = rtv;
                    device_->CreateRenderTargetView(frame.buffer.Get(), nullptr, rtv);
                    rtv.ptr += rtvStride;
                }
                if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
                    FAILED(commands_->Close()) ||
                    FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_))))
                    return false;
                event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!event_)
                    return false;
                ImGui_ImplDX12_InitInfo info;
                info.Device = device_.Get();
                info.CommandQueue = queue_.Get();
                info.NumFramesInFlight = static_cast<int>(desc.BufferCount);
                info.RTVFormat = desc.BufferDesc.Format;
                info.SrvDescriptorHeap = srvHeap_.Get();
                info.UserData = this;
                info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *init,
                                               D3D12_CPU_DESCRIPTOR_HANDLE *cpu, D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
                {
                    auto *self = static_cast<DX12Renderer *>(init->UserData);
                    *cpu = {};
                    *gpu = {};
                    for (UINT i = 0; i < SrvCount; ++i)
                    {
                        if (self->srvUsed_[i])
                            continue;
                        self->srvUsed_[i] = true;
                        *cpu = self->srvHeap_->GetCPUDescriptorHandleForHeapStart();
                        *gpu = self->srvHeap_->GetGPUDescriptorHandleForHeapStart();
                        cpu->ptr += SIZE_T(i) * self->srvStride_;
                        gpu->ptr += UINT64(i) * self->srvStride_;
                        return;
                    }
                    Log("DX12 SRV descriptor heap exhausted");
                };
                info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *init,
                                              D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
                {
                    auto *self = static_cast<DX12Renderer *>(init->UserData);
                    const SIZE_T start = self->srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
                    if (cpu.ptr < start || !self->srvStride_)
                        return;
                    const SIZE_T slot = (cpu.ptr - start) / self->srvStride_;
                    if (slot < SrvCount)
                        self->srvUsed_[slot] = false;
                };
                initialized_ = ImGui_ImplDX12_Init(&info);
                if (initialized_ && !ImGui_ImplDX12_CreateDeviceObjects())
                {
                    Log("DX12 backend device-object initialization failed");
                    return false;
                }
                if (initialized_)
                    Log("DX12 renderer initialized with verified swap-chain command queue");
                return initialized_;
            }
            ~DX12Renderer() override
            {
                // The owner must successfully call BeforeResize before destruction.
                // Initialization failures have not submitted any GPU work yet.
                if (initialized_)
                    ImGui_ImplDX12_Shutdown();
                if (event_)
                    CloseHandle(event_);
            }
            bool Draw() override
            {
                if (!initialized_ || frames_.empty() || DeviceLost())
                    return false;
                if (releaseBlocked_)
                {
                    // A failed resize may be retried by the host, or normal rendering
                    // may resume once the pending idle marker has completed.
                    if (!idleFence_ || fence_->GetCompletedValue() < idleFence_)
                        return false;
                    idleFence_ = 0;
                    releaseBlocked_ = false;
                    idleWaitLogged_ = false;
                }
                const UINT index = swapChain_->GetCurrentBackBufferIndex();
                if (index >= frames_.size())
                {
                    failed_ = true;
                    Log("DX12 back-buffer index is invalid; disabling overlay");
                    return false;
                }
                Frame &frame = frames_[index];
                // Never stall the game's Present while an earlier overlay frame uses
                // this allocator. The next available frame can draw the UI instead.
                if (frame.fence && fence_->GetCompletedValue() < frame.fence)
                {
                    if (!noFrameSince_)
                        noFrameSince_ = GetTickCount64();
                    if (GetTickCount64() - noFrameSince_ >= 2000)
                        overlay::SetRendererReady(false);
                    return false;
                }
                noFrameSince_ = 0;
                if (FAILED(frame.allocator->Reset()) ||
                    FAILED(commands_->Reset(frame.allocator.Get(), nullptr)))
                {
                    failed_ = true;
                    Log("DX12 command allocator/list Reset failed; disabling overlay");
                    return false;
                }
                submitted_ = true; // The backend may submit texture-upload work too.
                ImGui_ImplDX12_NewFrame();
                overlay::DrawFrame("DirectX 12");
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = frame.buffer.Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                commands_->ResourceBarrier(1, &barrier);
                commands_->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
                ID3D12DescriptorHeap *heaps[] = {srvHeap_.Get()};
                commands_->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands_.Get());
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                commands_->ResourceBarrier(1, &barrier);
                if (FAILED(commands_->Close()))
                {
                    failed_ = true;
                    Log("DX12 command list Close failed; disabling overlay");
                    return false;
                }
                ID3D12CommandList *lists[] = {commands_.Get()};
                queue_->ExecuteCommandLists(1, lists);
                frame.fence = ++nextFence_;
                if (FAILED(queue_->Signal(fence_.Get(), frame.fence)))
                {
                    failed_ = true;
                    Log("DX12 queue Signal failed; disabling overlay until swap-chain recreation");
                    return false;
                }
                return true;
            }
            bool BeforeResize() override
            {
                if (!WaitIdle())
                    return false;
                for (auto &frame : frames_)
                    frame.buffer.Reset();
                return true;
            }
            bool DeviceLost() const override
            {
                return failed_ || (device_ && FAILED(device_->GetDeviceRemovedReason()));
            }

        private:
            bool WaitIdle()
            {
                if (!submitted_ || !queue_ || !fence_ || !event_)
                    return true;
                // A renderer-side failure (e.g. Signal failed) alone does not prove GPU
                // work has stopped. Only actual device removal makes immediate release
                // safe without a completed fence.
                if (device_ && FAILED(device_->GetDeviceRemovedReason()))
                    return true;
                if (idleFence_)
                {
                    if (fence_->GetCompletedValue() >= idleFence_)
                    {
                        releaseBlocked_ = false;
                        return true;
                    }
                    return false; // Retry/poll without another multi-second wait.
                }
                const UINT64 value = ++nextFence_;
                if (FAILED(queue_->Signal(fence_.Get(), value)))
                {
                    releaseBlocked_ = true;
                    if (!idleWaitLogged_)
                    {
                        Log("DX12 idle Signal failed; resources retained and resize deferred");
                        idleWaitLogged_ = true;
                    }
                    return false;
                }
                idleFence_ = value;
                if (fence_->GetCompletedValue() >= value)
                    return true;
                releaseBlocked_ = true;
                // A previous timed-out marker may have signalled this auto-reset event
                // after the waiter returned. Never treat that stale event as completion
                // of the new marker; check the fence itself after waking as well.
                ResetEvent(event_);
                if (SUCCEEDED(fence_->SetEventOnCompletion(value, event_)) &&
                    WaitForSingleObject(event_, 2000) == WAIT_OBJECT_0 &&
                    fence_->GetCompletedValue() >= value)
                {
                    releaseBlocked_ = false;
                    return true;
                }
                if (device_ && FAILED(device_->GetDeviceRemovedReason()))
                    return true;
                if (!idleWaitLogged_)
                {
                    Log("DX12 GPU idle wait timed out; resources retained and resize deferred");
                    idleWaitLogged_ = true;
                }
                return false;
            }
            ComPtr<IDXGISwapChain3> swapChain_;
            ComPtr<ID3D12Device> device_;
            ComPtr<ID3D12CommandQueue> queue_;
            ComPtr<ID3D12DescriptorHeap> rtvHeap_, srvHeap_;
            ComPtr<ID3D12GraphicsCommandList> commands_;
            ComPtr<ID3D12Fence> fence_;
            std::vector<Frame> frames_;
            std::array<bool, SrvCount> srvUsed_{};
            HANDLE event_{};
            UINT srvStride_{};
            UINT64 nextFence_{}, idleFence_{};
            ULONGLONG noFrameSince_{};
            bool initialized_{}, failed_{}, submitted_{}, releaseBlocked_{}, idleWaitLogged_{};
        };
    }

    std::unique_ptr<Renderer> CreateDX12Renderer(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
    {
        auto renderer = std::make_unique<DX12Renderer>();
        if (!renderer->Initialize(swapChain, queue))
            return {};
        return renderer;
    }
}
