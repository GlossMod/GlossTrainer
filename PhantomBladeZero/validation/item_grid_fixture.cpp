// Standalone graphics fixture. Never included in the shipped PBZ target.
#include "../src/runtime.hpp"
#include "../src/gameplay/backend.hpp"
#include "../src/gameplay/item_images.hpp"
#include "../src/gameplay/item_richtext.hpp"
#include "../src/hotkeys.hpp"
#include <MinHook.h>
#include <atomic>
#include <fstream>
#include <memory>
#include <vector>
#include <imgui_internal.h>

// Visual-only fixture: no global input hooks and no focus-dependent automation.
// The production input implementation is excluded only from this target.
namespace gloss::input {
    bool InstallHooks() { return true; }
    bool Attach(HWND) {
        std::ifstream mode(DataDirectory() / L"fixture-mode.txt");
        std::string value;
        mode >> value;
        if (value == "settings" || value == "menu-key" || value == "menu-capture") {
            static bool menuKey{};
            menuKey = value != "settings";
            if (menuKey) {
                std::string error;
                gloss::hotkeys::GetRegistry().SetMenu({VK_F10, 7}, DataDirectory() / L"hotkeys.ini", error);
                if (!error.empty()) Log(error);
                if (value == "menu-capture") gloss::hotkeys::GetRegistry().BeginMenuCapture();
            }
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePre;
            hook.Callback = [](ImGuiContext* context, ImGuiContextHook*) {
                if (!overlay::Visible()) return;
                for (auto* window : context->Windows)
                    if (std::string_view(window->Name).starts_with("GlossTrainer/content_"))
                        ImGui::SetScrollY(window, menuKey ? 220.f : window->ScrollMax.y);
            };
            ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
        }
        if (value == "features") {
            std::string error;
            gloss::hotkeys::GetRegistry().Set("toggle:infinite_dark_source", {VK_F24, 7}, {}, error);
            gloss::hotkeys::GetRegistry().Set("action:set_max_health", {'H', gloss::hotkeys::Control}, {"10000"}, error);
        }
        if (value == "hover" || value == "quantity") {
            static bool quantity{};
            quantity = value == "quantity";
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePre;
            hook.Callback = [](ImGuiContext*, ImGuiContextHook*) {
                if (!overlay::Visible()) return;
                auto& io = ImGui::GetIO();
                io.AddMousePosEvent(520, 250);
                static unsigned visibleFrames{};
                ++visibleFrames;
                if (quantity && visibleFrames == 10) io.AddMouseButtonEvent(0, true);
                if (quantity && visibleFrames == 11) io.AddMouseButtonEvent(0, false);
            };
            ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
        }
        return true;
    }
    void SetMenuVisible(bool) {}
    void Shutdown() {}
}

namespace {
    DWORD WINAPI SupplyImages(void* argument) {
        std::unique_ptr<std::vector<std::uint8_t>> bytes(static_cast<std::vector<std::uint8_t>*>(argument));
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < 30000) {
            if (auto request = gloss::gameplay::TakeItemImageRequest()) {
                gloss::gameplay::ItemImageResult image;
                image.epoch = request->epoch;
                image.key = std::move(request->key);
                image.width = image.height = 512;
                image.format = gloss::gameplay::ItemImageFormat::Bc3;
                image.data = *bytes;
                gloss::gameplay::PublishItemImage(std::move(image));
            } else Sleep(5);
        }
        return 0;
    }
    void PrepareFixture() {
        std::ifstream mode(gloss::DataDirectory() / L"fixture-mode.txt");
        std::string modeName; mode >> modeName;
        if (modeName == "settings" || modeName == "menu-key" || modeName == "menu-capture") return;
        if (modeName == "features") {
            auto& backend = gloss::gameplay::GetBackend();
            backend.SetRuntimeState(true, "仅用于界面测试，不连接游戏", "features-fixture");
            for (const auto& feature : gloss::gameplay::Catalog())
                if (!feature.functionName.empty()) backend.SetCapability(feature.id, true, "界面测试样本");
            std::vector<gloss::gameplay::ToggleState> states;
            for (const auto& toggle : gloss::gameplay::ToggleCatalog())
                states.push_back({toggle.id, true, toggle.id == "infinite_dark_source", "界面测试状态，不连接游戏"});
            backend.PublishToggleStates(std::move(states));
            return;
        }
        auto bytes = std::make_unique<std::vector<std::uint8_t>>(262144);
        std::ifstream input(gloss::DataDirectory() / L"fixture.dxt5", std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(bytes->data()), bytes->size())) {
            gloss::Log("Grid fixture missing its captured BC3 sample");
            return;
        }
        auto& backend = gloss::gameplay::GetBackend();
        backend.SetRuntimeState(true, "仅用于界面测试，不连接游戏", "grid-fixture");
        std::vector<gloss::gameplay::ItemEntry> items;
        for (int i = 0; i < 20; ++i) {
            gloss::gameplay::ItemEntry item;
            item.key = "fixture:" + std::to_string(i);
            item.name = i ? "界面样本 " + std::to_string(i + 1) : "黑伤（图片测试）";
            item.category = "主武器";
            const std::string attack = "<InputAction id='LightAttack'/>";
            item.description = gloss::gameplay::FormatItemRichText("普通连招\n" + attack + attack + attack + attack + attack +
                "\n\n杀气连招\n<InputAction id='HeavyAttack'/><InputAction id='HeavyAttack'/><InputAction id='HeavyAttack'/>\n\n" +
                "示例按键用于验证布局；实机按键从游戏当前绑定读取。此宿主不连接游戏。",
                [](const auto& ref) -> std::optional<std::string> { return ref.id == "LightAttack" ? "鼠标左键" : "F"; }).text;
            item.maxQuantity = 1;
            item.owned = i == 0 ? 1 : 0;
            item.available = i != 0;
            item.iconKey = "/Fixture/Weapon" + std::to_string(i);
            items.push_back(std::move(item));
        }
        backend.PublishItems(std::move(items), true, "20 个界面样本");
        gloss::gameplay::ResetItemImages(backend.GetSnapshot().generation);
        if (const auto thread = CreateThread(nullptr, 0, SupplyImages, bytes.get(), 0, nullptr)) {
            bytes.release();
            CloseHandle(thread);
        }
    }
}

extern "C" __declspec(dllexport) void InitializeASI() {
    static std::atomic_flag initialized = ATOMIC_FLAG_INIT;
    if (initialized.test_and_set()) return;
    const auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return;
    PrepareFixture();
    if (!gloss::input::InstallHooks()) return;
    gloss::graphics::InstallHooks();
}
extern "C" __declspec(dllexport) bool GlossTrainerFixtureMenuVisible() {
    return gloss::overlay::Visible();
}
extern "C" __declspec(dllexport) void GlossTrainerFixtureSetMenuVisible(bool visible) {
    std::lock_guard lock(gloss::OverlayMutex());
    gloss::overlay::SetVisible(visible);
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) gloss::SetModule(module);
    return TRUE;
}
