#include "runtime.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>

namespace gloss::overlay {
namespace {
struct Settings {
    bool light{};
    float scale = 1.0f;
    float x = -1.0f, y = -1.0f;
} settings;
std::atomic_bool visible{};
std::atomic_bool rendererReady{};
HWND currentWindow{};
bool dirty{}, forcePosition = true;
int page{};
ULONGLONG initializedAt{}, savedAt{};
float previousScale{};
bool previousLight{};
ImVec4 accent;

float ReadNumber(const wchar_t* name, float fallback) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"UI", name, L"", text, static_cast<DWORD>(std::size(text)), (DataDirectory() / L"ui.ini").c_str());
    if (!*text) return fallback;
    wchar_t* end{};
    const auto number = std::wcstof(text, &end);
    return end != text && !*end && std::isfinite(number) ? number : fallback;
}
void LoadSettings() {
    settings.light = ReadNumber(L"LightTheme", 0) == 1;
    settings.scale = std::clamp(ReadNumber(L"Scale", 1), 0.8f, 1.5f);
    settings.x = ReadNumber(L"X", -1);
    settings.y = ReadNumber(L"Y", -1);
}
void SaveSettings() {
    if (!dirty) return;
    try {
        std::filesystem::create_directories(DataDirectory());
        const auto file = DataDirectory() / L"ui.ini";
        const auto write = [&](const wchar_t* key, auto value) {
            const auto text = std::format(L"{}", value);
            if (!WritePrivateProfileStringW(L"UI", key, text.c_str(), file.c_str())) Log("Could not save UI settings");
        };
        write(L"LightTheme", settings.light ? 1 : 0);
        write(L"Scale", settings.scale);
        write(L"X", settings.x);
        write(L"Y", settings.y);
        dirty = false;
        savedAt = GetTickCount64();
    } catch (...) { Log("Could not save UI settings"); }
}
ImVec4 Color(unsigned red, unsigned green, unsigned blue, unsigned alpha = 255) {
    return ImVec4(red / 255.f, green / 255.f, blue / 255.f, alpha / 255.f);
}
void ApplyStyle(float scale) {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (settings.light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    accent = settings.light ? Color(156, 121, 61) : Color(198, 165, 107);
    style.Colors[ImGuiCol_WindowBg] = settings.light ? Color(244, 245, 247) : Color(23, 25, 30);
    style.Colors[ImGuiCol_ChildBg] = settings.light ? Color(255, 255, 255) : Color(32, 35, 41);
    style.Colors[ImGuiCol_Text] = settings.light ? Color(32, 36, 43) : Color(235, 238, 243);
    style.Colors[ImGuiCol_TextDisabled] = settings.light ? Color(112, 120, 132) : Color(140, 149, 164);
    style.Colors[ImGuiCol_Border] = settings.light ? Color(218, 222, 229) : Color(53, 59, 69);
    style.Colors[ImGuiCol_FrameBg] = settings.light ? Color(235, 237, 242) : Color(45, 50, 59);
    style.Colors[ImGuiCol_FrameBgHovered] = settings.light ? Color(225, 225, 221) : Color(64, 61, 52);
    style.Colors[ImGuiCol_FrameBgActive] = style.Colors[ImGuiCol_FrameBgHovered];
    style.Colors[ImGuiCol_Button] = settings.light ? Color(242, 237, 229) : Color(49, 46, 41);
    style.Colors[ImGuiCol_ButtonHovered] = settings.light ? Color(231, 222, 206) : Color(67, 59, 45);
    style.Colors[ImGuiCol_ButtonActive] = settings.light ? Color(222, 209, 185) : Color(81, 68, 47);
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accent;
    style.Colors[ImGuiCol_NavCursor] = accent;
    style.WindowPadding = ImVec2(0, 0);
    style.ChildRounding = 8;
    style.WindowRounding = 10;
    style.FrameRounding = 5;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.FramePadding = ImVec2(12, 9);
    style.ItemSpacing = ImVec2(12, 12);
    style.ScaleAllSizes(scale);
    style.FontSizeBase = 16;
    style.FontScaleMain = scale;
}
void Label(const char* text, float size, ImVec4 color) {
    ImGui::PushFont(nullptr, size);
    ImGui::TextColored(color, "%s", text);
    ImGui::PopFont();
}
void BeginCard(const char* id, ImVec2 size, float scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 20 * scale));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
}
void EndCard() { ImGui::EndChild(); ImGui::PopStyleVar(); }
void Overview(const char* renderer, float scale) {
    const auto width = ImGui::GetContentRegionAvail().x;
    BeginCard("status", ImVec2((width - 14 * scale) * .5f, 100 * scale), scale);
    ImGui::TextDisabled("运行状态");
    Label("已加载", 22, accent);
    EndCard();
    ImGui::SameLine(0, 14 * scale);
    BeginCard("renderer", ImVec2(0, 100 * scale), scale);
    ImGui::TextDisabled("渲染后端");
    Label(renderer, 22, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * scale));
    BeginCard("ready", ImVec2(0, 160 * scale), scale);
    Label("基础框架已就绪", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    ImGui::TextWrapped("通过左侧导航查看功能与设置。");
    ImGui::TextDisabled("功能尚未实现。");
    EndCard();
}
void FeaturePage(float scale) {
    BeginCard("empty", ImVec2(0, 230 * scale), scale);
    ImGui::Dummy(ImVec2(0, 30 * scale));
    Label("功能尚未实现", 24, accent);
    ImGui::Spacing();
    ImGui::TextWrapped("此版本提供游戏内界面基础框架。");
    ImGui::TextDisabled("后续功能将在这里展示。");
    EndCard();
}
void SettingsPage(float scale) {
    BeginCard("settings", ImVec2(0, 280 * scale), scale);
    Label("外观", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    ImGui::TextDisabled("主题");
    if (ImGui::RadioButton("暗色", !settings.light)) { settings.light = false; dirty = true; }
    ImGui::SameLine(0, 36 * scale);
    if (ImGui::RadioButton("亮色", settings.light)) { settings.light = true; dirty = true; }
    ImGui::Spacing();
    ImGui::TextDisabled("界面缩放");
    ImGui::SetNextItemWidth(std::min(300 * scale, ImGui::GetContentRegionAvail().x));
    if (ImGui::SliderFloat("##scale", &settings.scale, .8f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
        dirty = true; forcePosition = true;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("设置会自动保存。按 Insert 显示或隐藏菜单。");
    EndCard();
}
}
bool Visible() { return visible.load(); }
void SetVisible(bool show) {
    if (show && !rendererReady.load()) return;
    if (visible.exchange(show) == show) return;
    input::SetMenuVisible(show);
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    if (!show) SaveSettings();
    Log(show ? "Menu opened" : "Menu hidden");
}
void SetRendererReady(bool ready) {
    rendererReady = ready;
    if (!ready) SetVisible(false);
}
void OnFocusLost() {
    SetVisible(false);
    if (ImGui::GetCurrentContext()) ImGui::GetIO().AddFocusEvent(false);
}
bool Initialize(HWND window) {
    if (currentWindow == window && ImGui::GetCurrentContext()) return true;
    if (!ImGui::GetCurrentContext()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
        wchar_t windowsDirectory[MAX_PATH]{};
        GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        auto font = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyh.ttc";
        if (!std::filesystem::exists(font)) font = std::filesystem::path(windowsDirectory) / L"Fonts" / L"simhei.ttf";
        const auto utf8 = font.u8string();
        if (!std::filesystem::exists(font) || !io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), 16)) {
            io.Fonts->AddFontDefault();
            Log("Chinese font unavailable; using default font");
        }
        LoadSettings();
        initializedAt = GetTickCount64();
    } else if (currentWindow) {
        SetVisible(false);
        ImGui_ImplWin32_Shutdown();
        input::Shutdown();
    }
    if (!ImGui_ImplWin32_Init(window)) { Log("ImGui Win32 backend initialization failed"); return false; }
    currentWindow = window;
    if (!input::Attach(window)) {
        ImGui_ImplWin32_Shutdown(); currentWindow = nullptr;
        Log("Window subclass failed; overlay disabled"); return false;
    }
    Log("ImGui initialized with Simplified Chinese UI");
    return true;
}
void DrawFrame(const char* renderer) {
    if (!currentWindow) return;
    auto& io = ImGui::GetIO();
    io.MouseDrawCursor = Visible();
    if (Visible()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    const auto dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(currentWindow);
    RECT client{}; GetClientRect(currentWindow, &client);
    const auto fit = std::min((client.right - client.left) / 920.f, (client.bottom - client.top) / 600.f);
    const float scale = std::max(.5f, std::min(dpi * settings.scale, fit));
    if (previousScale != scale || previousLight != settings.light) {
        if (previousScale != scale) forcePosition = true;
        ApplyStyle(scale); previousScale = scale; previousLight = settings.light;
    }
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (Visible()) {
        const ImVec2 size(880 * scale, 560 * scale);
        ImGui::SetNextWindowSize(size);
        if (forcePosition) {
            ImVec2 position(settings.x, settings.y);
            if (position.x < 0 || position.y < 0) position = ImVec2((io.DisplaySize.x - size.x) * .5f, (io.DisplaySize.y - size.y) * .5f);
            position.x = std::clamp(position.x, 0.f, std::max(0.f, io.DisplaySize.x - size.x));
            position.y = std::clamp(position.y, 0.f, std::max(0.f, io.DisplaySize.y - size.y));
            ImGui::SetNextWindowPos(position);
            forcePosition = false;
        }
        ImGui::Begin("GlossTrainer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
        const auto origin = ImGui::GetWindowPos();
        if (settings.x != origin.x || settings.y != origin.y) { settings.x = origin.x; settings.y = origin.y; dirty = true; }
        auto* draw = ImGui::GetWindowDrawList();
        const auto border = ImGui::GetColorU32(ImGuiCol_Border);
        draw->AddLine(ImVec2(origin.x, origin.y + 62 * scale), ImVec2(origin.x + size.x, origin.y + 62 * scale), border);
        draw->AddLine(ImVec2(origin.x, origin.y + 516 * scale), ImVec2(origin.x + size.x, origin.y + 516 * scale), border);
        draw->AddLine(ImVec2(origin.x + 168 * scale, origin.y + 62 * scale), ImVec2(origin.x + 168 * scale, origin.y + 516 * scale), border);
        ImGui::SetCursorPos(ImVec2(22 * scale, 18 * scale));
        Label("GlossTrainer", 22, accent);
        ImGui::SameLine(0, 20 * scale); ImGui::TextUnformatted("黑神话：悟空");
        ImGui::SameLine(0, 18 * scale); ImGui::TextDisabled("v%s", Version);
        ImGui::SetCursorPos(ImVec2(size.x - 48 * scale, 14 * scale));
        if (ImGui::Button("X##close", ImVec2(32 * scale, 32 * scale))) SetVisible(false);
        ImGui::SetCursorPos(ImVec2(12 * scale, 80 * scale));
        ImGui::BeginGroup();
        constexpr const char* pages[] = {"概览", "修改功能", "设置"};
        for (int index = 0; index < 3; ++index) {
            ImGui::PushID(index);
            const bool selected = index == page;
            if (!selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            if (ImGui::Button(pages[index], ImVec2(144 * scale, 42 * scale))) page = index;
            ImGui::PopStyleColor();
            if (!selected) ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 3 * scale));
        }
        ImGui::EndGroup();
        ImGui::SetCursorPos(ImVec2(192 * scale, 82 * scale));
        ImGui::BeginChild("content", ImVec2(664 * scale, 416 * scale), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        Label(pages[page], 28, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::TextDisabled(page == 0 ? "游戏内工具面板" : page == 1 ? "修改功能将在这里呈现" : "让界面更合你的习惯");
        ImGui::Dummy(ImVec2(0, 10 * scale));
        if (page == 0) Overview(renderer, scale);
        else if (page == 1) FeaturePage(scale);
        else SettingsPage(scale);
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(22 * scale, 530 * scale));
        ImGui::TextColored(accent, "Insert"); ImGui::SameLine(); ImGui::TextDisabled("显示或隐藏菜单");
        ImGui::End();
    } else if (GetTickCount64() - initializedAt < 6500) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, 36 * scale), ImGuiCond_Always, ImVec2(.5f, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * scale, 12 * scale));
        ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(accent, "GlossTrainer"); ImGui::SameLine(); ImGui::TextUnformatted("已加载  |  Insert 打开菜单");
        ImGui::End(); ImGui::PopStyleVar();
    }
    ImGui::Render();
    if (dirty && GetTickCount64() - savedAt > 1000 && !ImGui::IsMouseDown(0)) SaveSettings();
}
}
