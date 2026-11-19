#include "renderer_dx12.hpp"
#include "input_win32.hpp"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <array>
#include <atomic>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

// The ImGui Win32 header intentionally does not expose this declaration.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace gloss::dndg {
namespace {

using Microsoft::WRL::ComPtr;

constexpr GUID BindingKey{0x78ed0874, 0xb2ea, 0x48fa, {0xa7, 0x49, 0x7d, 0x08, 0xd2, 0x02, 0x93, 0x13}};
constexpr UINT MaxBuffers = 16;
constexpr UINT SrvCount = 64;
constexpr DWORD ResizeWaitMs = 250;
constexpr UINT64 RemovedFence = (std::numeric_limits<UINT64>::max)();

void Trace(const char* text) noexcept {
    OutputDebugStringA(text);
}

bool IsDirectQueue(IUnknown* object, ComPtr<ID3D12CommandQueue>& queue) {
    if (!object || FAILED(object->QueryInterface(IID_PPV_ARGS(&queue))) ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        queue.Reset();
        return false;
    }
    return true;
}

bool SameIdentity(IUnknown* first, IUnknown* second) {
    ComPtr<IUnknown> a, b;
    return first && second && SUCCEEDED(first->QueryInterface(IID_PPV_ARGS(&a))) &&
        SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&b))) && a.Get() == b.Get();
}

// A binding lives on the actual DXGI object. No historical address map or
// owning swap-chain reference that could keep a flip-model HWND occupied.
struct SwapBinding final : IUnknown {
    explicit SwapBinding(HWND hwnd, ID3D12CommandQueue* observedQueue) : window(hwnd), queue(observedQueue) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return refs.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!remaining) delete this;
        return remaining;
    }

    const HWND window;
    ComPtr<ID3D12CommandQueue> queue;
    unsigned resizing{};
    bool rendererFailed{};

private:
    std::atomic<ULONG> refs{1};
};

ComPtr<SwapBinding> GetBinding(IDXGISwapChain* chain) {
    IUnknown* data{};
    UINT size = sizeof(data);
    ComPtr<SwapBinding> binding;
    // GetPrivateData returns an AddRef'd interface for SetPrivateDataInterface.
    if (SUCCEEDED(chain->GetPrivateData(BindingKey, &size, &data)) && size == sizeof(data) && data)
        binding.Attach(static_cast<SwapBinding*>(data));
    return binding;
}

class CurrentContext {
public:
    explicit CurrentContext(ImGuiContext* context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ~CurrentContext() { ImGui::SetCurrentContext(previous_); }

private:
    ImGuiContext* previous_;
};

class Dx12Renderer {
public:
    explicit Dx12Renderer(SwapBinding* binding) : binding_(binding), queue_(binding->queue) {}
    ~Dx12Renderer() {
        if (initialized_) ImGui_ImplDX12_Shutdown();
        if (event_) CloseHandle(event_);
    }
    SwapBinding* Binding() const { return binding_.Get(); }

