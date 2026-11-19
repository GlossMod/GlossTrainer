#include "runtime.hpp"
#include "model.hpp"
#include "icons.hpp"
#include "update.hpp"

#include <imgui.h>
#include <ShlObj.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace gloss::dndg {
namespace {

std::atomic<bool> menuOpen{};
std::atomic<bool> captureKey{};
std::atomic<unsigned> toggleKey{VK_INSERT};
std::atomic<unsigned> changedKey{};
bool dark = true;
bool showNext = false;
Locale language = Locale::Game;
std::filesystem::path settingsPath;

const char* Text(bool zh, const char* en, const char* cn) { return zh ? cn : en; }

std::filesystem::path SettingsPath() {
    PWSTR directory{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &directory)))
        return {};
    const auto path = std::filesystem::path(directory) / L"GlossTrainer" / L"DnDG" / L"native.ini";
    CoTaskMemFree(directory);
    return path;
}

bool ValidKey(unsigned key) {
    return key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END ||
        key == VK_PRIOR || key == VK_NEXT || (key >= VK_F1 && key <= VK_F12);
}

void LoadSettings(const wchar_t* configPath) {
    settingsPath = configPath ? std::filesystem::path(configPath) : SettingsPath();
    if (settingsPath.empty())
        return;
    const auto load = [](const wchar_t* name, int fallback) {
        return GetPrivateProfileIntW(L"UI", name, fallback, settingsPath.c_str());
    };
    dark = load(L"Dark", 1) != 0;
    showNext = load(L"ShowNext", 0) != 0;
    const unsigned selected = load(L"Language", 0);
    language = selected <= 2 ? static_cast<Locale>(selected) : Locale::Game;
    const unsigned key = load(L"Key", VK_INSERT);
    toggleKey.store(ValidKey(key) ? key : VK_INSERT);
}

