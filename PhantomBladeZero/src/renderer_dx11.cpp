#include "renderer.hpp"
#include "runtime.hpp"
#include "item_icons.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <array>

namespace gloss::graphics
{
    using Microsoft::WRL::ComPtr;
    namespace
    {
        // ImGui restores its pipeline state. The render targets and output UAVs belong
        // to the host, so keep those separately while binding our swap-chain RTV.
        class OutputState
        {
        public:
            explicit OutputState(ID3D11DeviceContext *context) : context_(context)
            {
                context_->OMGetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), &depth_);
                context_->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr,
                                                                    0, static_cast<UINT>(uavs_.size()), uavs_.data());
                for (UINT i = 0; i < targets_.size(); ++i)
                    if (targets_[i])
                        count_ = i + 1;
            }
            ~OutputState()
            {
                std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> counts;
                counts.fill(UINT(-1));
                context_->OMSetRenderTargetsAndUnorderedAccessViews(count_, targets_.data(), depth_,
                                                                    count_, static_cast<UINT>(uavs_.size()) - count_, uavs_.data() + count_, counts.data());
                for (auto *target : targets_)
                    if (target)
                        target->Release();
                for (auto *uav : uavs_)
                    if (uav)
                        uav->Release();
                if (depth_)
                    depth_->Release();
            }

        private:
            ID3D11DeviceContext *context_;
            std::array<ID3D11RenderTargetView *, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets_{};
            std::array<ID3D11UnorderedAccessView *, D3D11_PS_CS_UAV_REGISTER_COUNT> uavs_{};
            ID3D11DepthStencilView *depth_{};
            UINT count_{};
        };

        class DX11Renderer final : public Renderer
        {
        public:
            bool Initialize(IDXGISwapChain *swapChain)
            {
                if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device_))))
                    return false;
                device_->GetImmediateContext(&context_);
                ComPtr<ID3D11Texture2D> buffer;
                if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer))) ||
                    FAILED(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_)))
                    return false;
                initialized_ = ImGui_ImplDX11_Init(device_.Get(), context_.Get());
                if (initialized_ && !ImGui_ImplDX11_CreateDeviceObjects())
                {
                    Log("DX11 backend device-object initialization failed");
                    return false;
                }
                if (initialized_)
                {
                    ui::SetItemIconRetirementCallback([this](std::span<ImTextureData* const> textures) {
                        return RetireItemTextures(textures);
                    });
                    iconCallbackBound_ = true;
                    Log("DX11 renderer initialized");
                }
                return initialized_;
            }
            ~DX11Renderer() override
            {
                if (iconCallbackBound_) ui::SetItemIconRetirementCallback({});
                BeforeResize();
                if (initialized_)
                    ImGui_ImplDX11_Shutdown();
            }
            bool Draw() override
            {
                if (!initialized_ || !target_ || DeviceLost())
                    return false;
                OutputState state(context_.Get());
                ImGui_ImplDX11_NewFrame();
                overlay::DrawFrame("DirectX 11");
                ID3D11RenderTargetView *target = target_.Get();
                context_->OMSetRenderTargets(1, &target, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                return true;
            }
            bool BeforeResize() override
            {
                target_.Reset();
                return true;
            }
            bool DeviceLost() const override
            {
                return device_ && FAILED(device_->GetDeviceRemovedReason());
            }

        private:
            bool RetireItemTextures(std::span<ImTextureData* const> textures)
            {
                // Called at frame start, before any new item draw commands exist.
                // A fresh marker covers all prior uses, including frames since a
                // previous retirement attempt timed out.
                if (!DeviceLost())
                {
                    D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
                    ComPtr<ID3D11Query> marker;
                    if (FAILED(device_->CreateQuery(&description, &marker))) return false;
                    context_->End(marker.Get());
                    context_->Flush();
                    const auto deadline = GetTickCount64() + 32;
                    for (;;)
                    {
                        BOOL complete = FALSE;
                        const auto status = context_->GetData(marker.Get(), &complete, sizeof(complete),
                            D3D11_ASYNC_GETDATA_DONOTFLUSH);
                        if (status == S_OK && complete) break;
                        if (DeviceLost()) break;
                        if (FAILED(status) || GetTickCount64() >= deadline) return false;
                        Sleep(1);
                    }
                }
                for (auto* texture : textures)
                {
                    texture->WantDestroyNextFrame = true;
                    texture->UnusedFrames = 1;
                    texture->SetStatus(ImTextureStatus_WantDestroy);
                    ImGui_ImplDX11_UpdateTexture(texture);
                }
                return true;
            }
            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11RenderTargetView> target_;
            bool initialized_{}, iconCallbackBound_{};
        };
    }

    std::unique_ptr<Renderer> CreateDX11Renderer(IDXGISwapChain *swapChain)
    {
        auto renderer = std::make_unique<DX11Renderer>();
        if (!renderer->Initialize(swapChain))
            return {};
        return renderer;
    }
}
