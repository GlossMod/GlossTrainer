#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <memory>

namespace gloss::graphics {
// The caller holds OverlayMutex for every call and destroys a renderer before
// selecting another one. No renderer work is performed from DllMain.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool Draw() = 0;
    // Return false if GPU work still references our resources. The caller must
    // preserve this renderer and defer ResizeBuffers/destruction in that case.
    virtual bool BeforeResize() = 0;
    virtual bool DeviceLost() const = 0;
};

std::unique_ptr<Renderer> CreateDX11Renderer(IDXGISwapChain* swapChain);
std::unique_ptr<Renderer> CreateDX12Renderer(IDXGISwapChain* swapChain,
                                         ID3D12CommandQueue* queue);
}
