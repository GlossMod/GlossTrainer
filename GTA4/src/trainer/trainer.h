#pragma once

#include "../imgui/imgui_impl_win32.h"
#include "../imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include "../imgui/imgui.h"

#include "../dllmain.hpp"

namespace trainer
{
    void init();
    void SetConfig(ImGuiIO &io);
}