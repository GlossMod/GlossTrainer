#include "graphics.hpp"
#include "renderer.hpp"
#include "runtime.hpp"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gloss::graphics
{
    using Microsoft::WRL::ComPtr;
    namespace
    {
        using FactoryFn = HRESULT(WINAPI *)(REFIID, void **);
        using Factory2Fn = HRESULT(WINAPI *)(UINT, REFIID, void **);
        using DeviceSwapFn = decltype(&D3D11CreateDeviceAndSwapChain);
        using CreateSwapFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
        using CreateHwndFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory2 *, IUnknown *, HWND,
                                                          const DXGI_SWAP_CHAIN_DESC1 *, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
        using CreateCoreFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory2 *, IUnknown *, IUnknown *,
                                                          const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *, IDXGISwapChain1 **);
        using CreateCompositionFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory2 *, IUnknown *,
                                                                 const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *, IDXGISwapChain1 **);
        using PresentFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
        using Present1Fn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
        using ResizeFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using Resize1Fn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *, UINT, UINT, UINT, DXGI_FORMAT,
                                                       UINT, const UINT *, IUnknown *const *);

        FactoryFn originalFactory{}, originalFactory1{};
        Factory2Fn originalFactory2{};
        DeviceSwapFn originalDeviceSwap{};
        std::mutex hookMutex;
        std::unordered_map<void *, void *> originals;
        thread_local unsigned presentDepth{};
        struct Chain
        {
            ComPtr<ID3D12CommandQueue> queue;
            HWND window{};
            UINT64 generation{};
            UINT resizing{};
            bool hooksReady{true};
            bool queueMissingLogged{}, initFailed{}, deviceLost{}, rebuildRequested{};
        };
        // These are deliberately process-lifetime objects. CRT static destruction runs
        // beneath the loader lock; releasing an active DX12 backend there could wait
        // for a GPU or re-enter the graphics driver. Normal resize/recovery still tears
        // down renderers explicitly, outside DllMain.
        auto &chains = *new std::unordered_map<IUnknown *, Chain>;
        auto &renderer = *new std::unique_ptr<Renderer>;
        IUnknown *activeChain{};
        HWND mainWindow{};
        UINT64 generation{};

        bool ReleaseRenderer()
        {
            if (!renderer)
                return true;
            if (!renderer->BeforeResize())
            {
                overlay::SetRendererReady(false);
                return false;
            }
            renderer.reset();
            return true;
        }

        void *Method(IUnknown *object, size_t index)
        {
            return (*reinterpret_cast<void ***>(object))[index];
        }
        IUnknown *Identity(IUnknown *object)
        {
            if (!object)
                return nullptr;
            ComPtr<IUnknown> identity;
            if (FAILED(object->QueryInterface(IID_PPV_ARGS(&identity))))
                return nullptr;
            return identity.Get();
        }
        template <class T, class Object>
        T Original(Object *object, size_t index)
        {
            std::lock_guard lock(hookMutex);
            const auto found = originals.find(Method(object, index));
            return found == originals.end() ? nullptr : reinterpret_cast<T>(found->second);
        }
        bool AddHook(void *target, void *replacement, void **trampoline = nullptr)
        {
            if (!target)
                return false;
            std::lock_guard lock(hookMutex);
            const auto found = originals.find(target);
            if (found != originals.end())
            {
                if (trampoline)
                    *trampoline = found->second;
                const auto status = MH_QueueEnableHook(target);
                if (status == MH_OK || status == MH_ERROR_ENABLED)
                    return true;
                Log(std::string("MinHook graphics hook re-enable failed: ") + MH_StatusToString(status));
                return false;
            }
            void *original{};
            const auto status = MH_CreateHook(target, replacement, &original);
            if (status != MH_OK)
            {
                Log(std::string("MinHook graphics hook creation failed: ") + MH_StatusToString(status));
                return false;
            }
            originals.emplace(target, original);
            if (trampoline)
                *trampoline = original;
            const auto queued = MH_QueueEnableHook(target);
            if (queued != MH_OK)
            {
                Log(std::string("MinHook graphics hook enable failed: ") + MH_StatusToString(queued));
                return false;
            }
            return true;
        }
        bool ApplyHooks()
        {
            std::lock_guard lock(hookMutex);
            const auto status = MH_ApplyQueued();
            if (status != MH_OK)
                Log(std::string("MinHook graphics apply failed: ") + MH_StatusToString(status));
            return status == MH_OK;
        }

        HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain *, UINT, UINT);
        HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
        HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3 *, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                              const UINT *, IUnknown *const *);
        HRESULT STDMETHODCALLTYPE HookCreateSwap(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
        HRESULT STDMETHODCALLTYPE HookCreateHwnd(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
        HRESULT STDMETHODCALLTYPE HookCreateCore(IDXGIFactory2 *, IUnknown *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *,
                                                 IDXGIOutput *, IDXGISwapChain1 **);
        HRESULT STDMETHODCALLTYPE HookCreateComposition(IDXGIFactory2 *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *,
                                                        IDXGIOutput *, IDXGISwapChain1 **);

        bool InstallSwapHooks(IDXGISwapChain *chain)
        {
            bool added = AddHook(Method(chain, 8), reinterpret_cast<void *>(HookPresent));
            added &= AddHook(Method(chain, 13), reinterpret_cast<void *>(HookResize));
            ComPtr<IDXGISwapChain1> chain1;
            if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain1))))
                added &= AddHook(Method(chain1.Get(), 22), reinterpret_cast<void *>(HookPresent1));
            ComPtr<IDXGISwapChain3> chain3;
            if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain3))))
                added &= AddHook(Method(chain3.Get(), 39), reinterpret_cast<void *>(HookResize1));
            const bool applied = ApplyHooks();
            if (!added || !applied)
                Log("Swap-chain hook installation incomplete; overlay disabled for this chain");
            return added && applied;
        }
        bool InstallFactoryHooks(IUnknown *object)
        {
            if (!object)
                return false;
            ComPtr<IDXGIFactory> factory;
            if (FAILED(object->QueryInterface(IID_PPV_ARGS(&factory))))
                return false;
            bool added = AddHook(Method(factory.Get(), 10), reinterpret_cast<void *>(HookCreateSwap));
            ComPtr<IDXGIFactory2> factory2;
            if (SUCCEEDED(factory.As(&factory2)))
            {
                added &= AddHook(Method(factory2.Get(), 15), reinterpret_cast<void *>(HookCreateHwnd));
                added &= AddHook(Method(factory2.Get(), 16), reinterpret_cast<void *>(HookCreateCore));
                added &= AddHook(Method(factory2.Get(), 24), reinterpret_cast<void *>(HookCreateComposition));
            }
            const bool applied = ApplyHooks();
            if (!added || !applied)
                Log("DXGI factory hook installation incomplete; affected swap chains may be skipped");
            return added && applied;
        }
        void RecordChain(IDXGISwapChain *swapChain, IUnknown *source, HWND requestedWindow = nullptr)
        {
            if (!swapChain)
                return;
            const bool hooked = InstallSwapHooks(swapChain);
            std::lock_guard lock(OverlayMutex());
            const auto key = Identity(swapChain);
            if (!key)
                return;
            Chain state;
            state.hooksReady = hooked;
            // Some wrappers return an existing COM identity from a creation call. A
            // refreshed queue/description must never keep using that identity's old
            // backend. Defer safe GPU release until Present instead of waiting in a
            // factory callback. The old renderer keeps its own queue/resources alive.
            state.rebuildRequested = key == activeChain && bool(renderer);
            if (key == activeChain && (!hooked || state.rebuildRequested))
                overlay::SetRendererReady(false);
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(swapChain->GetDesc(&desc)))
                state.window = desc.OutputWindow;
            if (!state.window)
                state.window = requestedWindow;
            if (source)
            {
                ComPtr<ID3D12CommandQueue> queue;
                if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&queue))) &&
                    queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
                {
                    ComPtr<ID3D12Device> queueDevice, chainDevice;
                    if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) &&
                        SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&chainDevice))) &&
                        Identity(queueDevice.Get()) == Identity(chainDevice.Get()))
                        state.queue = queue;
                }
            }
            state.generation = ++generation;
            chains.insert_or_assign(key, std::move(state));
            Log("Graphics swap chain captured");
        }

        HWND FindMainWindow()
        {
            struct Search
            {
                HWND window{};
                LONG64 area{};
            } search;
            EnumWindows([](HWND window, LPARAM param) -> BOOL
                        {
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId() || !IsWindowVisible(window) ||
            GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            return TRUE;
        RECT client{};
        if (!GetClientRect(window, &client)) return TRUE;
        const auto area = LONG64(client.right - client.left) * (client.bottom - client.top);
        auto* result = reinterpret_cast<Search*>(param);
        if (area > result->area) { result->area = area; result->window = window; }
        return TRUE; }, reinterpret_cast<LPARAM>(&search));
            return search.window;
        }

        void Draw(IDXGISwapChain *swapChain, UINT flags)
        {
            if (flags & DXGI_PRESENT_TEST)
                return;
            std::lock_guard lock(OverlayMutex());
            const auto key = Identity(swapChain);
            if (!key)
                return;
            auto found = chains.find(key);
            if (found == chains.end())
            {
                // A chain may predate plugin initialization. DX11 can be recovered by
                // GetDevice; DX12 deliberately waits for a known queue association.
                DXGI_SWAP_CHAIN_DESC desc{};
                if (FAILED(swapChain->GetDesc(&desc)))
                    return;
                Chain state;
                state.window = desc.OutputWindow;
                state.generation = ++generation;
                found = chains.emplace(key, std::move(state)).first;
            }
            auto &state = found->second;
            if (key == activeChain && state.rebuildRequested)
            {
                if (!ReleaseRenderer())
                    return;
                state.rebuildRequested = false;
            }
            if (!state.hooksReady || state.initFailed || state.deviceLost || !state.window ||
                !IsWindow(state.window) || IsIconic(state.window) || !IsWindowVisible(state.window))
            {
                if (key == activeChain)
                    overlay::SetRendererReady(false);
                return;
            }
            if (state.resizing)
                return;
            if (!mainWindow || !IsWindow(mainWindow) || !IsWindowVisible(mainWindow))
                mainWindow = FindMainWindow();
            if (state.window != mainWindow)
                return;
            RECT rect{};
            if (!GetClientRect(state.window, &rect) || rect.right <= rect.left || rect.bottom <= rect.top)
            {
                if (key == activeChain)
                    overlay::SetRendererReady(false);
                return;
            }
            if (activeChain != key)
            {
                auto old = chains.find(activeChain);
                if (old != chains.end() && old->second.window == mainWindow &&
                    old->second.generation > state.generation)
                    return;
                if (!ReleaseRenderer())
                    return;
                activeChain = key;
                // Older chains for the same main window cannot become active again
                // without a fresh creation record. Retain their identity/generation
                // markers, but release their redundant strong command-queue refs.
                for (auto &[identity, oldState] : chains)
                    if (identity != key && oldState.window == mainWindow &&
                        oldState.generation < state.generation)
                        oldState.queue.Reset();
            }
            if (!renderer)
            {
                ComPtr<ID3D12Device> device12;
                const bool is12 = SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&device12)));
                if (is12 && !state.queue)
                {
                    overlay::SetRendererReady(false);
                    if (!state.queueMissingLogged)
                    {
                        Log("DX12 swap chain has no verified direct command queue; overlay skipped");
                        state.queueMissingLogged = true;
                    }
                    return;
                }
                if (!overlay::Initialize(state.window))
                {
                    overlay::SetRendererReady(false);
                    return;
                }
                renderer = is12 ? CreateDX12Renderer(swapChain, state.queue.Get()) : CreateDX11Renderer(swapChain);
                if (!renderer)
                {
                    overlay::SetRendererReady(false);
                    state.initFailed = true;
                    Log("Graphics renderer initialization failed; game presentation continues");
                    return;
                }
                overlay::SetRendererReady(true);
            }
            if (renderer->DeviceLost())
            {
                overlay::SetRendererReady(false);
                if (!ReleaseRenderer())
                    return;
                state.deviceLost = true;
                Log("Graphics device lost; overlay disabled until swap-chain recreation");
                return;
            }
            if (renderer->Draw())
                overlay::SetRendererReady(true);
            else if (renderer->DeviceLost())
                overlay::SetRendererReady(false);
        }

        void Presented(IDXGISwapChain *swapChain, HRESULT result)
        {
            if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET)
                return;
            std::lock_guard lock(OverlayMutex());
            const auto key = Identity(swapChain);
            auto found = chains.find(key);
            if (found != chains.end())
                found->second.deviceLost = true;
            if (key == activeChain)
            {
                overlay::SetRendererReady(false);
                ReleaseRenderer();
            }
            Log("DXGI Present reported device removal or reset");
        }
        bool BeforeResize(IDXGISwapChain *swapChain)
        {
            std::lock_guard lock(OverlayMutex());
            const auto key = Identity(swapChain);
            if (!key)
                return true;
            auto [found, inserted] = chains.try_emplace(key);
            if (inserted)
            {
                DXGI_SWAP_CHAIN_DESC desc{};
                if (SUCCEEDED(swapChain->GetDesc(&desc)))
                    found->second.window = desc.OutputWindow;
                found->second.generation = ++generation;
            }
            ++found->second.resizing;
            if (key == activeChain && !ReleaseRenderer())
            {
                --found->second.resizing;
                return false;
            }
            return true;
        }
        void AfterResize(IDXGISwapChain *swapChain, HRESULT result,
                         UINT count = 0, IUnknown *const *queues = nullptr)
        {
            std::lock_guard lock(OverlayMutex());
            const auto key = Identity(swapChain);
            auto found = chains.find(key);
            if (found == chains.end())
                return;
            auto &state = found->second;
            if (state.resizing)
                --state.resizing;
            state.initFailed = false;
            if (FAILED(result))
            {
                if (key == activeChain)
                    overlay::SetRendererReady(false);
                return;
            }
            if (!queues)
                return;
            // ResizeBuffers1 can replace the presenting queue per buffer. A single
            // overlay queue is safe only when every supplied queue has one COM identity.
            state.queue.Reset();
            ComPtr<ID3D12Device> device;
            swapChain->GetDevice(IID_PPV_ARGS(&device));
            IUnknown *identity{};
            if (!count)
            {
                DXGI_SWAP_CHAIN_DESC desc{};
                if (SUCCEEDED(swapChain->GetDesc(&desc)))
                    count = desc.BufferCount;
            }
            for (UINT i = 0; i < count; ++i)
            {
                ComPtr<ID3D12CommandQueue> queue;
                ComPtr<ID3D12Device> queueDevice;
                if (!queues[i] || FAILED(queues[i]->QueryInterface(IID_PPV_ARGS(&queue))) ||
                    queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
                    FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
                    Identity(device.Get()) != Identity(queueDevice.Get()) ||
                    (identity && identity != Identity(queue.Get())))
                {
                    state.queue.Reset();
                    if (key == activeChain)
                        overlay::SetRendererReady(false);
                    Log("ResizeBuffers1 supplied unverified or multiple presenting queues; overlay skipped");
                    return;
                }
                identity = Identity(queue.Get());
                state.queue = queue;
            }
            state.queueMissingLogged = false;
        }

        void RenderingException(IDXGISwapChain *chain, const char *message)
        {
            std::lock_guard lock(OverlayMutex());
            if (Identity(chain) == activeChain)
            {
                overlay::SetRendererReady(false);
                const auto found = chains.find(activeChain);
                if (found != chains.end())
                    found->second.initFailed = true;
            }
            Log(message);
        }

        struct PresentScope
        {
            PresentScope() { ++presentDepth; }
            ~PresentScope() { --presentDepth; }
        };
        HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain *chain, UINT sync, UINT flags)
        {
            auto original = Original<PresentFn>(chain, 8);
            if (!original)
                return DXGI_ERROR_INVALID_CALL;
            PresentScope scope;
            if (presentDepth == 1)
            {
                try
                {
                    Draw(chain, flags);
                }
                catch (...)
                {
                    RenderingException(chain, "Exception in overlay Present; game presentation continues");
                }
            }
            const auto result = original(chain, sync, flags);
            if (presentDepth == 1)
                Presented(chain, result);
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1 *chain, UINT sync, UINT flags,
                                               const DXGI_PRESENT_PARAMETERS *params)
        {
            auto original = Original<Present1Fn>(chain, 22);
            if (!original)
                return DXGI_ERROR_INVALID_CALL;
            PresentScope scope;
            if (presentDepth == 1)
            {
                try
                {
                    Draw(chain, flags);
                }
                catch (...)
                {
                    RenderingException(chain, "Exception in overlay Present1; game presentation continues");
                }
            }
            const auto result = original(chain, sync, flags, params);
            if (presentDepth == 1)
                Presented(chain, result);
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain *chain, UINT count, UINT width, UINT height,
                                             DXGI_FORMAT format, UINT flags)
        {
            auto original = Original<ResizeFn>(chain, 13);
            if (!original)
                return DXGI_ERROR_INVALID_CALL;
            if (!BeforeResize(chain))
                return DXGI_ERROR_WAS_STILL_DRAWING;
            const auto result = original(chain, count, width, height, format, flags);
            AfterResize(chain, result);
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3 *chain, UINT count, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags, const UINT *nodes,
                                              IUnknown *const *queues)
        {
            auto original = Original<Resize1Fn>(chain, 39);
            if (!original)
                return DXGI_ERROR_INVALID_CALL;
            if (!BeforeResize(chain))
                return DXGI_ERROR_WAS_STILL_DRAWING;
            const auto result = original(chain, count, width, height, format, flags, nodes, queues);
            AfterResize(chain, result, count, queues);
            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreateSwap(IDXGIFactory *factory, IUnknown *source,
                                                 DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **output)
        {
            auto original = Original<CreateSwapFn>(factory, 10);
            if (!original)
                return E_FAIL;
            const auto result = original(factory, source, desc, output);
            if (SUCCEEDED(result) && output && *output)
            {
                try
                {
                    RecordChain(*output, source, desc ? desc->OutputWindow : nullptr);
                }
                catch (...)
                {
                    Log("Exception recording CreateSwapChain");
                }
            }
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookCreateHwnd(IDXGIFactory2 *factory, IUnknown *source, HWND window,
                                                 const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                 IDXGIOutput *restrictOutput, IDXGISwapChain1 **output)
        {
            auto original = Original<CreateHwndFn>(factory, 15);
            if (!original)
                return E_FAIL;
            const auto result = original(factory, source, window, desc, fullscreen, restrictOutput, output);
            if (SUCCEEDED(result) && output && *output)
            {
                try
                {
                    RecordChain(*output, source, window);
                }
                catch (...)
                {
                    Log("Exception recording CreateSwapChainForHwnd");
                }
            }
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookCreateCore(IDXGIFactory2 *factory, IUnknown *source, IUnknown *window,
                                                 const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrictOutput, IDXGISwapChain1 **output)
        {
            auto original = Original<CreateCoreFn>(factory, 16);
            if (!original)
                return E_FAIL;
            const auto result = original(factory, source, window, desc, restrictOutput, output);
            if (SUCCEEDED(result) && output && *output)
            {
                try
                {
                    RecordChain(*output, source);
                }
                catch (...)
                {
                    Log("Exception recording CreateSwapChainForCoreWindow");
                }
            }
            return result;
        }
        HRESULT STDMETHODCALLTYPE HookCreateComposition(IDXGIFactory2 *factory, IUnknown *source,
                                                        const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrictOutput, IDXGISwapChain1 **output)
        {
            auto original = Original<CreateCompositionFn>(factory, 24);
            if (!original)
                return E_FAIL;
            const auto result = original(factory, source, desc, restrictOutput, output);
            if (SUCCEEDED(result) && output && *output)
            {
                try
                {
                    RecordChain(*output, source);
                }
                catch (...)
                {
                    Log("Exception recording CreateSwapChainForComposition");
                }
            }
            return result;
        }

        HRESULT WINAPI HookFactory(REFIID iid, void **output)
        {
            const auto result = originalFactory(iid, output);
            if (SUCCEEDED(result) && output && *output)
                InstallFactoryHooks(static_cast<IUnknown *>(*output));
            return result;
        }
        HRESULT WINAPI HookFactory1(REFIID iid, void **output)
        {
            const auto result = originalFactory1(iid, output);
            if (SUCCEEDED(result) && output && *output)
                InstallFactoryHooks(static_cast<IUnknown *>(*output));
            return result;
        }
        HRESULT WINAPI HookFactory2(UINT flags, REFIID iid, void **output)
        {
            const auto result = originalFactory2(flags, iid, output);
            if (SUCCEEDED(result) && output && *output)
                InstallFactoryHooks(static_cast<IUnknown *>(*output));
            return result;
        }
        HRESULT WINAPI HookDeviceSwap(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software,
                                      UINT flags, const D3D_FEATURE_LEVEL *levels, UINT levelCount, UINT sdk,
                                      const DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **chain, ID3D11Device **device,
                                      D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context)
        {
            const auto result = originalDeviceSwap(adapter, type, software, flags, levels, levelCount,
                                                   sdk, desc, chain, device, level, context);
            if (SUCCEEDED(result) && chain && *chain)
            {
                try
                {
                    RecordChain(*chain, nullptr, desc ? desc->OutputWindow : nullptr);
                }
                catch (...)
                {
                    Log("Exception recording D3D11CreateDeviceAndSwapChain");
                }
            }
            return result;
        }

    }

    bool InstallHooks()
    {
        static std::once_flag once;
        static bool installed{};
        std::call_once(once, []
                       {
        Log("Graphics initialization: loading dxgi.dll");
        const HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        Log(dxgi ? "Graphics initialization: dxgi.dll loaded" : "Graphics initialization: dxgi.dll load failed");
        Log("Graphics initialization: loading d3d11.dll");
        const HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
        Log(d3d11 ? "Graphics initialization: d3d11.dll loaded" : "Graphics initialization: d3d11.dll load failed");
        if (!dxgi || !d3d11) { Log("Unable to load DXGI/D3D11 for graphics hooks"); return; }
        Log("Graphics initialization: creating CreateDXGIFactory hook");
        const bool factory = AddHook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory")),
            reinterpret_cast<void*>(HookFactory), reinterpret_cast<void**>(&originalFactory));
        Log("Graphics initialization: creating CreateDXGIFactory1 hook");
        const bool factory1 = AddHook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")),
            reinterpret_cast<void*>(HookFactory1), reinterpret_cast<void**>(&originalFactory1));
        Log("Graphics initialization: creating CreateDXGIFactory2 hook");
        const bool factory2 = AddHook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")),
            reinterpret_cast<void*>(HookFactory2), reinterpret_cast<void**>(&originalFactory2));
        Log("Graphics initialization: creating D3D11CreateDeviceAndSwapChain hook");
        const bool deviceSwap = AddHook(reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain")),
            reinterpret_cast<void*>(HookDeviceSwap), reinterpret_cast<void**>(&originalDeviceSwap));
        Log("Graphics initialization: applying discovery hooks");
        const bool applied = ApplyHooks();
        Log(applied ? "Graphics initialization: discovery hooks applied" :
            "Graphics initialization: discovery hook application failed");
        // ASI initialization may run during the game's protected startup, well
        // before its graphics environment is ready. Creating a probe factory,
        // HWND or device here can re-enter driver/loader initialization. Instead
        // hook each real factory synchronously before returning it to the game,
        // then hook each real swap chain before its caller can Present. This
        // preserves the DX12 queue association without speculative GPU work.
        Log("Graphics initialization: factory and device bootstrap probes skipped");
        installed = factory && factory1 && factory2 && deviceSwap && applied;
        Log(installed ? "Graphics DX11/DX12 discovery hooks installed" : "Graphics hooks installed with errors"); });
        return installed;
    }
}