void SaveSettings() {
    if (settingsPath.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(settingsPath.parent_path(), error);
    if (error)
        return;
    const auto save = [](const wchar_t* name, unsigned value) {
        const auto field = std::to_wstring(value);
        return WritePrivateProfileStringW(L"UI", name, field.c_str(), settingsPath.c_str()) != 0;
    };
    if (!save(L"Dark", dark) || !save(L"ShowNext", showNext) ||
            !save(L"Language", static_cast<unsigned>(language)) || !save(L"Key", toggleKey.load()))
        Log("Could not persist native trainer UI settings.");
}

void Style(float scale) {
    ImGuiStyle next;
    if (dark)
        ImGui::StyleColorsDark(&next);
    else
        ImGui::StyleColorsLight(&next);
    const ImVec4 gold = dark ? ImVec4(.81f, .69f, .45f, 1.f) : ImVec4(.54f, .39f, .17f, 1.f);
    const ImVec4 subtle = dark ? ImVec4(.39f, .32f, .22f, 1.f) : ImVec4(.88f, .81f, .67f, 1.f);
    next.Colors[ImGuiCol_WindowBg] = dark ? ImVec4(.09f, .10f, .12f, 1.f) : ImVec4(.96f, .95f, .93f, 1.f);
    next.Colors[ImGuiCol_PopupBg] = dark ? ImVec4(.13f, .14f, .17f, 1.f) : ImVec4(.99f, .98f, .95f, 1.f);
    next.Colors[ImGuiCol_TitleBgActive] = next.Colors[ImGuiCol_WindowBg];
    next.Colors[ImGuiCol_TabSelected] = subtle;
    next.Colors[ImGuiCol_TabSelectedOverline] = gold;
    next.Colors[ImGuiCol_TabHovered] = subtle;
    next.Colors[ImGuiCol_CheckMark] = gold;
    next.Colors[ImGuiCol_ResizeGrip] = ImVec4(gold.x, gold.y, gold.z, .42f);
    next.Colors[ImGuiCol_ResizeGripHovered] = gold;
    next.Colors[ImGuiCol_ResizeGripActive] = gold;
    next.WindowRounding = 8;
    next.FrameRounding = 4;
    next.PopupRounding = 5;
    next.WindowBorderSize = 1;
    next.PopupBorderSize = 1;
    next.FontSizeBase = 16;
    next.FontScaleDpi = scale;
    next.ScaleAllSizes(scale);
    ImGui::GetStyle() = next;
}

const char* KeyName(unsigned vk) {
    constexpr std::array functions{"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    if (vk >= VK_F1 && vk <= VK_F12)
        return functions[vk - VK_F1];
    switch (vk) {
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    default: return "Insert";
    }
}

void Detail(const Entry& card, bool zh, bool showEmpty = true) {
    if (card.concealed)
        return;
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 22 * ImGui::GetFontSize());
        ImGui::TextUnformatted(card.name.c_str());
        if (!card.effect.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted(card.effect.c_str());
        } else if (showEmpty) {
            ImGui::TextDisabled("%s", Text(zh, "No special effect", "无特殊效果"));
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

struct VitalDraft {
    int draft{};
    bool dirty{};
};

void DrawVital(const char* name, VitalDraft& draft, int actual, bool allowed, Command command, bool zh, bool heal = false) {
    ImGui::PushID(static_cast<int>(command.edit));
    if (!draft.dirty || !allowed)
        draft.draft = actual;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::SameLine(5.0f * ImGui::GetFontSize());
    ImGui::BeginDisabled(!allowed);
    ImGui::SetNextItemWidth(6.0f * ImGui::GetFontSize());
    if (ImGui::InputInt("##value", &draft.draft, 0, 0))
        draft.dirty = true;
    ImGui::SameLine();
    if (ImGui::Button(Text(zh, "Apply", "应用"))) {
        command.value = draft.draft;
        if (SubmitCommand(std::move(command)))
            draft.dirty = false;
    }
    if (heal) {
        ImGui::SameLine();
        if (ImGui::Button(Text(zh, "Heal", "回满"))) {
            command.edit = Edit::Heal;
            if (SubmitCommand(std::move(command)))
                draft.dirty = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

struct Picker {
    bool active{};
    Command command;
    std::array<char, 128> filter{};
};

void Pick(Picker& picker, Command command) { picker = {true, std::move(command), {}}; }

bool Matches(std::string_view text, std::string_view query) {
    return std::search(text.begin(), text.end(), query.begin(), query.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    }) != text.end() || query.empty();
}

float CellWidth() { return 4.4f * ImGui::GetFontSize(); }

void NextCell(int column) {
    if (column > 0)
        ImGui::SameLine();
}

bool Tile(const Entry& entry, bool zh) {
    // Keep display strings out of the ImGui ID stack.
    const bool clicked = ImGui::Button("##entry", {CellWidth(), 3.7f * ImGui::GetFontSize()});
    const auto top = ImGui::GetItemRectMin();
    const auto end = ImGui::GetItemRectMax();
    const auto* label = entry.concealed ? "?" : entry.name.c_str();
    const ImVec4 clip(top.x + 4, top.y, end.x - 4, end.y);
    const bool art = !entry.concealed && DrawItemIcon(entry.iconKey, top.x + 4, top.y + 4, end.x - 4, end.y - 4);
    if (!art) {
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            {clip.x, top.y + (end.y - top.y - ImGui::GetFontSize()) / 2},
            ImGui::GetColorU32(ImGuiCol_Text), label, nullptr, 0, &clip);
    }
    Detail(entry, zh);
    return clicked;
}

int Columns() {
    return std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) /
        (CellWidth() + ImGui::GetStyle().ItemSpacing.x)));
}

void DrawEntries(const char* title, const std::vector<Entry>& entries, Picker& picker,
        Kind kind, std::uint64_t encounter, bool allowed, bool zh) {
    ImGui::PushID(static_cast<int>(kind));
    ImGui::SeparatorText(title);
    const int columns = Columns();
    int column = 0;
    for (const auto& entry : entries) {
        // A pending context menu must stay attached to the original object even
        // when another command changes the row order. Hidden rows have no actions.
        if (entry.token)
            ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entry.token)));
        else
            ImGui::PushID(static_cast<int>(&entry - entries.data()));
        NextCell(column);
        Tile(entry, zh);
        if (!entry.concealed && ImGui::BeginPopupContextItem("actions")) {
            if (ImGui::MenuItem(Text(zh, "Discard", "丢弃"), nullptr, false, allowed && !entry.locked))
                SubmitCommand({Edit::Discard, kind, encounter, entry.token});
            if (ImGui::MenuItem(Text(zh, "Replace", "更换"), nullptr, false, allowed && !entry.locked))
                Pick(picker, {Edit::Replace, kind, encounter, entry.token});
            if (ImGui::MenuItem(Text(zh, "Add", "添加"), nullptr, false, allowed))
                Pick(picker, {Edit::Add, kind, encounter});
            ImGui::EndPopup();
        }
        ImGui::PopID();
        column = (column + 1) % columns;
    }
    NextCell(column);
    ImGui::BeginDisabled(!allowed);
    if (ImGui::Button("+##catalog", {CellWidth(), 3.7f * ImGui::GetFontSize()}))
        Pick(picker, {Edit::Add, kind, encounter});
    ImGui::SetItemTooltip("%s", Text(zh, "Add", "添加"));
    ImGui::EndDisabled();
    ImGui::PopID();
}