    bool Initialize(IDXGISwapChain3* chain) {
        ComPtr<ID3D12Device> queueDevice;
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(chain->GetDevice(IID_PPV_ARGS(&device_))) ||
            FAILED(queue_->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
            !SameIdentity(device_.Get(), queueDevice.Get()) || device_->GetNodeCount() != 1 ||
            FAILED(chain->GetDesc(&desc)) || desc.OutputWindow != binding_->window ||
            !desc.BufferCount || desc.BufferCount > MaxBuffers || desc.SampleDesc.Count != 1 ||
            !(desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT))
            return false;
        bufferCount_ = desc.BufferCount;

        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = bufferCount_;
        if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_)))) return false;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = SrvCount;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap_)))) return false;

        auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        const UINT rtvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        srvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 0; i < bufferCount_; ++i) {
            auto& buffer = buffers_[i];
            if (FAILED(chain->GetBuffer(i, IID_PPV_ARGS(&buffer.resource)))) return false;
            buffer.rtv = rtv;
            device_->CreateRenderTargetView(buffer.resource.Get(), nullptr, rtv);
            rtv.ptr += rtvStride;
        }
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))) ||
            FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                IID_PPV_ARGS(&commands_))) || FAILED(commands_->Close()) ||
            FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_))))
            return false;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) return false;

        ImGui_ImplDX12_InitInfo info{};
        info.Device = device_.Get();
        info.CommandQueue = queue_.Get();
        info.NumFramesInFlight = static_cast<int>(bufferCount_);
        // Match the effective default RTV, not an independently chosen format.
        info.RTVFormat = buffers_[0].resource->GetDesc().Format;
        info.SrvDescriptorHeap = srvHeap_.Get();
        info.UserData = this;
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            auto& self = *static_cast<Dx12Renderer*>(init->UserData);
            *cpu = {};
            *gpu = {};
            for (UINT i = 0; i < SrvCount; ++i) {
                if (self.srvUsed_[i]) continue;
                self.srvUsed_[i] = true;
                *cpu = self.srvHeap_->GetCPUDescriptorHandleForHeapStart();
                *gpu = self.srvHeap_->GetGPUDescriptorHandleForHeapStart();
                const SIZE_T offset = SIZE_T{i} * self.srvStride_;
                cpu->ptr += offset;
                gpu->ptr += offset;
                return;
            }
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE) {
            auto& self = *static_cast<Dx12Renderer*>(init->UserData);
            const auto base = self.srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
            if (cpu.ptr < base || !self.srvStride_) return;
            const auto offset = cpu.ptr - base;
            if (!(offset % self.srvStride_) && offset / self.srvStride_ < SrvCount)
                self.srvUsed_[offset / self.srvStride_] = false;
        };
        initialized_ = ImGui_ImplDX12_Init(&info);
        return initialized_ && ImGui_ImplDX12_CreateDeviceObjects();
    }

    bool Ready() const {
        if (submittedWithoutFence_) return false;
        const UINT64 completed = fence_->GetCompletedValue();
        return completed != RemovedFence && completed >= lastFence_;
    }

    bool Render(IDXGISwapChain3* chain, ImDrawData* data) {
        if (!data || data->TotalVtxCount <= 0 || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f)
            return true;
        if (data->Textures) {
            // The stock backend assumes descriptor allocation succeeds. Don't
            // enter it if the queued texture creates cannot fit in our heap.
            unsigned available{};
            for (const bool used : srvUsed_) if (!used) ++available;
            for (const ImTextureData* texture : *data->Textures) {
                if (texture->Status == ImTextureStatus_WantCreate) {
                    if (!available) return false;
                    --available;
                }
            }
        }
        const UINT index = chain->GetCurrentBackBufferIndex();
        if (index >= bufferCount_ || FAILED(allocator_->Reset()) ||
            FAILED(commands_->Reset(allocator_.Get(), nullptr))) return false;
        const auto& buffer = buffers_[index];
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer.resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands_->ResourceBarrier(1, &barrier);
        commands_->OMSetRenderTargets(1, &buffer.rtv, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[]{srvHeap_.Get()};
        commands_->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(data, commands_.Get());
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commands_->ResourceBarrier(1, &barrier);
        if (FAILED(commands_->Close())) return false;
        ID3D12CommandList* lists[]{commands_.Get()};
        submittedWithoutFence_ = true;
        queue_->ExecuteCommandLists(1, lists);
        return Signal();
    }

    bool Retire(DWORD timeout) {
        if (Removed()) return true;
        if (submittedWithoutFence_ && !Signal()) return Removed();
        if (Ready() || Removed()) return true;
        if (!timeout) return false;
        if (eventFence_ != lastFence_) {
            ResetEvent(event_);
            if (FAILED(fence_->SetEventOnCompletion(lastFence_, event_))) return Removed();
            eventFence_ = lastFence_;
        }
        const ULONGLONG deadline = GetTickCount64() + timeout;
        while (!Ready() && !Removed()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline || WaitForSingleObject(event_, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
                break;
        }
        return Ready() || Removed();
    }

private:
    bool Signal() {
        if (lastFence_ == RemovedFence - 1 || FAILED(queue_->Signal(fence_.Get(), lastFence_ + 1))) return false;
        ++lastFence_;
        submittedWithoutFence_ = false;
        return true;
    }
    bool Removed() const {
        return fence_->GetCompletedValue() == RemovedFence || FAILED(device_->GetDeviceRemovedReason());
    }

    struct Buffer {
        ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    };
    ComPtr<SwapBinding> binding_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_, srvHeap_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    std::array<Buffer, MaxBuffers> buffers_;
    std::array<bool, SrvCount> srvUsed_{};
    HANDLE event_{};
    UINT bufferCount_{}, srvStride_{};
    UINT64 lastFence_{}, eventFence_{};
    bool initialized_{}, submittedWithoutFence_{};
};

struct NativeState {
    std::recursive_mutex mutex;
    std::unique_ptr<Dx12Renderer> renderer;
    ImGuiContext* context{};
    HWND window{};
    bool uiFailed{}, drawing{}, inputRequested{};
};

NativeState& State() {
    // Do not shut down the GPU or call Win32 backend code under loader lock.
    static auto* state = new NativeState;
    return *state;
}

bool PrepareWindow(NativeState& state, HWND window) {
    if (state.uiFailed) return false;
    if (!state.context) {
        const CurrentContext restore(ImGui::GetCurrentContext());
        state.context = ImGui::CreateContext();
        if (!state.context) return false;
        ImGui::SetCurrentContext(state.context);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        try {
            ConfigureNativeImGui();
        } catch (...) {
            state.uiFailed = true;
            Trace("[DnDG] ConfigureNativeImGui failed\n");
            return false;
        }
    }
    if (state.window == window) return true;
    const CurrentContext context(state.context);
    if (state.window) {
        ImGui_ImplWin32_Shutdown();
        state.window = nullptr;
    }
    if (!ImGui_ImplWin32_Init(window)) return false;
    state.window = window;
    state.inputRequested = false;
    return true;
}

LRESULT WINAPI ForwardWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT result{};
    TryForwardNativeWin32Message(hwnd, msg, wparam, lparam, result);
    return result;
}


