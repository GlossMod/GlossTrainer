#include "model.hpp"
#include "runtime.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace gloss::dndg {
void Log(const char*) noexcept {}
}

namespace {
using namespace gloss::dndg;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

std::string Frame() {
    auto& io = ImGui::GetIO();
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    ImGui::LogToBuffer(0);
    DrawNativeOverlay(nullptr);
    const std::string text(ImGui::GetCurrentContext()->LogBuffer.c_str());
    ImGui::LogFinish();
    ImGui::Render();
    // Satisfy the renderer-textures protocol without a GPU. This fixture
    // checks widgets and text, not raster quality or hardware submission.
    if (auto* textures = ImGui::GetDrawData()->Textures) {
        for (auto* texture : *textures) {
            if (texture->Status == ImTextureStatus_WantCreate) {
                texture->SetTexID(static_cast<ImTextureID>(texture->UniqueID + 1));
                texture->SetStatus(ImTextureStatus_OK);
            } else if (texture->Status == ImTextureStatus_WantUpdates) {
                texture->SetStatus(ImTextureStatus_OK);
            } else if (texture->Status == ImTextureStatus_WantDestroy) {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
    return text;
}

void CheckPreview(const Snapshot& state, const char* header, const char* effect) {
    PublishSnapshot(state);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(500.f, 500.f);
    Check(Frame().find(header) != std::string::npos, "peek header is rendered with the current locale and card");
    Check(Frame().find(effect) == std::string::npos, "effect is not shown when cursor is away");
    const auto* window = ImGui::FindWindowByName("##next-card");
    Check(window != nullptr, "peek window created");
    io.AddMousePosEvent(window->Pos.x + window->WindowPadding.x + 8.f,
        window->Pos.y + window->WindowPadding.y + 5.f);
    bool showedEffect{};
    for (int frame = 0; frame < 60; ++frame) {
        if (Frame().find(effect) != std::string::npos) {
            showedEffect = true;
            break;
        }
    }
    Check(showedEffect, "hover renders the card's actual effect");
}

void Drag(ImVec2 from, ImVec2 to) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(from.x, from.y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMousePosEvent(to.x, to.y);
    Frame();
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame();
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    Check(argc == 3 && std::filesystem::exists(argv[1]), "supply an isolated fixture INI and 'light' or 'dark'");
    const std::wstring theme(argv[2]);
    Check(theme == L"light" || theme == L"dark", "theme argument");
    const bool darkDefaults = theme == L"dark";
    ImGui::CreateContext();
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetIO().DisplaySize = {1280, 720};
    ConfigureNativeImGui(argv[1]);
    Snapshot state;
    state.locale = Locale::English;
    state.nextCard = Entry{0, "144", "First card", "Discard to earn 3 chips."};
    CheckPreview(state, "Next card: First card", "Discard to earn 3 chips.");
    Check(ReadUiRequest().showNext, "preview preference restored on first frame");
    const float bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x;
    Check(darkDefaults ? bg < .2f : bg > .8f, "theme matches restored preference/default");

    ImGui::GetIO().DisplaySize = {2560, 1440};
    state.locale = Locale::Chinese;
    state.nextCard = Entry{0, "145", "四条", "出牌时：随机选择一个点数。"};
    CheckPreview(state, "下一张: 四条", "出牌时：随机选择一个点数。");
    Check(ImGui::GetDrawData()->DisplaySize.x == 2560.f && ImGui::GetDrawData()->DisplaySize.y == 1440.f,
        "full-size draw data follows the output dimensions");
    state.nextCard->concealed = true;
    PublishSnapshot(state);
    const auto hidden = Frame();
    Check(hidden.find("四条") == std::string::npos && hidden.find("出牌时") == std::string::npos,
        "concealed preview leaves neither name nor tooltip");

    const auto key = darkDefaults ? VK_INSERT : VK_HOME;
    Check(HandleNativeKey(key, false) && NativeMenuOpen(), "the default or restored custom shortcut opens the panel");
    Check(HandleNativeKey(key, true) && NativeMenuOpen(), "held shortcut does not toggle twice");
    state.nextCard->concealed = false;
    PublishSnapshot(state);
    auto panel = Frame();
    panel += Frame();
    Check(ReadUiRequest().panelOpen && panel.find("显示下一张牌") != std::string::npos,
        "player panel opens in the language of the game");
    Check(panel.find("下一张: 四条") == std::string::npos, "floating preview hidden behind panel");
    const auto* window = ImGui::FindWindowByName("GlossTrainer | DnDG");
    Check(window && window->Size.x < 2560.f * .4f && window->Size.y < 1440.f * .5f,
        "default panel occupies a compact portion of a 1440p viewport");
    const auto pos = window->Pos;
    Drag({pos.x + 125, pos.y + 10}, {pos.x + 45, pos.y + 85});
    Check(window->Pos.x < pos.x - 70.f && window->Pos.y > pos.y + 65.f, "dragging the title moves the panel");
    const auto size = window->Size;
    const ImVec2 grip(window->Pos.x + size.x - 5, window->Pos.y + size.y - 5);
    Drag(grip, {grip.x + 55, grip.y + 70});
    Check(window->Size.x > size.x + 45.f && window->Size.y > size.y + 60.f,
        "dragging the corner resizes both axes");
    Check(HandleNativeKey(VK_ESCAPE, false) && !NativeMenuOpen(), "Escape closes the panel");
    state.nextCard.reset();
    PublishSnapshot(state);
    Check(Frame().find("四条") == std::string::npos && !ReadUiRequest().panelOpen, "empty pile clears preview");

    ImGui::DestroyContext();
    std::cout << "DnDG native overlay fixture passed\n";
}
