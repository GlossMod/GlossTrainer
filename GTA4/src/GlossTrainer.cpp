#include "GlossTrainer.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Present oPresent;

HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device *pDevice = NULL;
ID3D11DeviceContext *pContext = NULL;
ID3D11RenderTargetView *mainRenderTargetView;

void InitImGui()
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange; // 鼠标修改

    trainer::SetConfig(io);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);
}

LRESULT WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

    if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

bool init = false;
HRESULT hkPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void **)&pDevice)))
        {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            window = sd.OutputWindow;
            ID3D11Texture2D *pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&pBackBuffer);
            pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            InitImGui();
            init = true;
        }
        else
        {
            return oPresent(pSwapChain, SyncInterval, Flags);
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    trainer::init();

    // ImGui::Begin("ImGui Window");
    // ImGui::End();

    ImGui::Render();

    pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
    bool init_hook = false;
    while (!init_hook)
    {
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
        {
            kiero::bind(8, (void **)&oPresent, (void *)hkPresent);
            init_hook = true;
        }
    }
    return TRUE;
}

// 入口函数
BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID lpReserved)
{
    Logger::info("GlossTrainer", "GlossTrainer loaded");

    if (reason == DLL_PROCESS_ATTACH)
    {

        CreateThread(nullptr, 0, MainThread, handle, 0, nullptr);
    }

    return TRUE;
}