void DrawPicker(Picker& picker, const Catalog& catalog, bool allowed, bool zh) {
    if (!picker.active)
        return;
    ImGui::PushID("picker");
    ImGui::SeparatorText(picker.command.edit == Edit::Replace ? Text(zh, "Choose replacement", "选择替换项") :
        Text(zh, "Click to add", "点击添加"));
    if (ImGui::Button(Text(zh, "Cancel", "取消"))) {
        picker.active = false;
        ImGui::PopID();
        return;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", Text(zh, "Search name or ID...", "搜索名称或 ID…"), picker.filter.data(), picker.filter.size());
    const auto& entries = picker.command.kind == Kind::Charm ? catalog.charms :
        picker.command.kind == Kind::UniqueChip ? catalog.uniqueChips : catalog.cards;
    const int columns = Columns();
    int count = 0;
    ImGui::BeginDisabled(!allowed);
    for (const auto& entry : entries) {
        if (!Matches(entry.name, picker.filter.data()) && !Matches(entry.id, picker.filter.data()))
            continue;
        ImGui::PushID(entry.id.c_str());
        NextCell(count++ % columns);
        if (Tile(entry, zh)) {
            picker.command.id = entry.id;
            if (SubmitCommand(picker.command))
                picker.active = false;
        }
        ImGui::PopID();
        if (!picker.active)
            break;
    }
    ImGui::EndDisabled();
    if (!count)
        ImGui::TextDisabled("%s", Text(zh, "No matches", "没有匹配项"));
    ImGui::PopID();
}

void DrawSettings(bool zh) {
    if (ImGui::Checkbox(Text(zh, "Dark appearance", "暗色主题"), &dark))
        SaveSettings();
    const char* locales[] = {Text(zh, "Follow game", "跟随游戏"), "English", "简体中文"};
    int selected = static_cast<int>(language);
    if (ImGui::Combo(Text(zh, "Language", "语言"), &selected, locales, IM_ARRAYSIZE(locales))) {
        language = static_cast<Locale>(selected);
        SaveSettings();
    }
    ImGui::Text("%s: %s", Text(zh, "Toggle shortcut", "显示快捷键"), KeyName(toggleKey.load()));
    if (captureKey.load()) {
        ImGui::TextUnformatted(Text(zh, "Press Insert, Delete, Home, End, Page Up/Down or F1–F12.",
            "请按 Insert、Delete、Home、End、Page Up/Down 或 F1–F12。"));
        if (ImGui::Button(Text(zh, "Cancel", "取消")))
            captureKey.store(false);
    } else if (ImGui::Button(Text(zh, "Change shortcut", "更改快捷键"))) {
        captureKey.store(true);
    }
    ImGui::SeparatorText(Text(zh, "About", "关于"));
    ImGui::Text("GlossTrainer | DnDG  %s", Version);
    ImGui::TextUnformatted("C++20 / Dear ImGui / MinHook / Ultimate ASI Loader");
    if (ImGui::Button(Text(zh, "Check for updates", "检查更新")))
        CheckForUpdate();
    const auto update = ReadUpdate();
    switch (update.state) {
    case UpdateState::Running: ImGui::TextUnformatted(Text(zh, "Checking GitHub…", "正在查询 GitHub…")); break;
    case UpdateState::Current: ImGui::TextUnformatted(Text(zh, "No newer stable release", "没有更新的正式版本")); break;
    case UpdateState::NoRelease: ImGui::TextUnformatted(Text(zh, "No stable DnDG release found", "未发现 DnDG 正式版本")); break;
    case UpdateState::Unavailable: ImGui::TextUnformatted(Text(zh, "Could not check for updates", "无法检查更新")); break;
    case UpdateState::Available:
        ImGui::Text("%s %u.%u.%u", Text(zh, "New version:", "发现新版："), update.version[0], update.version[1], update.version[2]);
        break;
    default: break;
    }
}

void ShowNextCard(const Entry& card, bool zh) {
    const auto origin = ImGui::GetMainViewport()->Pos;
    ImGui::SetNextWindowPos({origin.x + 12, origin.y + 12});
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##next-card", nullptr, flags)) {
        // Keep the text as the ImGui item so it remains visible in fixture
        // logs and use that same item for the hover effect. Detail() is called
        // immediately after Text(), so BeginItemTooltip() sees the correct
        // last-item rectangle without cursor rewinding or synthetic buttons.
        ImGui::Text("%s: %s", Text(zh, "Next card", "下一张"), card.name.c_str());
        Detail(card, zh);
    }
    ImGui::End();
}

} // namespace

