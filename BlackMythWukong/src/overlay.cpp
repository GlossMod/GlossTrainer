#include "runtime.hpp"
#include "features.hpp"
#include "ime.hpp"
#include "update.hpp"
#include "gameplay/extensions.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <string_view>
#include <vector>

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
int page{1};
ULONGLONG initializedAt{}, savedAt{};
float previousScale{};
bool previousLight{};
ImVec4 accent;
ImFont* headingFont{};
std::size_t hotkeyCaptureIndex = std::numeric_limits<std::size_t>::max();
bool openHotkeyCapture{};
bool openMenuHotkeyCapture{};
namespace ext = gameplay::extensions;
struct ExtensionNotice {
    std::string message;
    ULONGLONG shownAt{};
    bool failed{};
} extensionNotice;
struct TravelEditor {
    ext::Vec3 position;
    bool hasCoordinates{}, awaitingPosition{};
    char bookmarkName[128]{};
} travelEditor;
constexpr auto compactTableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
    | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
    | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
constexpr float listRowHeight = 44;
float ListRowHeight(float scale) {
    return std::max(listRowHeight * scale, ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2);
}

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
    style.Colors[ImGuiCol_Header] = style.Colors[ImGuiCol_Button];
    style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_ButtonHovered];
    style.Colors[ImGuiCol_HeaderActive] = style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_Button];
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_ButtonHovered];
    style.Colors[ImGuiCol_TabSelected] = style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_TabSelectedOverline] = accent;
    style.Colors[ImGuiCol_TabDimmed] = style.Colors[ImGuiCol_Button];
    style.Colors[ImGuiCol_TabDimmedSelected] = style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = accent;
    style.Colors[ImGuiCol_TableHeaderBg] = settings.light ? Color(235, 237, 242) : Color(40, 44, 52);
    style.Colors[ImGuiCol_TableRowBg] = settings.light ? Color(255, 255, 255, 180) : Color(32, 35, 41, 140);
    style.Colors[ImGuiCol_TableRowBgAlt] = settings.light ? Color(239, 241, 245, 180) : Color(38, 42, 49, 140);
    style.Colors[ImGuiCol_TableBorderLight] = style.Colors[ImGuiCol_Border];
    style.Colors[ImGuiCol_TableBorderStrong] = style.Colors[ImGuiCol_Border];
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accent;
    style.Colors[ImGuiCol_NavCursor] = accent;
    style.WindowPadding = ImVec2(14, 10);
    style.ChildRounding = 8;
    style.WindowRounding = 6;
    style.PopupRounding = 6;
    style.FrameRounding = 5;
    style.WindowBorderSize = 1;
    style.PopupBorderSize = 1;
    style.ChildBorderSize = 1;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.CellPadding = ImVec2(14, 8);
    style.ScaleAllSizes(scale);
    style.FontSizeBase = 16;
    style.FontScaleMain = scale;
}
void Label(const char* text, float size, ImVec4 color) {
    ImGui::PushFont(size >= 20 ? headingFont : nullptr, size);
    ImGui::TextColored(color, "%s", text);
    ImGui::PopFont();
}
void ColumnText(const char* text, float scale, bool disabled = false) {
    const auto position = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(position, ImVec2(position.x + ImGui::GetContentRegionAvail().x,
        position.y + listRowHeight * scale), true);
    if (disabled) ImGui::TextDisabled("%s", text); else ImGui::TextUnformatted(text);
    draw->PopClipRect();
}
void ListHeaders(float scale) {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 36 * scale);
    for (int column = 0; column < ImGui::TableGetColumnCount(); ++column) {
        ImGui::TableSetColumnIndex(column);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * scale);
        ImGui::TableHeader(ImGui::TableGetColumnName(column));
    }
}
void NavigationIcon(int pageIndex, ImVec2 origin, ImU32 color, float scale) {
    auto* draw = ImGui::GetWindowDrawList();
    const auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
    const auto line = [&](float x1, float y1, float x2, float y2) {
        draw->AddLine(point(x1, y1), point(x2, y2), color, 1.5f * scale);
    };
    switch (pageIndex) {
    case 0:
        line(1, 9, 10, 1); line(10, 1, 19, 9);
        line(4, 7, 4, 19); line(16, 7, 16, 19);
        line(4, 19, 8, 19); line(12, 19, 16, 19);
        line(8, 19, 8, 12); line(8, 12, 12, 12); line(12, 12, 12, 19);
        break;
    case 1:
        line(1, 4, 19, 4); line(1, 10, 19, 10); line(1, 16, 19, 16);
        for (int row = 0; row < 3; ++row)
            draw->AddCircleFilled(point(row == 1 ? 13.f : 6.f, 4.f + row * 6.f), 2.7f * scale,
                ImGui::GetColorU32(ImGuiCol_WindowBg));
        for (int row = 0; row < 3; ++row)
            draw->AddCircle(point(row == 1 ? 13.f : 6.f, 4.f + row * 6.f), 2.2f * scale, color, 0, 1.5f * scale);
        break;
    case 2:
        draw->AddRect(point(3, 6), point(17, 19), color, 2 * scale, 0, 1.5f * scale);
        draw->AddBezierCubic(point(6, 7), point(6, -1), point(14, -1), point(14, 7), color, 1.5f * scale);
        break;
    case 3:
        draw->AddCircle(point(10, 7), 5.5f * scale, color, 0, 1.5f * scale);
        draw->AddCircle(point(10, 7), 1.7f * scale, color, 0, 1.5f * scale);
        line(5, 10, 10, 19); line(10, 19, 15, 10);
        break;
    default:
        draw->AddCircle(point(10, 10), 6.5f * scale, color, 0, 1.5f * scale);
        draw->AddCircle(point(10, 10), 2.5f * scale, color, 0, 1.5f * scale);
        for (int tooth = 0; tooth < 8; ++tooth) {
            const float angle = tooth * 3.14159265f / 4;
            line(10 + std::cos(angle) * 7, 10 + std::sin(angle) * 7,
                10 + std::cos(angle) * 9.5f, 10 + std::sin(angle) * 9.5f);
        }
        break;
    }
}
bool NavigationItem(const char* label, int index, bool selected, float scale) {
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(144 * scale, 46 * scale);
    const bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
    auto* draw = ImGui::GetWindowDrawList();
    if (selected || ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
            ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), 5 * scale);
        if (selected) draw->AddRectFilled(ImVec2(origin.x, origin.y + 4 * scale),
            ImVec2(origin.x + 3 * scale, origin.y + size.y - 4 * scale), ImGui::GetColorU32(accent), 1.5f * scale);
    }
    const auto color = ImGui::GetColorU32(selected ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    NavigationIcon(index, ImVec2(origin.x + 17 * scale, origin.y + 13 * scale), color, scale);
    draw->AddText(ImVec2(origin.x + 50 * scale, origin.y + 15 * scale), color, label);
    return pressed;
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
    unsigned available{}, enabled{};
    for (const auto& definition : features::Definitions()) {
        const auto state = features::GetSnapshot(definition.id);
        if (state.availability == features::Availability::Ready) ++available;
        if (state.enabled) ++enabled;
    }
    const auto catalogTitle = std::format("{} 项功能目录", features::Count);
    Label(catalogTitle.c_str(), 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    ImGui::Text("当前可用 %u 项  ·  已启用 %u 项", available, enabled);
    ImGui::TextDisabled("进入修改功能页查看每项支持状态。");
    ImGui::TextWrapped("所有开关默认关闭。编辑参数后需显式启用或点击应用。");
    EndCard();
}
bool Toggle(const char* id, bool value, float scale) {
    const ImVec2 size(34 * scale, 20 * scale);
    const auto origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    auto* draw = ImGui::GetWindowDrawList();
    const auto fill = ImGui::GetColorU32(value ? accent : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), fill, size.y * .5f);
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
        draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(accent), size.y * .5f);
    draw->AddCircleFilled(ImVec2(origin.x + (value ? size.x - size.y * .5f : size.y * .5f), origin.y + size.y * .5f),
        size.y * .5f - 3 * scale, ImGui::GetColorU32(ImGuiCol_Text));
    return pressed;
}
const char* AvailabilityLabel(const features::Snapshot& state, bool action) {
    if (state.actionFailed) return "失败";
    switch (state.availability) {
    case features::Availability::Ready: return action ? "可用" : state.enabled ? "已启用" : "已关闭";
    case features::Availability::Waiting: return "等待游戏";
    case features::Availability::Error: return "错误";
    default: return "未支持";
    }
}
void TooltipSeparator() {
    const auto position = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    const auto padding = ImGui::GetStyle().WindowPadding;
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(position.x + padding.x, position.y),
        ImVec2(position.x + size.x - padding.x, position.y + size.y), true);
    ImGui::Separator();
    draw->PopClipRect();
}
void FeatureTooltip(const features::Definition& definition, const features::Snapshot& state, float scale, bool parameter = false) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340 * scale);
    ImGui::TextUnformatted(definition.description);
    if (parameter) {
        TooltipSeparator();
        ImGui::Text("范围：%.2f ～ %.2f", definition.minimum, definition.maximum);
        ImGui::TextUnformatted("输入仅暂存，需启用或点击应用。");
    }
    if (!state.message.empty()) {
        TooltipSeparator();
        ImGui::TextUnformatted(state.message.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void FeatureRow(const features::Definition& definition, float scale) {
    const auto state = features::GetSnapshot(definition.id);
    const bool ready = state.availability == features::Availability::Ready;
    const bool action = definition.kind == features::Kind::Action;
    const bool parameter = definition.kind != features::Kind::Toggle && !(action && definition.minimum == definition.maximum);
    ImGui::PushID(static_cast<int>(definition.id));
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ColumnText(definition.name, scale);
    FeatureTooltip(definition, state, scale);

    ImGui::TableSetColumnIndex(1);
    const auto hotkey = features::GetHotkey(definition.id);
    const bool capturing = features::CapturingHotkey(definition.id);
    const bool showClear = !hotkey.label.empty() || capturing;
    ImGui::PushFont(nullptr, 14);
    ImGui::PushStyleColor(ImGuiCol_Text, capturing ? accent : hotkey.label.empty()
        ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    const auto* hotkeyLabel = capturing ? "按下按键…" : hotkey.label.empty() ? "未绑定" : hotkey.label.c_str();
    const float hotkeyWidth = ImGui::GetContentRegionAvail().x - (showClear ? 30 * scale : 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, .5f));
    if (ImGui::Button(hotkeyLabel, ImVec2(hotkeyWidth, 28 * scale))) {
        features::BeginHotkeyCapture(definition.id);
        hotkeyCaptureIndex = static_cast<std::size_t>(definition.id);
        openHotkeyCapture = true;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s点击录制快捷键", hotkey.label.c_str(), hotkey.label.empty() ? "" : "\n");
    if (showClear) {
        ImGui::SameLine(0, 6 * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("×##clear-hotkey", ImVec2(24 * scale, 28 * scale))) {
            features::SetHotkey(definition.id, 0, features::NoModifier);
            if (capturing) features::CancelHotkeyCapture();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("清除快捷键");
    }
    ImGui::PopFont();

    ImGui::TableSetColumnIndex(2);
    if (parameter) {
        double value = state.value;
        const float buttonWidth = 50 * scale;
        ImGui::SetNextItemWidth(std::max(60 * scale, ImGui::GetContentRegionAvail().x - buttonWidth - 8 * scale));
        if (ImGui::InputDouble("##value", &value, 0, 0, definition.integer ? "%.0f" : "%.2f"))
            features::SetValue(definition.id, value);
        FeatureTooltip(definition, state, scale, true);
        ImGui::SameLine(0, 8 * scale);
        ImGui::BeginDisabled(!ready || (definition.kind == features::Kind::ToggleValue && !state.enabled));
        if (ImGui::Button("应用", ImVec2(buttonWidth, 0))) features::Apply(definition.id);
        ImGui::EndDisabled();
        FeatureTooltip(definition, state, scale);
    } else if (action) {
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("应用", ImVec2(70 * scale, 0))) features::Apply(definition.id);
        ImGui::EndDisabled();
        FeatureTooltip(definition, state, scale);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(3);
    const auto statusOrigin = ImGui::GetCursorScreenPos();
    const auto statusHeight = ListRowHeight(scale) - ImGui::GetStyle().CellPadding.y * 2;
    if (!action) {
        ImGui::SetCursorScreenPos(ImVec2(statusOrigin.x, statusOrigin.y + (statusHeight - 20 * scale) * .5f));
        ImGui::BeginDisabled(!ready && !state.enabled);
        if (Toggle("##toggle", state.enabled, scale)) features::SetEnabled(definition.id, !state.enabled);
        ImGui::EndDisabled();
        FeatureTooltip(definition, state, scale);
    }
    const auto statusColor = state.availability == features::Availability::Error || state.actionFailed
        ? Color(214, 133, 83) : (state.enabled ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushFont(nullptr, 14);
    const auto* statusLabel = AvailabilityLabel(state, action);
    const auto statusSize = ImGui::CalcTextSize(statusLabel);
    const ImVec2 statusTextPosition(statusOrigin.x + (action ? 0 : 42 * scale),
        statusOrigin.y + (statusHeight - statusSize.y) * .5f);
    ImGui::SetCursorScreenPos(statusTextPosition);
    ImGui::Dummy(statusSize);
    ImGui::GetWindowDrawList()->AddText(statusTextPosition, ImGui::GetColorU32(statusColor), statusLabel);
    ImGui::PopFont();
    FeatureTooltip(definition, state, scale);
    ImGui::PopID();
}
void HotkeyPopup(float scale) {
    if (openHotkeyCapture) {
        ImGui::OpenPopup("设置快捷键");
        openHotkeyCapture = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * scale, 18 * scale));
    if (ImGui::BeginPopupModal("设置快捷键", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (hotkeyCaptureIndex < features::Count) {
            const auto id = static_cast<features::Id>(hotkeyCaptureIndex);
            if (!features::CapturingHotkey(id)) ImGui::CloseCurrentPopup();
            ImGui::TextUnformatted(features::GetDefinition(id).name);
            ImGui::TextColored(accent, "按下要使用的组合键…");
            ImGui::TextDisabled("Esc 取消，Backspace / Delete 清除");
            const auto state = features::GetSnapshot(id);
            if (state.actionFailed && !state.message.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * scale);
                ImGui::TextColored(Color(214, 133, 83), "%s", state.message.c_str());
                ImGui::PopTextWrapPos();
            }
            if (ImGui::Button("取消", ImVec2(100 * scale, 0))) {
                features::CancelHotkeyCapture(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("清除绑定", ImVec2(100 * scale, 0))) {
                features::SetHotkey(id, 0, features::NoModifier);
                features::CancelHotkeyCapture(); ImGui::CloseCurrentPopup();
            }
        } else { features::CancelHotkeyCapture(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
void FeaturePage(float scale) {
    static ImGuiTextFilter search;
    static int category = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * scale, 6 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 12 * scale));
    const auto headingY = ImGui::GetCursorPosY();
    const auto right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    Label("修改功能", 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::SameLine(0, 16 * scale);
    ImGui::TextDisabled("共 %zu 项", features::Count);
    ImGui::SetCursorPos(ImVec2(right - 268 * scale, headingY));
    ImGui::SetNextItemWidth(268 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34 * scale, 10 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, settings.light ? Color(248, 249, 251) : Color(30, 33, 39));
    if (ImGui::InputTextWithHint("##feature-search", "搜索功能或快捷键", search.InputBuf, sizeof(search.InputBuf))) search.Build();
    const auto searchOrigin = ImGui::GetItemRectMin();
    const auto searchEnd = ImGui::GetItemRectMax();
    const ImVec2 searchCenter(searchOrigin.x + 17 * scale, (searchOrigin.y + searchEnd.y) * .5f - scale);
    auto* searchDraw = ImGui::GetWindowDrawList();
    const auto searchColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    searchDraw->AddCircle(searchCenter, 5 * scale, searchColor, 0, 1.5f * scale);
    searchDraw->AddLine(ImVec2(searchCenter.x + 3.5f * scale, searchCenter.y + 3.5f * scale),
        ImVec2(searchCenter.x + 7.5f * scale, searchCenter.y + 7.5f * scale), searchColor, 1.5f * scale);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorPosY(headingY + 52 * scale);
    const auto categoryStart = ImGui::GetCursorScreenPos();
    const float categoryWidth = ImGui::GetContentRegionAvail().x / (static_cast<int>(features::Group::Count) + 1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    for (int index = -1; index < static_cast<int>(features::Group::Count); ++index) {
        if (index != -1) ImGui::SameLine(0, 0);
        const bool selected = category == index;
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button(index == -1 ? "全部" : features::GroupName(static_cast<features::Group>(index)),
            ImVec2(categoryWidth, 36 * scale))) category = index;
        ImGui::PopStyleColor();
        if (selected) {
            const auto start = ImGui::GetItemRectMin();
            const auto end = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + 14 * scale, end.y - scale),
                ImVec2(end.x - 14 * scale, end.y - scale), ImGui::GetColorU32(accent), 2 * scale);
        }
    }
    ImGui::GetWindowDrawList()->AddLine(ImVec2(categoryStart.x, categoryStart.y + 36 * scale),
        ImVec2(categoryStart.x + categoryWidth * (static_cast<int>(features::Group::Count) + 1), categoryStart.y + 36 * scale),
        ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(headingY + 104 * scale);
    std::array<const features::Definition*, features::Count> filtered{};
    int matches{};
    for (const auto& definition : features::Definitions()) {
        if (category >= 0 && definition.group != static_cast<features::Group>(category)) continue;
        const auto searchable = std::format("{} {} {}", definition.name, features::GetHotkey(definition.id).label, definition.description);
        if (search.PassFilter(searchable.c_str())) filtered[matches++] = &definition;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14 * scale, 8 * scale));
    constexpr auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("feature-list", 4, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("功能", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("快捷键", ImGuiTableColumnFlags_WidthFixed, 136 * scale);
        ImGui::TableSetupColumn("参数", ImGuiTableColumnFlags_WidthFixed, 170 * scale);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 100 * scale);
        ListHeaders(scale);
        ImGuiListClipper clipper;
        clipper.Begin(matches, ListRowHeight(scale));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                FeatureRow(*filtered[index], scale);
        }
        if (!matches) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("没有匹配的功能");
        }
        ImGui::EndTable();
    }
    HotkeyPopup(scale);
    ImGui::PopStyleVar(3);
}
void RecordExtensionResult(const features::Result& result) {
    extensionNotice = {result.message, GetTickCount64(), !result.success};
}
void ExtensionHeading(const char* title, const ext::Status& status, float scale) {
    Label(title, 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::SameLine(0, 16 * scale);
    ImGui::TextColored(status.ready ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        "%s", status.pending ? "处理中" : status.ready ? "已连接" : "等待游戏");
    const bool recent = GetTickCount64() - extensionNotice.shownAt < 6000;
    const bool failed = recent && extensionNotice.failed;
    const auto& message = failed || (status.message.empty() && recent) ? extensionNotice.message : status.message;
    ImGui::PushFont(nullptr, 13);
    ImGui::TextColored(failed ? Color(214, 133, 83) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        "%s", message.empty() ? (status.pending ? "正在处理请求" : "选择目标后操作") : message.c_str());
    if (ImGui::IsItemHovered() && !message.empty()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480 * scale);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopFont();
}
void ItemTooltip(const ext::ItemEntry& item, float scale) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * scale);
    ImGui::Text("%s  ·  %d", item.name, item.id);
    if (item.description && *item.description) ImGui::TextWrapped("%s", item.description);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void ItemsPage(float scale) {
    static ImGuiTextFilter search;
    static int category{}, count = 1;
    static const ext::ItemEntry* cachedData{};
    static std::size_t cachedSize{}, removeIndex = std::numeric_limits<std::size_t>::max();
    static int removeCount{};
    static std::size_t cardIndex=std::numeric_limits<std::size_t>::max();
    static int storyStage{},portraitStage{1};
    static std::vector<const char*> categories;
    static std::vector<std::size_t> filtered;
    const auto items = ext::Items();
    const auto status = ext::GetStatus();
    bool rebuild = items.data() != cachedData || items.size() != cachedSize;
    if (rebuild) {
        cachedData = items.data(); cachedSize = items.size(); category = 0;
        categories.clear();
        for (const auto& item : items) {
            if (std::none_of(categories.begin(), categories.end(), [&](const char* value) {
                return std::string_view(value) == item.category;
            })) categories.push_back(item.category);
        }
        removeIndex = std::numeric_limits<std::size_t>::max();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * scale, 6 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 12 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14 * scale, 8 * scale));
    ExtensionHeading("物品", status, scale);
    ImGui::SetNextItemWidth(300 * scale);
    if (ImGui::InputTextWithHint("##item-search", "搜索物品或编号", search.InputBuf, sizeof(search.InputBuf))) {
        search.Build(); rebuild = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170 * scale);
    if (ImGui::BeginCombo("##item-category", category == 0 ? "全部分类" : categories[category - 1])) {
        for (int index = 0; index <= static_cast<int>(categories.size()); ++index) {
            if (ImGui::Selectable(index == 0 ? "全部分类" : categories[index - 1], category == index)) {
                category = index; rebuild = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("数量");
    ImGui::SameLine(); ImGui::SetNextItemWidth(90 * scale);
    if (ImGui::InputInt("##item-count", &count, 0, 0)) count = std::max(1, count);
    if (rebuild) {
        filtered.clear();
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            if (category && std::string_view(item.category) != categories[category - 1]) continue;
            const auto searchable = std::format("{} {} {} {}", item.name, item.category, item.id, item.description ? item.description : "");
            if (search.PassFilter(searchable.c_str())) filtered.push_back(index);
        }
    }
    bool openRemove{},openCard{};
    const auto listHeight = std::max(60 * scale, ImGui::GetContentRegionAvail().y - 36 * scale);
    if (ImGui::BeginTable("items-list", 4, compactTableFlags, ImVec2(0, listHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("物品", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("分类", ImGuiTableColumnFlags_WidthFixed, 88 * scale);
        ImGui::TableSetupColumn("编号", ImGuiTableColumnFlags_WidthFixed, 80 * scale);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 116 * scale);
        ListHeaders(scale);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()), ListRowHeight(scale));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto index = filtered[row];
                const auto& item = items[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
                ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                ColumnText(item.name, scale); ItemTooltip(item, scale);
                ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", item.category);
                ImGui::TableSetColumnIndex(2); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%d", item.id);
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(!status.ready || status.pending);
                if (ImGui::Button(item.list==16?"设置":"添加", ImVec2(50 * scale, 0))) {
                    if(item.list==16){cardIndex=index;openCard=true;}
                    else RecordExtensionResult(ext::ChangeItem(index, count, false));
                }
                ImGui::SameLine(0, 6 * scale);
                ImGui::BeginDisabled(item.list==13||item.list==15||item.list==16);
                if (ImGui::Button("删除", ImVec2(50 * scale, 0))) {
                    removeIndex = index; removeCount = count; openRemove = true;
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        if (filtered.empty()) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("没有匹配的物品");
        }
        ImGui::EndTable();
    }
    ImGui::PushFont(nullptr, 13);
    ImGui::TextDisabled("添加和删除会写入背包");
    ImGui::PopFont();
    if (openRemove) ImGui::OpenPopup("确认删除物品");
    if (openCard) ImGui::OpenPopup("设置图鉴阶段");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * scale, 18 * scale));
    if (ImGui::BeginPopupModal("确认删除物品", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (removeIndex < items.size()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400 * scale);
            ImGui::Text("确认删除 %s × %d？", items[removeIndex].name, removeCount);
            ImGui::PopTextWrapPos();
            ImGui::TextDisabled("此操作会永久改变背包。");
            ImGui::Spacing();
            if (ImGui::Button("取消", ImVec2(100 * scale, 0))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::BeginDisabled(!status.ready || status.pending);
            if (ImGui::Button("确认删除", ImVec2(100 * scale, 0))) {
                RecordExtensionResult(ext::ChangeItem(removeIndex, removeCount, true));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        } else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("设置图鉴阶段",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)) {
        if(cardIndex<items.size()) {
            ImGui::TextUnformatted(items[cardIndex].name);
            ImGui::SetNextItemWidth(200*scale);ImGui::Combo("故事阶段",&storyStage,"初见\0故事一\0故事二\0故事三\0故事四\0故事五\0");
            ImGui::SetNextItemWidth(200*scale);ImGui::Combo("画像阶段",&portraitStage,"初见\0完整\0");
            ImGui::TextDisabled("将覆盖此条目的阶段。请选择它实际包含的故事阶段。");
            if(ImGui::Button("取消",ImVec2(100*scale,0)))ImGui::CloseCurrentPopup();
            ImGui::SameLine();ImGui::BeginDisabled(!status.ready||status.pending);
            if(ImGui::Button("应用",ImVec2(100*scale,0))){RecordExtensionResult(ext::SetCard(cardIndex,storyStage,portraitStage));ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();
        }else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
}
bool CoordinatesFinite(const ext::Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
void CoordinateToolbar(const ext::Status& status, float scale) {
    if (travelEditor.awaitingPosition && !status.pending) {
        if (status.positionValid) { travelEditor.position = status.position; travelEditor.hasCoordinates = true; }
        travelEditor.awaitingPosition = false;
    } else if (!travelEditor.hasCoordinates && status.positionValid) {
        travelEditor.position = status.position; travelEditor.hasCoordinates = true;
    }
    constexpr const char* axes[] = {"X", "Y", "Z"};
    double* values[] = {&travelEditor.position.x, &travelEditor.position.y, &travelEditor.position.z};
    ImGui::BeginDisabled(travelEditor.awaitingPosition);
    for (int index = 0; index < 3; ++index) {
        if (index) ImGui::SameLine();
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(axes[index]); ImGui::SameLine();
        ImGui::PushID(index); ImGui::SetNextItemWidth(118 * scale);
        if (ImGui::InputDouble("##coordinate", values[index], 0, 0, "%.2f")) travelEditor.hasCoordinates = true;
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!status.ready || status.pending);
    if (ImGui::Button("刷新坐标", ImVec2(84 * scale, 0))) {
        const auto result = ext::RefreshPosition();
        RecordExtensionResult(result);
        if (result.success) { travelEditor.awaitingPosition = true; travelEditor.hasCoordinates = false; }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!status.ready || status.pending || !travelEditor.hasCoordinates || !CoordinatesFinite(travelEditor.position));
    if (ImGui::Button("瞬移", ImVec2(56 * scale, 0))) RecordExtensionResult(ext::Teleport(travelEditor.position));
    ImGui::EndDisabled();
}
bool LandmarkGroupHeader(const char* name, std::size_t count, bool expanded, float scale) {
    const auto height = std::max(40 * scale, ImGui::GetTextLineHeight() + 16 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    if (ImGui::Button("##landmark-group", ImVec2(ImGui::GetContentRegionAvail().x, height))) expanded = !expanded;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    const auto centerY = (minimum.y + maximum.y) * .5f;
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    const auto point = [&](float x, float y) { return ImVec2(minimum.x + x * scale, centerY + y * scale); };
    if (expanded) {
        draw->AddLine(point(14, -2), point(19, 3), color, 1.5f * scale);
        draw->AddLine(point(19, 3), point(24, -2), color, 1.5f * scale);
    } else {
        draw->AddLine(point(17, -5), point(22, 0), color, 1.5f * scale);
        draw->AddLine(point(22, 0), point(17, 5), color, 1.5f * scale);
    }
    const auto countText = std::format("{} 个地标", count);
    const auto countSize = ImGui::CalcTextSize(countText.c_str());
    const auto countX = maximum.x - 14 * scale - countSize.x;
    const auto textY = centerY - ImGui::GetTextLineHeight() * .5f;
    const auto textX = minimum.x + 38 * scale;
    draw->PushClipRect(ImVec2(textX, minimum.y), ImVec2(std::max(textX, countX - 12 * scale), maximum.y), true);
    draw->AddText(ImVec2(textX, textY), color, name);
    draw->PopClipRect();
    draw->AddText(ImVec2(countX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), countText.c_str());
    return expanded;
}
void LandmarksPage(const ext::Status& status, float scale) {
    struct SceneState {
        int id;
        bool expanded{}, searchExpanded{};
    };
    struct Group {
        std::size_t state;
        const char* name;
        std::vector<std::size_t> landmarks;
    };
    static ImGuiTextFilter search;
    static int selectedScene = -1;
    static bool wasSearching{};
    static std::vector<SceneState> sceneStates;
    const auto scenes = ext::Scenes();
    const auto landmarks = ext::Landmarks();
    for (const auto& scene : scenes)
        if (std::none_of(sceneStates.begin(), sceneStates.end(), [&](const auto& state) { return state.id == scene.id; }))
            sceneStates.push_back({scene.id});
    if (selectedScene >= static_cast<int>(scenes.size())) selectedScene = -1;
    const auto previousScene = selectedScene;
    ImGui::SetNextItemWidth(300 * scale);
    const bool searchChanged = ImGui::InputTextWithHint("##landmark-search", "搜索场景或地标", search.InputBuf, sizeof(search.InputBuf));
    if (searchChanged) search.Build();
    ImGui::SameLine(); ImGui::SetNextItemWidth(190 * scale);
    if (ImGui::BeginCombo("##scene", selectedScene < 0 ? "全部场景" : scenes[selectedScene].name)) {
        if (ImGui::Selectable("全部场景", selectedScene < 0)) selectedScene = -1;
        for (int index = 0; index < static_cast<int>(scenes.size()); ++index) {
            ImGui::PushID(index);
            if (ImGui::Selectable(scenes[index].name, selectedScene == index)) selectedScene = index;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!status.ready || status.pending || selectedScene < 0);
    if (ImGui::Button("进入场景", ImVec2(88 * scale, 0)))
        RecordExtensionResult(ext::TravelDefault(scenes[selectedScene].id));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("进入所选场景的默认出生点。");
    const bool searching = search.IsActive();
    const bool sceneChanged = selectedScene != previousScene;
    // Keep the normal accordion layout intact while a search has its own temporary layout.
    if (searching && !wasSearching)
        for (auto& state : sceneStates) state.searchExpanded = state.expanded;
    wasSearching = searching;
    std::vector<Group> groups;
    std::size_t matchingLandmarks{};
    for (const auto& scene : scenes) {
        if (selectedScene >= 0 && scene.id != scenes[selectedScene].id) continue;
        const auto state = std::find_if(sceneStates.begin(), sceneStates.end(), [&](const auto& entry) { return entry.id == scene.id; });
        Group group{static_cast<std::size_t>(state - sceneStates.begin()), scene.name, {}};
        for (std::size_t index = 0; index < landmarks.size(); ++index) {
            const auto& landmark = landmarks[index];
            if (landmark.scene != scene.id) continue;
            const auto searchable = std::format("{} {}", scene.name, landmark.name);
            if (search.PassFilter(searchable.c_str())) group.landmarks.push_back(index);
        }
        if (group.landmarks.empty()) continue;
        auto& expanded = searching ? state->searchExpanded : state->expanded;
        if ((searchChanged && searching) || (sceneChanged && selectedScene >= 0)) expanded = true;
        matchingLandmarks += group.landmarks.size();
        groups.push_back(std::move(group));
    }
    ImGui::PushFont(nullptr, 13);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * scale, 4 * scale));
    const auto controlsWidth = ImGui::CalcTextSize("全部展开").x + ImGui::CalcTextSize("全部折叠").x
        + ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x;
    const auto controlsX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - controlsWidth;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu 个场景 · %zu 个地标", groups.size(), matchingLandmarks);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlsX));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::BeginDisabled(groups.empty());
    const bool expandAll = ImGui::Button("全部展开##landmarks");
    ImGui::SameLine();
    const bool collapseAll = ImGui::Button("全部折叠##landmarks");
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (expandAll || collapseAll)
        for (const auto& group : groups) {
            auto& state = sceneStates[group.state];
            (searching ? state.searchExpanded : state.expanded) = expandAll;
        }
    const auto height = std::max(60 * scale, ImGui::GetContentRegionAvail().y - 28 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6 * scale));
    if (ImGui::BeginChild("landmark-groups", ImVec2(0, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
        if (searchChanged || sceneChanged) ImGui::SetScrollY(0);
        if (groups.empty()) {
            ImGui::SetCursorPos(ImVec2(14 * scale, 12 * scale));
            ImGui::TextDisabled("没有匹配的地标");
        }
        for (const auto& group : groups) {
            auto& state = sceneStates[group.state];
            auto& expanded = searching ? state.searchExpanded : state.expanded;
            ImGui::PushID(state.id);
            expanded = LandmarkGroupHeader(group.name, group.landmarks.size(), expanded, scale);
            if (expanded) {
                constexpr auto rowFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                    | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
                if (ImGui::BeginTable("landmark-rows", 2, rowFlags)) {
                    ImGui::TableSetupColumn("地标", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 72 * scale);
                    for (const auto index : group.landmarks) {
                        const auto& landmark = landmarks[index];
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
                        ImGui::TableSetColumnIndex(0);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16 * scale);
                        ImGui::AlignTextToFramePadding();
                        ColumnText(landmark.name, scale);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("X %.2f  Y %.2f  Z %.2f", landmark.position.x, landmark.position.y, landmark.position.z);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginDisabled(!status.ready || status.pending);
                        if (ImGui::Button("前往", ImVec2(56 * scale, 0))) RecordExtensionResult(ext::Travel(landmark.scene, landmark.position));
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PushFont(nullptr, 13); ImGui::TextDisabled("跨场景移动可能影响当前进度"); ImGui::PopFont();
}
void BookmarksPage(const ext::Status& status, float scale) {
    ImGui::SetNextItemWidth(300 * scale);
    ImGui::InputTextWithHint("##bookmark-name", "书签名称", travelEditor.bookmarkName, sizeof(travelEditor.bookmarkName));
    ImGui::SameLine();
    ImGui::BeginDisabled(status.pending || !travelEditor.hasCoordinates || !CoordinatesFinite(travelEditor.position) || !*travelEditor.bookmarkName);
    if (ImGui::Button("记录坐标", ImVec2(96 * scale, 0)))
        RecordExtensionResult(ext::SaveBookmark(travelEditor.bookmarkName, travelEditor.position));
    ImGui::EndDisabled();
    const auto bookmarks = ext::Bookmarks();
    const auto height = std::max(60 * scale, ImGui::GetContentRegionAvail().y - 24 * scale);
    if (ImGui::BeginTable("bookmarks-list", 3, compactTableFlags, ImVec2(0, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("书签", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("坐标", ImGuiTableColumnFlags_WidthFixed, 238 * scale);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 116 * scale);
        ListHeaders(scale);
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(bookmarks.size()), ListRowHeight(scale));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& bookmark = bookmarks[index];
                ImGui::PushID(index); ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
                ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ColumnText(bookmark.name.c_str(), scale);
                ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::PushFont(nullptr, 13);
                ImGui::TextDisabled("%.1f, %.1f, %.1f", bookmark.position.x, bookmark.position.y, bookmark.position.z); ImGui::PopFont();
                ImGui::TableSetColumnIndex(2); ImGui::BeginDisabled(!status.ready || status.pending);
                if (ImGui::Button("跳转", ImVec2(50 * scale, 0))) RecordExtensionResult(ext::Teleport(bookmark.position));
                ImGui::EndDisabled(); ImGui::SameLine(0, 6 * scale); ImGui::BeginDisabled(status.pending);
                if (ImGui::Button("删除", ImVec2(50 * scale, 0))) RecordExtensionResult(ext::RemoveBookmark(index));
                ImGui::EndDisabled(); ImGui::PopID();
            }
        }
        if (bookmarks.empty()) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("还没有书签"); }
        ImGui::EndTable();
    }
    ImGui::PushFont(nullptr, 13); ImGui::TextDisabled("记录上方坐标；书签跳转不会切换场景"); ImGui::PopFont();
}
void ObjectsPage(const ext::Status& status, float scale) {
    static ImGuiTextFilter search;
    ImGui::BeginDisabled(!status.ready || status.pending);
    if (ImGui::Button("刷新对象", ImVec2(96 * scale, 0))) RecordExtensionResult(ext::RefreshObjects());
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::SetNextItemWidth(300 * scale);
    if (ImGui::InputTextWithHint("##object-search", "搜索对象", search.InputBuf, sizeof(search.InputBuf))) search.Build();
    std::vector<std::size_t> filtered;
    for (std::size_t index = 0; index < status.objects.size(); ++index)
        if (search.PassFilter(status.objects[index].name.c_str())) filtered.push_back(index);
    if (ImGui::BeginTable("objects-list", 3, compactTableFlags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("对象", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("坐标", ImGuiTableColumnFlags_WidthFixed, 238 * scale);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 64 * scale);
        ListHeaders(scale);
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(filtered.size()), ListRowHeight(scale));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto index = filtered[row]; const auto& object = status.objects[index];
                ImGui::PushID(static_cast<int>(index)); ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowHeight(scale));
                ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ColumnText(object.name.c_str(), scale);
                ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::PushFont(nullptr, 13);
                ImGui::TextDisabled("%.1f, %.1f, %.1f", object.position.x, object.position.y, object.position.z); ImGui::PopFont();
                ImGui::TableSetColumnIndex(2); ImGui::BeginDisabled(!status.ready || status.pending);
                if (ImGui::Button("定位", ImVec2(56 * scale, 0))) RecordExtensionResult(ext::TeleportToObject(object.address));
                ImGui::EndDisabled(); ImGui::PopID();
            }
        }
        if (filtered.empty()) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("点击刷新对象以获取目标"); }
        ImGui::EndTable();
    }
}
void TravelPage(float scale) {
    const auto status = ext::GetStatus();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * scale, 6 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 12 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14 * scale, 8 * scale));
    ExtensionHeading("传送", status, scale);
    CoordinateToolbar(status, scale);
    if (ImGui::BeginTabBar("travel-tabs")) {
        if (ImGui::BeginTabItem("坐标与地标")) { LandmarksPage(status, scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("书签")) { BookmarksPage(status, scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("场景对象")) { ObjectsPage(status, scale); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(3);
}
void SettingsPage(float scale) {
    BeginCard("appearance-settings", ImVec2(0, 160 * scale), scale);
    Label("外观", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("主题");
    ImGui::SameLine(112 * scale);
    if (ImGui::RadioButton("暗色", !settings.light)) { settings.light = false; dirty = true; }
    ImGui::SameLine(0, 36 * scale);
    if (ImGui::RadioButton("亮色", settings.light)) { settings.light = true; dirty = true; }
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("界面缩放");
    ImGui::SameLine(112 * scale);
    ImGui::SetNextItemWidth(std::min(300 * scale, ImGui::GetContentRegionAvail().x));
    if (ImGui::SliderFloat("##scale", &settings.scale, .8f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
        dirty = true; forcePosition = true;
    }
    EndCard();

    BeginCard("hotkey-settings", ImVec2(0, 220 * scale), scale);
    Label("快捷键", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("界面开关按键");
    ImGui::SameLine(144 * scale);
    const auto menuKey = features::GetMenuHotkey();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    const float keyWidth = std::max(80 * scale, ImGui::CalcTextSize(menuKey.label.c_str()).x + 20 * scale);
    const auto keycap = menuKey.label + "##settings-menu-key";
    ImGui::Button(keycap.c_str(), ImVec2(keyWidth, 0));
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
    ImGui::SameLine();
    if (ImGui::Button("修改", ImVec2(64 * scale, 0))) {
        features::BeginMenuHotkeyCapture();
        openMenuHotkeyCapture = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("恢复默认", ImVec2(88 * scale, 0)))
        features::SetMenuHotkey(VK_INSERT, features::NoModifier);
    const auto result = features::GetMenuHotkeyResult();
    if (!result.success && !result.message.empty())
        ImGui::TextColored(Color(214, 133, 83), "%s", result.message.c_str());
    else ImGui::TextDisabled("点击修改后按下组合键，Esc 取消。");
    ImGui::Spacing();
    bool hotkeys = features::HotkeysEnabled();
    if (ImGui::Checkbox("启用功能快捷键", &hotkeys)) features::SetHotkeysEnabled(hotkeys);
    ImGui::TextDisabled("Ctrl+Shift+Home 切换功能快捷键。");
    EndCard();
    ImGui::TextDisabled("设置自动保存，功能开关不会自动恢复。");
    ImGui::Dummy(ImVec2(0, 8 * scale));
    BeginCard("update-settings", ImVec2(0, 144 * scale), scale);
    Label("版本更新", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Spacing();
    const auto updateState = update::GetSnapshot();
    ImGui::BeginDisabled(updateState.status == update::Status::Checking);
    if (ImGui::Button("检查更新", ImVec2(112 * scale, 0))) update::CheckAsync();
    ImGui::EndDisabled();
    ImGui::SameLine(0, 16 * scale);
    ImGui::TextDisabled("%s", updateState.message.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("当前版本 v%s", Version);
    EndCard();

    if (openMenuHotkeyCapture) {
        ImGui::OpenPopup("设置界面开关按键");
        openMenuHotkeyCapture = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 20 * scale));
    if (ImGui::BeginPopupModal("设置界面开关按键", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!features::CapturingMenuHotkey()) ImGui::CloseCurrentPopup();
        ImGui::TextColored(accent, "按下要使用的按键或组合键…");
        ImGui::TextDisabled("支持 Ctrl / Shift / Alt，Esc 取消。");
        ImGui::TextDisabled("界面开关按键始终可用，不能留空。");
        const auto captureResult = features::GetMenuHotkeyResult();
        if (!captureResult.success && !captureResult.message.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * scale);
            ImGui::TextColored(Color(214, 133, 83), "%s", captureResult.message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        if (ImGui::Button("取消", ImVec2(100 * scale, 0))) {
            features::CancelHotkeyCapture(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
void UpdateRequiredPage(float scale) {
    // A modal opened before the asynchronous result must not leave an invisible
    // input blocker above this page, or keep recording the menu shortcut.
    if (features::AnyHotkeyCapture()) features::CancelHotkeyCapture();
    openHotkeyCapture = openMenuHotkeyCapture = false;
    for (const auto* name : {"设置快捷键", "设置界面开关按键", "确认删除物品", "设置图鉴阶段"}) {
        if (ImGui::IsPopupOpen(name) && ImGui::BeginPopupModal(name)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    const auto state = update::GetSnapshot();
    ImGui::Dummy(ImVec2(0, 12 * scale));
    BeginCard("required-update", ImVec2(0, 456 * scale), scale);
    ImGui::Dummy(ImVec2(0, 12 * scale));
    Label("发现新版本", 28, accent);
    ImGui::Dummy(ImVec2(0, 8 * scale));
    ImGui::TextUnformatted("请更新后继续使用修改功能");
    ImGui::Dummy(ImVec2(0, 18 * scale));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 14 * scale));
    ImGui::TextDisabled("当前版本"); ImGui::SameLine(144 * scale);
    ImGui::Text("v%s", Version);
    ImGui::TextDisabled("最新版本"); ImGui::SameLine(144 * scale);
    ImGui::TextColored(accent, "%s", state.latestVersion.c_str());
    ImGui::Dummy(ImVec2(0, 20 * scale));
    ImGui::PushStyleColor(ImGuiCol_Button, settings.light ? Color(140, 101, 40) : Color(147, 112, 55));
    ImGui::PushStyleColor(ImGuiCol_Text, Color(255, 255, 255));
    if (ImGui::Button("前往 GitHub 下载", ImVec2(220 * scale, 40 * scale))) update::OpenReleasePage();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
    if (ImGui::Button("重新检查", ImVec2(132 * scale, 36 * scale))) update::CheckAsync();
    ImGui::SameLine(0, 12 * scale);
    if (ImGui::Button("关闭菜单", ImVec2(132 * scale, 36 * scale))) SetVisible(false);
    ImGui::Dummy(ImVec2(0, 12 * scale));
    ImGui::TextDisabled("请正常退出游戏后手动安装新版。");
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
    if (!show) { features::CancelHotkeyCapture(); SaveSettings(); }
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
        const auto boldFont = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyhbd.ttc";
        if (std::filesystem::exists(boldFont)) {
            const auto boldUtf8 = boldFont.u8string();
            headingFont = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(boldUtf8.c_str()), 24);
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
    ImGui::GetPlatformIO().Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
        const auto target = static_cast<HWND>(viewport->PlatformHandleRaw);
        if (!target) return;
        // The render thread only copies ImGui data. IMM context association and
        // candidate/composition positioning run through the window's messages.
        input::ime::TextInput request;
        request.wantTextInput = data->WantTextInput;
        request.wantVisible = data->WantVisible;
        request.position = {static_cast<LONG>(data->InputPos.x - viewport->Pos.x),
            static_cast<LONG>(data->InputPos.y - viewport->Pos.y)};
        request.lineHeight = static_cast<LONG>(data->InputLineHeight);
        input::ime::Request(target, request);
    };
    Log("ImGui initialized with Simplified Chinese UI");
    static std::atomic_flag updateStarted = ATOMIC_FLAG_INIT;
    if (!updateStarted.test_and_set()) update::CheckAsync();
    return true;
}
void DrawFrame(const char* renderer) {
    if (!currentWindow) return;
    const bool updateRequired = update::Required();
    features::SetUpdateRequired(updateRequired);
    auto& io = ImGui::GetIO();
    io.MouseDrawCursor = Visible();
    if (Visible()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    const auto dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(currentWindow);
    RECT client{}; GetClientRect(currentWindow, &client);
    const auto fit = std::min((client.right - client.left) / 1000.f, (client.bottom - client.top) / 720.f);
    const float scale = std::max(.5f, std::min(dpi * settings.scale, fit));
    if (previousScale != scale || previousLight != settings.light) {
        if (previousScale != scale) forcePosition = true;
        ApplyStyle(scale); previousScale = scale; previousLight = settings.light;
    }
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (Visible()) {
        const ImVec2 size(960 * scale, 680 * scale);
        ImGui::SetNextWindowSize(size);
        if (forcePosition) {
            ImVec2 position(settings.x, settings.y);
            if (position.x < 0 || position.y < 0) position = ImVec2((io.DisplaySize.x - size.x) * .5f, (io.DisplaySize.y - size.y) * .5f);
            position.x = std::clamp(position.x, 0.f, std::max(0.f, io.DisplaySize.x - size.x));
            position.y = std::clamp(position.y, 0.f, std::max(0.f, io.DisplaySize.y - size.y));
            ImGui::SetNextWindowPos(position);
            forcePosition = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * scale);
        ImGui::Begin("GlossTrainer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        const auto origin = ImGui::GetWindowPos();
        if (settings.x != origin.x || settings.y != origin.y) { settings.x = origin.x; settings.y = origin.y; dirty = true; }
        auto* draw = ImGui::GetWindowDrawList();
        const auto border = ImGui::GetColorU32(ImGuiCol_Border);
        draw->AddLine(ImVec2(origin.x, origin.y + 64 * scale), ImVec2(origin.x + size.x, origin.y + 64 * scale), border);
        draw->AddLine(ImVec2(origin.x, origin.y + 632 * scale), ImVec2(origin.x + size.x, origin.y + 632 * scale), border);
        draw->AddLine(ImVec2(origin.x + 160 * scale, origin.y + 64 * scale), ImVec2(origin.x + 160 * scale, origin.y + 632 * scale), border);
        ImGui::SetCursorPos(ImVec2(24 * scale, 18 * scale));
        Label("Gloss", 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::SameLine(0, 0); Label("Trainer", 24, accent);
        ImGui::SameLine(0, 20 * scale); ImGui::TextDisabled("|");
        ImGui::SameLine(0, 20 * scale); ImGui::TextUnformatted("黑神话：悟空");
        ImGui::SameLine(0, 24 * scale); ImGui::TextDisabled("v%s", Version);
        ImGui::SameLine(0, 24 * scale); ImGui::TextDisabled("By: 小莫");
        ImGui::SetCursorPos(ImVec2(size.x - 48 * scale, 16 * scale));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, settings.light ? Color(222, 225, 232) : Color(43, 47, 55));
        const auto closePosition = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##close", ImVec2(32 * scale, 32 * scale))) SetVisible(false);
        ImGui::PopStyleColor(2);
        const auto closeColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddLine(ImVec2(closePosition.x + 10 * scale, closePosition.y + 10 * scale),
            ImVec2(closePosition.x + 22 * scale, closePosition.y + 22 * scale), closeColor, 1.5f * scale);
        draw->AddLine(ImVec2(closePosition.x + 22 * scale, closePosition.y + 10 * scale),
            ImVec2(closePosition.x + 10 * scale, closePosition.y + 22 * scale), closeColor, 1.5f * scale);
        ImGui::SetCursorPos(ImVec2(8 * scale, 88 * scale));
        ImGui::BeginGroup();
        constexpr const char* pages[] = {"概览", "修改功能", "物品", "传送", "设置"};
        ImGui::BeginDisabled(updateRequired);
        for (int index = 0; index < static_cast<int>(std::size(pages)); ++index) {
            ImGui::PushID(index);
            const bool selected = index == page;
            if (NavigationItem(pages[index], index, selected, scale)) page = index;
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::SetCursorPos(ImVec2(184 * scale, 88 * scale));
        ImGui::BeginChild("content", ImVec2(752 * scale, 520 * scale), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        if (!updateRequired && (page == 0 || page == 4)) {
            Label(pages[page], 28, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TextDisabled(page == 0 ? "游戏内工具面板" : "让界面更合你的习惯");
            ImGui::Dummy(ImVec2(0, 10 * scale));
        }
        if (updateRequired) UpdateRequiredPage(scale);
        else if (page == 0) Overview(renderer, scale);
        else if (page == 1) FeaturePage(scale);
        else if (page == 2) ItemsPage(scale);
        else if (page == 3) TravelPage(scale);
        else SettingsPage(scale);
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(24 * scale, 643 * scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9 * scale, 4 * scale));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
        const auto menuKeyLabel = features::GetMenuHotkey().label;
        const auto menuKeycap = menuKeyLabel + "##menu-keycap";
        ImGui::Button(menuKeycap.c_str(), ImVec2(std::max(56 * scale,
            ImGui::CalcTextSize(menuKeyLabel.c_str()).x + 18 * scale), 26 * scale));
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 12 * scale); ImGui::TextDisabled("显示或隐藏菜单");
        ImGui::SetCursorPos(ImVec2(size.x - 132 * scale, 649 * scale));
        const bool hotkeysEnabled = features::HotkeysEnabled() && !updateRequired;
        const auto statusPosition = ImGui::GetCursorScreenPos();
        draw->AddCircleFilled(ImVec2(statusPosition.x + 4 * scale, statusPosition.y + 8 * scale),
            3 * scale, ImGui::GetColorU32(hotkeysEnabled ? Color(99, 185, 75) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16 * scale);
        ImGui::TextDisabled("%s", updateRequired ? "请更新后使用" : hotkeysEnabled ? "快捷键可用" : "快捷键已禁用");
        if (ImGui::IsItemHovered() && !updateRequired) ImGui::SetTooltip("Ctrl+Shift+Home 切换功能快捷键");
        ImGui::End();
    } else if (GetTickCount64() - initializedAt < 6500) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, 36 * scale), ImGuiCond_Always, ImVec2(.5f, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * scale, 12 * scale));
        ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(accent, "GlossTrainer"); ImGui::SameLine();
        ImGui::Text("已加载  |  %s 打开菜单", features::GetMenuHotkey().label.c_str());
        ImGui::End(); ImGui::PopStyleVar();
    }
    ImGui::Render();
    if (dirty && GetTickCount64() - savedAt > 1000 && !ImGui::IsMouseDown(0)) SaveSettings();
}
}