bool RetireRenderer(NativeState& state, DWORD timeout) {
    if (!state.renderer) return true;
    if (!state.renderer->Retire(timeout)) return false;
    const CurrentContext context(state.context);
    state.renderer.reset();
    return true;
}

void DrawSidecar(IDXGISwapChain* chain) {
    auto binding = GetBinding(chain);
    if (!binding) return;
    auto& state = State();
    const std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock || state.drawing || binding->resizing || binding->rendererFailed || !binding->queue || state.uiFailed) return;
    const HWND window = binding->window;
    if (!IsWindowVisible(window) || IsIconic(window) || GetWindow(window, GW_OWNER)) return;
    if (state.renderer && state.renderer->Binding() != binding.Get()) {
        // Don't switch to a secondary chain for a different background HWND.
        if (state.window != window && IsWindowVisible(state.window) && GetForegroundWindow() != window) return;
        if (!RetireRenderer(state, 0)) return;
    }
    struct DrawScope {
        explicit DrawScope(bool& flag) : drawing(flag) { drawing = true; }
        ~DrawScope() { drawing = false; }
        bool& drawing;
    } drawScope(state.drawing);
    ComPtr<IDXGISwapChain3> chain3;
    if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) return;
    if (!state.renderer) {
        if (!PrepareWindow(state, window)) return;
        const CurrentContext context(state.context);
        auto renderer = std::make_unique<Dx12Renderer>(binding.Get());
        if (!renderer->Initialize(chain3.Get())) {
            binding->rendererFailed = true;
            Trace("[DnDG] DX12 backend initialization failed; swap chain left untouched\n");
            return;
        }
        state.renderer = std::move(renderer);
        if (!state.inputRequested) {
            state.inputRequested = true;
            if (!HookNativeInput(window)) Trace("[DnDG] Win32 input hook could not be scheduled\n");
        }
        Trace("[DnDG] DX12 overlay initialized for observed HWND swap chain\n");
    }
    // One overlay submission in flight, independent of swap-chain indexing.
    // This protects allocator reuse and ImGui's dynamic textures without a
    // fence wait every frame. Game Present proceeds even if the overlay skips.
    if (!state.renderer->Ready()) return;
    const CurrentContext context(state.context);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    try {
        DrawNativeOverlay(window);
    } catch (...) {
        ImGui::EndFrame();
        state.uiFailed = true;
        Trace("[DnDG] DrawNativeOverlay failed; overlay disabled\n");
        return;
    }
    ImGui::Render();
    if (!state.renderer->Render(chain3.Get(), ImGui::GetDrawData())) {
        binding->rendererFailed = true;
        Trace("[DnDG] DX12 overlay submission failed; disabled until swap-chain resize or recreation\n");
    }
}

using FactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using Factory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateSwapFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
    const UINT*, IUnknown* const*);

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain*, UINT, UINT);
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
HRESULT STDMETHODCALLTYPE HookCreateSwap(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
HRESULT STDMETHODCALLTYPE HookCreateHwnd(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
HRESULT WINAPI HookFactory(REFIID, void**);
HRESULT WINAPI HookFactory1(REFIID, void**);
HRESULT WINAPI HookFactory2(UINT, REFIID, void**);

// COM vtable slots from the Windows SDK DXGI interface declarations.
constexpr size_t CreateSwapSlot = 10, CreateHwndSlot = 15;
constexpr size_t PresentSlot = 8, ResizeSlot = 13, Present1Slot = 22, Resize1Slot = 39;

void* Method(void* object, size_t slot) {
    return (*reinterpret_cast<void***>(object))[slot];
}

struct HookSpec { void* target; void* detour; };
struct HookEntry : HookSpec { void* original; bool enabled; };
std::mutex hookMutex;
std::array<HookEntry, 32> hooks{};
HMODULE dxgi{};

bool InstallGroup(std::initializer_list<HookSpec> specs) {
    const std::lock_guard lock(hookMutex);
    std::array<HookEntry*, 32> pending{};
    size_t pendingCount{};
    bool complete = true;
    for (const auto& spec : specs) {
        HookEntry* found{};
        for (auto& entry : hooks) {
            if (!entry.target || entry.target == spec.target) {
                found = &entry;
                break;
            }
        }
        if (!spec.target || !found) {
            complete = false;
            continue;
        }
        if (!found->target) {
            void* original{};
            if (MH_CreateHook(spec.target, spec.detour, &original) != MH_OK) {
                complete = false;
                continue;
            }
            *found = {{spec.target, spec.detour}, original, false};
        }
        if (found->detour != spec.detour) complete = false;
        else if (!found->enabled) {
            if (MH_QueueEnableHook(found->target) == MH_OK) pending[pendingCount++] = found;
            else complete = false;
        }
    }
    if (pendingCount) {
        if (MH_ApplyQueued() != MH_OK) return false;
        for (size_t i = 0; i < pendingCount; ++i) pending[i]->enabled = true;
    }
    return complete;
}

template<class Fn>
Fn Original(void* target) {
    const std::lock_guard lock(hookMutex);
    for (const auto& entry : hooks) {
        if (entry.target == target) return reinterpret_cast<Fn>(entry.original);
        if (!entry.target) break;
    }
    return nullptr;
}

void ObserveChain(IDXGISwapChain* chain, IUnknown* input, HWND hwnd) {
    if (!chain || !hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    DWORD pid{};
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> chain3;
    if (!IsDirectQueue(input, queue) || FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) return;
    if (!InstallGroup({
            {Method(chain, PresentSlot), reinterpret_cast<void*>(HookPresent)},
            {Method(chain, ResizeSlot), reinterpret_cast<void*>(HookResize)},
            {Method(static_cast<IDXGISwapChain1*>(chain3.Get()), Present1Slot), reinterpret_cast<void*>(HookPresent1)},
            {Method(chain3.Get(), Resize1Slot), reinterpret_cast<void*>(HookResize1)},
        })) {
        Trace("[DnDG] DXGI swap-chain hooks incomplete; no queue binding installed\n");
        return;
    }
    if (GetBinding(chain)) return;
    ComPtr<SwapBinding> binding;
    binding.Attach(new (std::nothrow) SwapBinding(hwnd, queue.Get()));
    if (!binding || FAILED(chain->SetPrivateDataInterface(BindingKey, binding.Get()))) return;
    Trace("[DnDG] Observed D3D12 direct queue on DXGI HWND swap-chain creation\n");
}

void ObserveFactory(IUnknown* object) {
    if (!object) return;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(object->QueryInterface(IID_PPV_ARGS(&factory)))) return;
    if (!InstallGroup({
            {Method(factory.Get(), CreateSwapSlot), reinterpret_cast<void*>(HookCreateSwap)},
            {Method(factory.Get(), CreateHwndSlot), reinterpret_cast<void*>(HookCreateHwnd)},
        }))
        Trace("[DnDG] DXGI factory method hooks incomplete\n");
}

struct PresentScope {
    PresentScope() { ++depth; }
    ~PresentScope() { --depth; }
    static thread_local unsigned depth;
};
thread_local unsigned PresentScope::depth{};

void MaybeDraw(IDXGISwapChain* chain, UINT flags) noexcept {
    if (PresentScope::depth != 1 || flags & (DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_WAIT)) return;
    try {
        DrawSidecar(chain);
    } catch (...) {
        Trace("[DnDG] Exception in DX12 sidecar Present; forwarding game Present\n");
    }
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    const auto original = Original<PresentFn>(Method(chain, PresentSlot));
    if (!original) return E_UNEXPECTED;
    const PresentScope scope;
    MaybeDraw(chain, flags);
    return original(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags,
    const DXGI_PRESENT_PARAMETERS* params) {
    const auto original = Original<Present1Fn>(Method(chain, Present1Slot));
    if (!original) return E_UNEXPECTED;
    const PresentScope scope;
    // Don't change the game's dirty rectangles or scroll contract.
    if (params && !params->DirtyRectsCount && !params->pScrollRect && !params->pScrollOffset)
        MaybeDraw(chain, flags);
    return original(chain, sync, flags, params);
}

template<class CallOriginal, class UpdateBinding>
HRESULT ResizeChain(IDXGISwapChain* chain, const CallOriginal& callOriginal, const UpdateBinding& updateBinding) {
    auto binding = GetBinding(chain);
    if (!binding) return callOriginal();
    auto& state = State();
    {
        const std::lock_guard lock(state.mutex);
        if (state.drawing || (state.renderer && state.renderer->Binding() == binding.Get() &&
            !RetireRenderer(state, ResizeWaitMs))) {
            // DXGI requires outstanding overlay back-buffer refs to be gone.
            // Retry on a later game resize rather than releasing live GPU objects.
            Trace("[DnDG] DX12 overlay still in flight; resize deferred\n");
            return DXGI_ERROR_WAS_STILL_DRAWING;
        }
        ++binding->resizing;
    }
    const HRESULT result = callOriginal();
    {
        const std::lock_guard lock(state.mutex);
        if (SUCCEEDED(result)) {
            updateBinding(*binding.Get());
            binding->rendererFailed = false;
        }
        --binding->resizing;
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags) {
    const auto original = Original<ResizeFn>(Method(chain, ResizeSlot));
    if (!original) return E_UNEXPECTED;
    return ResizeChain(chain, [&] { return original(chain, count, width, height, format, flags); }, [](SwapBinding&) {});
}

HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format,
    UINT flags, const UINT* masks, IUnknown* const* queues) {
    const auto original = Original<Resize1Fn>(Method(chain, Resize1Slot));
    if (!original) return E_UNEXPECTED;
    return ResizeChain(chain, [&] { return original(chain, count, width, height, format, flags, masks, queues); },
        [&](SwapBinding& binding) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            ComPtr<ID3D12CommandQueue> queue;
            if (queues && SUCCEEDED(chain->GetDesc1(&desc)) && desc.BufferCount && desc.BufferCount <= MaxBuffers &&
                (!count || count == desc.BufferCount) && IsDirectQueue(queues[0], queue)) {
                for (UINT i = 1; i < desc.BufferCount; ++i) {
                    if (SameIdentity(queues[i], queue.Get())) continue;
                    queue.Reset();
                    break;
                }
            }
            // No single verified queue after a multi-queue or unobserved update.
            binding.queue = std::move(queue);
        });
}

void RetireBeforeCreate(IUnknown* input, HWND window) {
    if (!window) return;
    auto& state = State();
    const std::lock_guard lock(state.mutex);
    if (!state.renderer || state.renderer->Binding()->window != window || state.drawing) return;
    ComPtr<ID3D12CommandQueue> queue;
    if (!IsDirectQueue(input, queue)) return;
    // Even without an owning swap-chain pointer, held back buffers can keep
    // the former flip-model chain associated with the HWND. Release them
    // before (not after) DXGI attempts to create its replacement.
    if (!RetireRenderer(state, ResizeWaitMs))
        Trace("[DnDG] Previous HWND overlay still in flight before swap-chain recreation\n");
}

HRESULT STDMETHODCALLTYPE HookCreateSwap(IDXGIFactory* factory, IUnknown* input, DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** output) {
    const auto original = Original<CreateSwapFn>(Method(factory, CreateSwapSlot));
    if (!original) return E_UNEXPECTED;
    if (desc) RetireBeforeCreate(input, desc->OutputWindow);
    const HRESULT result = original(factory, input, desc, output);
    if (SUCCEEDED(result) && desc && output) ObserveChain(*output, input, desc->OutputWindow);
    return result;
}

HRESULT STDMETHODCALLTYPE HookCreateHwnd(IDXGIFactory2* factory, IUnknown* input, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* restrictTo,
    IDXGISwapChain1** output) {
    const auto original = Original<CreateHwndFn>(Method(factory, CreateHwndSlot));
    if (!original) return E_UNEXPECTED;
    RetireBeforeCreate(input, window);
    const HRESULT result = original(factory, input, window, desc, fullscreen, restrictTo, output);
    if (SUCCEEDED(result) && output) ObserveChain(*output, input, window);
    return result;
}

HRESULT WINAPI HookFactory(REFIID iid, void** output) {
    const auto original = Original<FactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory")));
    if (!original) return E_UNEXPECTED;
    const HRESULT result = original(iid, output);
    if (SUCCEEDED(result) && output) ObserveFactory(static_cast<IUnknown*>(*output));
    return result;
}

HRESULT WINAPI HookFactory1(REFIID iid, void** output) {
    const auto original = Original<FactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    if (!original) return E_UNEXPECTED;
    const HRESULT result = original(iid, output);
    if (SUCCEEDED(result) && output) ObserveFactory(static_cast<IUnknown*>(*output));
    return result;
}

HRESULT WINAPI HookFactory2(UINT flags, REFIID iid, void** output) {
    const auto original = Original<Factory2Fn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));
    if (!original) return E_UNEXPECTED;
    const HRESULT result = original(flags, iid, output);
    if (SUCCEEDED(result) && output) ObserveFactory(static_cast<IUnknown*>(*output));
    return result;
}

}  // namespace

bool InstallDx12Hooks() {
    static const bool installed = [] {
        try {
            (void)State();
            const auto status = MH_Initialize();
            if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
            dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!dxgi) return false;
            // No probe window, device, swap chain, or guessed command queue.
            return InstallGroup({
                {reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory")), reinterpret_cast<void*>(HookFactory)},
                {reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")), reinterpret_cast<void*>(HookFactory1)},
                {reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")), reinterpret_cast<void*>(HookFactory2)},
            });
        } catch (...) {
            return false;
        }
    }();
    return installed;
}

bool TryForwardNativeWin32Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result) noexcept {
    result = 0;
    try {
        auto& state = State();
        // Synchronous window messages can originate on a rendering thread.
        // Never make the window thread wait behind a Present/resize callback.
        const std::unique_lock lock(state.mutex, std::try_to_lock);
        if (!lock) return false;
        if (state.context && state.window == hwnd && !state.uiFailed) {
            const CurrentContext context(state.context);
            result = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        }
    } catch (...) {
        // Don't let an overlay exception cross the game's WndProc.
    }
    return true;
}


NativeWin32MessageHandler GetNativeWin32MessageHandler() noexcept {
    return ForwardWindowMessage;
}

}  // namespace gloss::dndg
