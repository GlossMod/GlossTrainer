// Standalone graphics fixture. Never included in the shipped PBZ target.
#include "../src/runtime.hpp"
#include "../src/gameplay/backend.hpp"
#include "../src/gameplay/item_images.hpp"
#include "../src/gameplay/item_richtext.hpp"
#include "../src/gameplay/item_quantity.hpp"
#include "../src/hotkeys.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <fstream>
#include <memory>
#include <vector>
#include <imgui_internal.h>

namespace {
    struct LoadingPreviewProbe {
        unsigned visibleFrames{}, pressed{}, released{}, hoveredReleases{};
        unsigned renderedTiles{}, quantityPopupFrames{}, pendingRequests{}, requestHistory{};
        bool backendRejected{}, samplesReadOnly{};
        std::array<ImVec2, 4> targets{};
        std::array<ImRect, 4> tiles{};
        unsigned releasedTileMask{};
        bool targetsReady{};
    } loadingPreviewProbe;

    void RecordLoadingPreview() {
        const auto& probe = loadingPreviewProbe;
        const bool passed = probe.samplesReadOnly && probe.backendRejected && probe.renderedTiles == 4 &&
            probe.pressed == 4 && probe.released == 4 && probe.hoveredReleases == 4 &&
            !probe.quantityPopupFrames && !probe.pendingRequests && !probe.requestHistory;
        std::ofstream result(gloss::DataDirectory() / L"loading-preview-result.json", std::ios::trunc);
        result << "{\n  \"scope\": \"Fixture UI only; no game connection\",\n"
            << "  \"visibleFrames\": " << probe.visibleFrames << ",\n"
            << "  \"samples\": 6,\n  \"namedSamples\": 4,\n  \"unnamedSamples\": 2,\n"
            << "  \"itemsReady\": false,\n  \"allAvailableFalseOwnedUnknown\": " << (probe.samplesReadOnly ? "true" : "false") << ",\n"
            << "  \"renderedTiles\": " << probe.renderedTiles << ",\n"
            << "  \"mousePresses\": " << probe.pressed << ",\n  \"mouseReleases\": " << probe.released << ",\n"
            << "  \"releasesOverTiles\": " << probe.hoveredReleases << ",\n"
            << "  \"quantityPopupFrames\": " << probe.quantityPopupFrames << ",\n"
            << "  \"maxPendingRequests\": " << probe.pendingRequests << ",\n"
            << "  \"maxRequestHistory\": " << probe.requestHistory << ",\n"
            << "  \"backendSubmitRejected\": " << (probe.backendRejected ? "true" : "false") << ",\n"
            << "  \"passed\": " << (passed ? "true" : "false") << "\n}\n";
    }

    void InstallLoadingPreviewProbe() {
        ImGuiContextHook input;
        input.Type = ImGuiContextHookType_NewFramePre;
        input.Callback = [](ImGuiContext*, ImGuiContextHook*) {
            if (!gloss::overlay::Visible()) return;
            auto& probe = loadingPreviewProbe;
            ++probe.visibleFrames;
            if (!probe.targetsReady) return;
            auto& io = ImGui::GetIO();
            const unsigned stage = probe.visibleFrames >= 10 ? (probe.visibleFrames - 10) / 8 : 4;
            if (stage < 4) {
                io.AddMousePosEvent(probe.targets[stage].x, probe.targets[stage].y);
                if (probe.visibleFrames == 10 + stage * 8) { io.AddMouseButtonEvent(0, true); ++probe.pressed; }
                if (probe.visibleFrames == 11 + stage * 8) { io.AddMouseButtonEvent(0, false); ++probe.released; }
            } else if (probe.visibleFrames > 42) io.AddMousePosEvent(1200, 700);
        };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &input);

        ImGuiContextHook inspect;
        inspect.Type = ImGuiContextHookType_EndFramePost;
        inspect.Callback = [](ImGuiContext* context, ImGuiContextHook*) {
            if (!gloss::overlay::Visible()) return;
            auto& probe = loadingPreviewProbe;
            for (auto* window : context->Windows) {
                if (!window->Active) continue;
                const std::string_view name(window->Name);
                if (name.find("item-quantity") != name.npos) ++probe.quantityPopupFrames;
                if (name.find("items-grid") == name.npos || window->IDStack.empty()) continue;
                const auto* table = context->Tables.GetByKey(ImHashStr("item-tiles", 0, window->IDStack.back()));
                if (!table || table->CurrentRow < 0) continue;
                // EndTable resets CurrentColumn. For this one-row fixture,
                // populated cells retain their actual content extents.
                probe.renderedTiles = 0;
                for (int columnIndex = 0; columnIndex < table->ColumnsCount; ++columnIndex)
                    if (table->Columns[columnIndex].ContentMaxXUnfrozen > table->Columns[columnIndex].WorkMinX + 10)
                        ++probe.renderedTiles;
                if (table->ColumnsCount < 4) continue;
                for (unsigned i = 0; i < 4; ++i) {
                    const auto& column = table->Columns[i];
                    probe.targets[i] = ImVec2((column.MinX + column.MaxX) * .5f, table->RowPosY1 + 48);
                    probe.tiles[i] = ImRect(column.MinX, table->RowPosY1, column.MaxX, table->RowPosY2);
                }
                probe.targetsReady = true;
            }
            // ImGui may defer queued mouse input by one frame. Observe the
            // processed release, rather than assuming it matches the enqueue frame.
            if (context->IO.MouseReleased[0] && context->HoveredWindow &&
                std::string_view(context->HoveredWindow->Name).find("items-grid") != std::string_view::npos) {
                for (unsigned i = 0; i < 4; ++i) {
                    if (!probe.targetsReady || !probe.tiles[i].Contains(context->IO.MousePos) || (probe.releasedTileMask & (1u << i))) continue;
                    probe.releasedTileMask |= 1u << i;
                    ++probe.hoveredReleases;
                }
            }
            const auto snapshot = gloss::gameplay::GetBackend().GetSnapshot();
            probe.pendingRequests = (std::max)(probe.pendingRequests, static_cast<unsigned>(snapshot.pendingCount));
            probe.requestHistory = (std::max)(probe.requestHistory, static_cast<unsigned>(snapshot.requests.size()));
            if (probe.visibleFrames >= 40) RecordLoadingPreview();
        };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &inspect);
    }
}