void ConfigureNativeImGui() { ConfigureNativeImGui(nullptr); }

void ConfigureNativeImGui(const wchar_t* configPath) {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    wchar_t path[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(path, MAX_PATH);
    if (length && length < MAX_PATH) {
        std::ifstream font(std::filesystem::path(path) / L"Fonts" / L"msyh.ttc", std::ios::binary | std::ios::ate);
        const auto size = font.tellg();
        if (size > 0 && size <= 64 * 1024 * 1024) {
            auto* bytes = ImGui::MemAlloc(static_cast<std::size_t>(size));
            font.seekg(0);
            if (bytes && font.read(static_cast<char*>(bytes), static_cast<std::streamsize>(size))) {
                auto* loaded = io.Fonts->AddFontFromMemoryTTF(bytes, static_cast<int>(size), 16.f);
                if (loaded)
                    io.FontDefault = loaded;
            } else {
                ImGui::MemFree(bytes);
            }
        }
    }
    if (!io.FontDefault)
        io.FontDefault = io.Fonts->AddFontDefault();
    static const bool loaded = (LoadSettings(configPath), true);
    (void)loaded;
    Style(1.0f);
    PublishUiRequest({language, menuOpen.load(), showNext});
}

bool NativeMenuOpen() noexcept { return menuOpen.load(); }

bool HandleNativeKey(unsigned vk, bool repeated) noexcept {
    if (captureKey.load()) {
        if (!repeated && vk == VK_ESCAPE)
            captureKey.store(false);
        else if (!repeated && ValidKey(vk)) {
            toggleKey.store(vk);
            changedKey.store(vk);
            captureKey.store(false);
        }
        return true;
    }
    if (vk == toggleKey.load()) {
        if (!repeated)
            menuOpen.store(!menuOpen.load());
        return true;
    }
    if (vk == VK_ESCAPE && menuOpen.load()) {
        if (!repeated)
            menuOpen.store(false);
        return true;
    }
    return false;
}

void DrawNativeOverlay(HWND) {
    if (changedKey.exchange(0))
        SaveSettings();
    bool open = NativeMenuOpen();
    const auto state = ReadSnapshot();
    const auto viewport = ImGui::GetMainViewport();
    const float scale = std::clamp(viewport->Size.y / 1080.f, .85f, 1.65f);
    Style(scale);
    const bool zh = (language == Locale::Game ? state->locale : language) == Locale::Chinese;
    if (!open) {
        if (showNext && state->nextCard && !state->nextCard->concealed)
            ShowNextCard(*state->nextCard, zh);
        PublishUiRequest({language, false, showNext});
        return;
    }

    PumpIconAtlas();

    const ImVec2 initialSize(std::min(448.f * scale, viewport->Size.x * .83f),
        std::min(370.f * scale, viewport->Size.y * .85f));
    ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - initialSize.x - 14.f * scale,
        viewport->Pos.y + 14.f * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({std::min(292.f * scale, viewport->Size.x), std::min(238.f * scale, viewport->Size.y)},
        {viewport->Size.x, viewport->Size.y});
    if (ImGui::Begin("GlossTrainer | DnDG", &open, ImGuiWindowFlags_NoCollapse)) {
        static std::uint64_t lastEncounter{};
        static VitalDraft health, shield, chips;
        static Picker cards, items;
        static Feedback feedback{};
        if (lastEncounter != state->encounter) {
            lastEncounter = state->encounter;
            health = shield = chips = {};
            cards = items = {};
            feedback = Feedback::None;
        }
        if (const auto received = TakeFeedback(); received != Feedback::None)
            feedback = received;
        ImGui::BeginChild("##contents", {0.f, -2.6f * ImGui::GetTextLineHeightWithSpacing()});
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem(Text(zh, "Player###player", "玩家###player"))) {
                if (!state->encounter)
                    ImGui::TextDisabled("%s", Text(zh, "No editable encounter", "暂无可编辑的遇敌对局"));
                else
                    ImGui::Text("%s %d/%d", Text(zh, "Health", "生命"), state->health, state->maxHealth);
                DrawVital(Text(zh, "Health", "生命"), health, state->health, state->canEditVitals, {Edit::Health, Kind::Hand, state->encounter}, zh, true);
                DrawVital(Text(zh, "Shield", "护盾"), shield, state->shield, state->canEditVitals, {Edit::Shield, Kind::Hand, state->encounter}, zh);
                DrawVital(Text(zh, "Chips", "筹码"), chips, state->chips, state->canEditVitals, {Edit::Chips, Kind::Hand, state->encounter}, zh);
                if (ImGui::Checkbox(Text(zh, "Show next card", "显示下一张牌"), &showNext))
                    SaveSettings();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Text(zh, "Cards###cards", "卡牌###cards"))) {
                DrawEntries(Text(zh, "Sleeve", "袖中牌"), state->sleeve, cards, Kind::Sleeve, state->encounter, state->canEditHand, zh);
                DrawEntries(Text(zh, "Table", "桌面牌"), state->hand, cards, Kind::Hand, state->encounter, state->canEditHand, zh);
                DrawPicker(cards, *ReadCatalog(), state->canEditHand, zh);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Text(zh, "Items###items", "物品###items"))) {
                DrawEntries(Text(zh, "Lucky charms", "幸运护符"), state->charms, items, Kind::Charm, state->encounter, state->canEditItems, zh);
                DrawEntries(Text(zh, "Unique chips", "独特筹码"), state->uniqueChips, items, Kind::UniqueChip, state->encounter, state->canEditItems, zh);
                DrawPicker(items, *ReadCatalog(), state->canEditItems, zh);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Text(zh, "Settings###settings", "设置###settings"))) {
                DrawSettings(zh);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        if (feedback != Feedback::None) {
            const char* message = feedback == Feedback::Applied ? Text(zh, "Applied", "已应用") :
                feedback == Feedback::Stale ? Text(zh, "Card or encounter changed", "卡牌或对局已变更") :
                feedback == Feedback::Busy ? Text(zh, "Command queue is busy", "操作队列已满") :
                Text(zh, "Not applied", "未应用");
            ImGui::TextUnformatted(message);
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s  ·  %s", KeyName(toggleKey.load()), Text(zh, "Show/hide", "显示/隐藏"));
    }
    ImGui::End();
    if (!open) {
        menuOpen.store(false);
        captureKey.store(false);
    }
    PublishUiRequest({language, open, showNext});
}

} // namespace gloss::dndg