// Visual-only fixture: no global input hooks and no focus-dependent automation.
// The production input implementation is excluded only from this target.
namespace gloss::input {
    bool InstallHooks() { return true; }
    bool Attach(HWND) {
        std::ifstream mode(DataDirectory() / L"fixture-mode.txt");
        std::string value;
        mode >> value;
        if (value == "loading-preview") InstallLoadingPreviewProbe();
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
        if (value == "hover" || value == "quantity" || value == "dust-quantity") {
            static bool quantity{};
            static bool dust{};
            static std::string dustInput;
            dust = value == "dust-quantity";
            quantity = value == "quantity" || dust;
            if (dust) {
                std::ifstream input(DataDirectory() / L"quantity-input.txt");
                input >> dustInput;
            }
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePre;
            hook.Callback = [](ImGuiContext*, ImGuiContextHook*) {
                if (!overlay::Visible()) return;
                auto& io = ImGui::GetIO();
                io.AddMousePosEvent(dust ? 350.f : 520.f, 250);
                static unsigned visibleFrames{};
                ++visibleFrames;
                if (quantity && visibleFrames == 10) io.AddMouseButtonEvent(0, true);
                if (quantity && visibleFrames == 11) io.AddMouseButtonEvent(0, false);
                if (dust && !dustInput.empty() && visibleFrames >= 20) {
                    io.AddMousePosEvent(600, 387);
                    if (visibleFrames == 20) io.AddMouseButtonEvent(0, true);
                    if (visibleFrames == 21) io.AddMouseButtonEvent(0, false);
                    if (visibleFrames == 23) {
                        io.AddKeyEvent(ImGuiMod_Ctrl, true);
                        io.AddKeyEvent(ImGuiKey_A, true);
                    }
                    if (visibleFrames == 24) {
                        io.AddKeyEvent(ImGuiKey_A, false);
                        io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    }
                    if (visibleFrames == 25) io.AddInputCharactersUTF8(dustInput.c_str());
                }
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
        if (modeName == "loading-preview") {
            auto& backend = gloss::gameplay::GetBackend();
            backend.SetRuntimeState(true, "仅用于界面测试，不连接游戏", "loading-preview-fixture");
            backend.SetCapability("grant_item", true, "界面测试样本");
            std::vector<gloss::gameplay::ItemEntry> items;
            constexpr std::array<const char*, 6> names{"缓存·黑伤", "缓存·偃月刀", "增量·九转灵药", "增量·精铁", "", "未命名"};
            for (unsigned i = 0; i < names.size(); ++i) {
                gloss::gameplay::ItemEntry item;
                item.key = "loading-preview:" + std::to_string(i);
                item.name = names[i];
                item.category = i < 2 ? "主武器" : i == 2 ? "药品" : i == 3 ? "材料" : "未命名分类不应显示";
                item.description = i < 2 ? "名称缓存预览；等待完整游戏目录与库存读取。" : "增量读取预览；加载完成前不能获取。";
                item.maxQuantity = i < 2 ? 1 : gloss::gameplay::MaxItemQuantity;
                item.owned = -1;
                item.available = false;
                item.reason = "物品列表正在加载，请稍候。";
                items.push_back(std::move(item));
            }
            backend.PublishItems(std::move(items), false, "加载预览：4 个已命名条目，2 个未命名样本应过滤");
            const auto snapshot = backend.GetSnapshot();
            loadingPreviewProbe.samplesReadOnly = !snapshot.itemsReady && snapshot.items.size() == 6 &&
                std::all_of(snapshot.items.begin(), snapshot.items.end(), [](const auto& item) { return !item.available && item.owned == -1; });
            loadingPreviewProbe.backendRejected = !backend.SubmitItem("loading-preview:0", 1).accepted;
            return;
        }
        if (modeName == "dust-quantity") {
            auto& backend = gloss::gameplay::GetBackend();
            backend.SetRuntimeState(true, "仅用于界面测试，不连接游戏", "dust-fixture");
            backend.SetCapability("grant_item", true, "界面测试样本");
            gloss::gameplay::ItemEntry item;
            item.key = "resource:consciousness_dust";
            item.name = "意识尘埃";
            item.description = "数量输入界面测试样本，不连接游戏。";
            item.category = "资源";
            item.maxQuantity = gloss::gameplay::MaxItemQuantity;
            item.owned = 25555901;
            item.available = true;
            backend.PublishItems({std::move(item)}, true, "资源界面测试样本");
            return;
        }
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
