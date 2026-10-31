#include "runtime.hpp"
#include "ime.hpp"
#include "item_icons.hpp"
#include "hotkeys.hpp"
#include "localization.hpp"
#include "update.hpp"
#include "gameplay/backend.hpp"
#include "gameplay/catalog.hpp"
#include "gameplay/item_quantity.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gloss::overlay
{
    namespace
    {
        using localization::Text;

        // Visible labels change with the language; their ImGui identities do not.
        const char *WidgetLabel(std::string_view source, std::string_view stableId = {})
        {
            static std::map<std::pair<localization::Language, std::string>, std::string> labels;
            const auto marker = source.find("###");
            const auto visibleText = source.substr(0, marker);
            const auto identity = stableId.empty() ? (marker == std::string_view::npos ? source : source.substr(marker + 3)) : stableId;
            const auto key = std::pair{localization::GetLanguage(), std::string(source) + "###" + std::string(identity)};
            const auto [entry, inserted] = labels.try_emplace(key);
            if (inserted)
                entry->second = localization::Translate(visibleText) + "###" + std::string(identity);
            return entry->second.c_str();
        }

        template <typename... Args>
        std::string FormatText(std::string_view source, const Args &...args)
        {
            return std::vformat(localization::Translate(source), std::make_format_args(args...));
        }

        struct Settings
        {
            bool light{};
            bool debug{};
            float scale = 1.0f;
            float x = -1.0f, y = -1.0f;
            int page = 1;
        } settings;
        std::atomic_bool visible{};
        std::atomic_bool rendererReady{};
        HWND currentWindow{};
        bool dirty{}, forcePosition = true;
        ULONGLONG initializedAt{}, savedAt{};
        float previousScale{};
        bool previousLight{};
        ImVec4 accent;
        ImFont *headingFont{};
        constexpr std::array<const char *, 6> pages{"概览", "修改功能", "物品", "传送", "设置", "关于"};
        constexpr std::array<const char *, 6> categories{"全部", "生存", "战斗", "资源", "移动", "世界"};
        struct ParameterEditor
        {
            std::vector<char> text;
            double number{};
            std::int64_t integer{};
            bool boolean{};
        };
        std::map<std::string, std::vector<ParameterEditor>> parameterEditors;
        struct SubmissionNotice
        {
            bool present{}, accepted{};
            std::uint64_t requestId{};
            std::string featureId;
            std::string message;
        } submissionNotice;
        struct ItemSelection
        {
            std::string key;
            int quantity{1};
            std::uint64_t generation{};
            std::array<char, 64> quantityText{'1'};
        } itemSelection;
        struct ItemReceipt
        {
            std::uint64_t requestId{}, generation{};
            std::string key, name;
            int quantity{};
            ULONGLONG submittedAt{};
        };
        std::vector<ItemReceipt> itemReceipts;
        enum class ToastTone { Info, Success, Failure, Uncertain };
        struct ItemToast
        {
            std::uint64_t id{}, requestId{};
            std::string text, detail;
            ToastTone tone{};
            ULONGLONG shownAt{};
            std::string format, argument;
            int quantity{};
            bool translateArgument{};
        };
        std::deque<ItemToast> itemToasts;
        std::uint64_t nextToastId{1};
        bool hotkeysInitialized{};
        ULONGLONG hotkeysSavedAt{};
        ULONGLONG featuresSavedAt{};

        std::string ActionHotkeyId(std::string_view id) { return "action:" + std::string(id); }
        std::string ToggleHotkeyId(std::string_view id) { return "toggle:" + std::string(id); }
        void InitializeHotkeys()
        {
            if (hotkeysInitialized) return;
            std::vector<hotkeys::Action> actions;
            for (const auto& toggle : gameplay::ToggleCatalog())
            {
                const auto* on = gameplay::FindFeature(toggle.onFeatureId);
                if (toggle.page == 1 && on && gameplay::FindFeature(toggle.offFeatureId))
                    actions.push_back({ToggleHotkeyId(toggle.id), gameplay::DefaultParameters(*on), toggle.title});
            }
            for (const auto& feature : gameplay::Catalog())
            {
                const bool paired = std::any_of(gameplay::ToggleCatalog().begin(), gameplay::ToggleCatalog().end(),
                    [&](const auto& toggle) { return toggle.onFeatureId == feature.id || toggle.offFeatureId == feature.id; });
                if (feature.page != 1 || feature.functionName.empty() || paired) continue;
                const hotkeys::Chord defaultChord = feature.id == "flight_up" ? hotkeys::Chord{VK_PRIOR, 0} :
                    feature.id == "flight_down" ? hotkeys::Chord{VK_NEXT, 0} : hotkeys::Chord{};
                actions.push_back({ActionHotkeyId(feature.id), gameplay::DefaultParameters(feature), feature.title, defaultChord});
            }
            auto& registry = hotkeys::GetRegistry();
            registry.Configure(std::move(actions));
            std::string error;
            if (!registry.Load(DataDirectory() / L"hotkeys.ini", error)) Log(error);
            hotkeysInitialized = true;
        }
        void SaveHotkeys()
        {
            if (!hotkeys::GetRegistry().Dirty()) return;
            std::string error;
            if (!hotkeys::GetRegistry().Save(DataDirectory() / L"hotkeys.ini", error)) Log(error);
            hotkeysSavedAt = GetTickCount64();
        }

        float ReadNumber(const wchar_t *name, float fallback)
        {
            wchar_t text[64]{};
            GetPrivateProfileStringW(L"UI", name, L"", text, static_cast<DWORD>(std::size(text)), (DataDirectory() / L"ui.ini").c_str());
            if (!*text)
                return fallback;
            wchar_t *end{};
            const auto number = std::wcstof(text, &end);
            return end != text && !*end && std::isfinite(number) ? number : fallback;
        }
        void LoadSettings()
        {
            settings.light = ReadNumber(L"LightTheme", 0) == 1;
            settings.debug = ReadNumber(L"DebugEnabled", 0) == 1;
            settings.scale = std::clamp(ReadNumber(L"Scale", 1), 0.8f, 1.5f);
            settings.x = ReadNumber(L"X", -1);
            settings.y = ReadNumber(L"Y", -1);
            settings.page = static_cast<int>(std::clamp(ReadNumber(L"Page", 1), 0.f, static_cast<float>(pages.size() - 1)));
            if (settings.page == 3)
                settings.page = 1;
        }
        void SaveSettings()
        {
            if (!dirty)
                return;
            try
            {
                std::filesystem::create_directories(DataDirectory());
                const auto file = DataDirectory() / L"ui.ini";
                bool success = true;
                const auto write = [&](const wchar_t *key, auto value)
                {
                    const auto text = std::format(L"{}", value);
                    if (!WritePrivateProfileStringW(L"UI", key, text.c_str(), file.c_str()))
                        success = false;
                };
                write(L"LightTheme", settings.light ? 1 : 0);
                write(L"DebugEnabled", settings.debug ? 1 : 0);
                write(L"Scale", settings.scale);
                write(L"X", settings.x);
                write(L"Y", settings.y);
                write(L"Page", settings.page);
                dirty = !success;
                if (!success)
                    Log("Could not save UI settings");
            }
            catch (...)
            {
                Log("Could not save UI settings");
            }
            savedAt = GetTickCount64();
        }
        ImVec4 Color(unsigned red, unsigned green, unsigned blue, unsigned alpha = 255)
        {
            return ImVec4(red / 255.f, green / 255.f, blue / 255.f, alpha / 255.f);
        }
        void ApplyStyle(float scale)
        {
            auto &style = ImGui::GetStyle();
            style = ImGuiStyle();
            if (settings.light)
                ImGui::StyleColorsLight();
            else
                ImGui::StyleColorsDark();
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
        void Label(const char *text, float size, ImVec4 color, bool translate = true)
        {
            ImGui::PushFont(size >= 20 ? headingFont : nullptr, size);
            ImGui::TextColored(color, "%s", translate ? Text(text) : text);
            ImGui::PopFont();
        }
        void MutedWrapped(std::string_view text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", Text(text));
            ImGui::PopStyleColor();
        }
        void NavigationIcon(int pageIndex, ImVec2 origin, ImU32 color, float scale)
        {
            auto *draw = ImGui::GetWindowDrawList();
            const auto point = [&](float x, float y)
            { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
            const auto line = [&](float x1, float y1, float x2, float y2)
            { draw->AddLine(point(x1, y1), point(x2, y2), color, 1.5f * scale); };
            switch (pageIndex)
            {
            case 0:
                line(1, 9, 10, 1);
                line(10, 1, 19, 9);
                line(4, 7, 4, 19);
                line(16, 7, 16, 19);
                line(4, 19, 8, 19);
                line(12, 19, 16, 19);
                line(8, 19, 8, 12);
                line(8, 12, 12, 12);
                line(12, 12, 12, 19);
                break;
            case 1:
                line(1, 4, 19, 4);
                line(1, 10, 19, 10);
                line(1, 16, 19, 16);
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
                line(5, 10, 10, 19);
                line(10, 19, 15, 10);
                break;
            case 4:
                draw->AddCircle(point(10, 10), 6.5f * scale, color, 0, 1.5f * scale);
                draw->AddCircle(point(10, 10), 2.5f * scale, color, 0, 1.5f * scale);
                for (int tooth = 0; tooth < 8; ++tooth)
                {
                    const float angle = tooth * 3.14159265f / 4;
                    line(10 + std::cos(angle) * 7, 10 + std::sin(angle) * 7,
                         10 + std::cos(angle) * 9.5f, 10 + std::sin(angle) * 9.5f);
                }
                break;
            default:
                draw->AddCircle(point(10, 10), 8.5f * scale, color, 0, 1.5f * scale);
                draw->AddCircleFilled(point(10, 5.5f), 1 * scale, color);
                line(10, 9, 10, 15);
                break;
            }
        }
        bool NavigationItem(const char *label, int index, bool selected, float scale)
        {
            const auto origin = ImGui::GetCursorScreenPos();
            const ImVec2 size(144 * scale, 46 * scale);
            const bool pressed = ImGui::InvisibleButton("##navigation", size, ImGuiButtonFlags_EnableNav);
            auto *draw = ImGui::GetWindowDrawList();
            if (selected || ImGui::IsItemHovered() || ImGui::IsItemFocused())
            {
                draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                    ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), 5 * scale);
                if (selected)
                    draw->AddRectFilled(ImVec2(origin.x, origin.y + 4 * scale),
                                        ImVec2(origin.x + 3 * scale, origin.y + size.y - 4 * scale), ImGui::GetColorU32(accent), 1.5f * scale);
            }
            const auto color = ImGui::GetColorU32(selected ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            NavigationIcon(index, ImVec2(origin.x + 17 * scale, origin.y + 13 * scale), color, scale);
            draw->AddText(ImVec2(origin.x + 50 * scale, origin.y + 15 * scale), color, Text(label));
            return pressed;
        }
        void SelectPage(int page)
        {
            if (settings.page != page)
            {
                settings.page = page;
                dirty = true;
            }
        }
        void BeginCard(const char *id, ImVec2 size, float scale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 20 * scale));
            ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        }
        void EndCard()
        {
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        void PageHeading(const char *title, const char *subtitle, float scale)
        {
            Label(title, 28, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TextDisabled("%s", Text(subtitle));
            ImGui::Dummy(ImVec2(0, 10 * scale));
        }
        void BeginAutoCard(const char *id, float scale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * scale, 14 * scale));
            ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize, ImGuiWindowFlags_NoScrollbar);
        }
        ImVec4 ResultColor(gameplay::RequestState state)
        {
            switch (state)
            {
            case gameplay::RequestState::Verified:
                return settings.light ? Color(32, 118, 82) : Color(110, 200, 152);
            case gameplay::RequestState::Failed:
            case gameplay::RequestState::Indeterminate:
                return settings.light ? Color(173, 64, 54) : Color(235, 141, 128);
            case gameplay::RequestState::Dispatched:
                return accent;
            default:
                return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
        }
        const gameplay::Capability *FindCapability(const gameplay::Snapshot &snapshot, const std::string &id)
        {
            const auto found = std::find_if(snapshot.capabilities.begin(), snapshot.capabilities.end(),
                [&](const auto &capability) { return capability.featureId == id; });
            return found == snapshot.capabilities.end() ? nullptr : &*found;
        }
        bool FeatureReady(const gameplay::Snapshot &snapshot, const gameplay::FeatureDef &feature)
        {
            const auto *capability = FindCapability(snapshot, feature.id);
            return snapshot.ready && capability && capability->ready && !feature.functionName.empty();
        }
        std::string AvailabilityReason(const gameplay::Snapshot &snapshot, const gameplay::FeatureDef &feature)
        {
            if (!settings.debug)
                return feature.functionName.empty() ? "暂不支持此功能。" :
                    snapshot.ready ? "当前功能暂不可用。" : "请进入游戏存档后重试。";
            const auto *capability = FindCapability(snapshot, feature.id);
            if (feature.functionName.empty())
                return feature.unavailableReason;
            if (capability && !capability->ready && !capability->reason.empty())
                return capability->reason;
            if (!snapshot.ready)
                return snapshot.status.empty() ? "等待游戏运行时" : snapshot.status;
            return feature.unavailableReason.empty() ? "适配器尚未开放此功能" : feature.unavailableReason;
        }
        void RuntimeStatus(const gameplay::Snapshot &snapshot, float scale)
        {
            if (!settings.debug)
            {
                ImGui::TextColored(accent, "%s", Text(snapshot.ready ? "已连接游戏" : "等待游戏"));
                return;
            }
            ImGui::TextColored(accent, "%s", Text(snapshot.ready ? "运行时可用" : "等待运行时"));
            ImGui::SameLine(0, 16 * scale);
            ImGui::TextDisabled(Text("待处理 %zu"), snapshot.pendingCount);
            ImGui::TextWrapped("%s", Text(snapshot.status));
        }
        void Statistics(const gameplay::Snapshot &snapshot, float scale)
        {
            if (!settings.debug)
                return;
            if (snapshot.statistics.empty())
            {
                ImGui::TextDisabled(Text("尚无游戏读回数据"));
                return;
            }
            if (ImGui::BeginTable("actual-statistics", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(Text("项目"), ImGuiTableColumnFlags_WidthFixed, 138 * scale);
                ImGui::TableSetupColumn(Text("实际读回"), ImGuiTableColumnFlags_WidthStretch);
                for (const auto &statistic : snapshot.statistics)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", Text(statistic.label));
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", Text(statistic.value));
                }
                ImGui::EndTable();
            }
        }
        const gameplay::RequestRecord *LatestRequest(const gameplay::Snapshot &snapshot, const std::string &featureId)
        {
            for (auto record = snapshot.requests.rbegin(); record != snapshot.requests.rend(); ++record)
                if (record->request.featureId == featureId)
                    return &*record;
            return nullptr;
        }
        bool RequestOnPage(const gameplay::RequestRecord &record, int page)
        {
            const auto *feature = gameplay::FindFeature(record.request.featureId);
            return page == 0 || (feature && feature->page == page);
        }
        void RequestHistory(const gameplay::Snapshot &snapshot, int page, float height, float scale, bool showHeading = true)
        {
            if (!settings.debug)
                return;
            if (showHeading)
            {
                ImGui::TextUnformatted(Text("请求历史"));
                ImGui::SameLine();
                ImGui::TextDisabled(Text("排队 / 已调用 / 已回读验证"));
            }
            BeginCard("request-history", ImVec2(0, height), scale);
            if (submissionNotice.present && !submissionNotice.accepted)
            {
                const auto *feature = gameplay::FindFeature(submissionNotice.featureId);
                if (page == 0 || (feature && feature->page == page))
                {
                    ImGui::TextColored(ResultColor(gameplay::RequestState::Failed), Text("提交未受理"));
                    ImGui::TextWrapped(Text("%s：%s"), feature ? Text(feature->title) : submissionNotice.featureId.c_str(),
                        Text(submissionNotice.message));
                    ImGui::Separator();
                }
            }
            bool any{};
            for (auto record = snapshot.requests.rbegin(); record != snapshot.requests.rend(); ++record)
            {
                if (!RequestOnPage(*record, page))
                    continue;
                any = true;
                const auto *feature = gameplay::FindFeature(record->request.featureId);
                const auto label = std::format("#{}  {}", record->request.requestId,
                    feature ? localization::Translate(feature->title) : record->request.featureId);
                ImGui::TextUnformatted(label.c_str());
                ImGui::SameLine(0, 12 * scale);
                ImGui::TextColored(ResultColor(record->state), "%s", Text(gameplay::RequestStateLabel(record->state)));
                ImGui::TextWrapped("%s", Text(record->message));
                if (!record->request.arguments.empty())
                {
                    std::string arguments;
                    for (const auto &value : record->request.arguments)
                    {
                        if (!arguments.empty()) arguments += " | ";
                        arguments += value;
                    }
                    ImGui::TextDisabled(Text("参数："));
                    ImGui::SameLine(0, 0);
                    ImGui::TextWrapped("%s", arguments.c_str());
                }
                ImGui::Separator();
            }
            if (!any)
                ImGui::TextDisabled(Text("尚无请求。点击操作按钮后在此查看执行进度。"));
            EndCard();
        }
        std::vector<std::string> SavedToggleParameters(const gameplay::ToggleDef& definition,
            const gameplay::Snapshot& snapshot)
        {
            const auto* feature = gameplay::FindFeature(definition.onFeatureId);
            if (!feature) return {};
            std::string error;
            const auto preference = std::find_if(snapshot.togglePreferences.begin(), snapshot.togglePreferences.end(),
                [&](const auto& value) { return value.id == definition.id; });
            if (preference != snapshot.togglePreferences.end() && gameplay::ValidateParameters(*feature, preference->parameters, error))
                return preference->parameters;
            const auto observed = std::find_if(snapshot.toggleStates.begin(), snapshot.toggleStates.end(),
                [&](const auto& value) { return value.id == definition.id && value.known; });
            if (observed != snapshot.toggleStates.end() && gameplay::ValidateParameters(*feature, observed->parameters, error))
                return observed->parameters;
            return gameplay::DefaultParameters(*feature);
        }
        std::vector<ParameterEditor> &EditorsFor(const gameplay::FeatureDef &feature,
            const std::vector<std::string>* initialValues = nullptr)
        {
            const auto [entry, inserted] = parameterEditors.try_emplace(feature.id);
            if (inserted)
            {
                auto restored = hotkeys::GetRegistry().Get(ActionHotkeyId(feature.id));
                std::string validationError;
                if (restored && !gameplay::ValidateParameters(feature, restored->parameters, validationError)) restored.reset();
                for (std::size_t parameterIndex = 0; parameterIndex < feature.params.size(); ++parameterIndex)
                {
                    const auto& param = feature.params[parameterIndex];
                    const auto& initial = initialValues ? (*initialValues)[parameterIndex] :
                        restored ? restored->parameters[parameterIndex] : param.defaultValue;
                    ParameterEditor editor;
                    editor.text.resize(param.maxLength + 1);
                    std::copy_n(initial.data(), std::min(initial.size(), param.maxLength), editor.text.data());
                    const auto *first = initial.data();
                    const auto *last = first + initial.size();
                    if (param.type == gameplay::ParamType::Float)
                        std::from_chars(first, last, editor.number);
                    else if (param.type == gameplay::ParamType::Int)
                        std::from_chars(first, last, editor.integer);
                    else if (param.type == gameplay::ParamType::Bool)
                        editor.boolean = initial == "true" || initial == "1";
                    entry->second.push_back(std::move(editor));
                }
            }
            return entry->second;
        }
        std::vector<std::string> ParameterValues(const gameplay::FeatureDef &feature,
            const std::vector<ParameterEditor> &editors)
        {
            std::vector<std::string> values;
            for (std::size_t index = 0; index < feature.params.size(); ++index)
            {
                const auto &editor = editors[index];
                switch (feature.params[index].type)
                {
                case gameplay::ParamType::Float: values.push_back(std::format("{:.15g}", editor.number)); break;
                case gameplay::ParamType::Int: values.push_back(std::to_string(editor.integer)); break;
                case gameplay::ParamType::Bool: values.push_back(editor.boolean ? "true" : "false"); break;
                default: values.emplace_back(editor.text.data()); break;
                }
            }
            return values;
        }
        void HotkeyCell(const std::string& action, std::vector<std::string> parameters, float scale)
        {
            auto& registry = hotkeys::GetRegistry();
            const auto binding = registry.Get(action);
            const auto capture = registry.Capture();
            const bool editing = capture.action == action;
            const auto label = editing ? std::string(Text("请按键…")) : binding ? localization::Translate(hotkeys::ChordLabel(binding->chord)) : std::string(Text("未绑定"));
            ImGui::PushFont(nullptr, 13);
            auto displayLabel = label;
            const auto textWidth = std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2);
            if (ImGui::CalcTextSize(displayLabel.c_str()).x > textWidth)
            {
                while (!displayLabel.empty() && ImGui::CalcTextSize((displayLabel + "…").c_str()).x > textWidth)
                {
                    auto end = displayLabel.size() - 1;
                    while (end && (static_cast<unsigned char>(displayLabel[end]) & 0xc0) == 0x80) --end;
                    displayLabel.resize(end);
                }
                displayLabel += "…";
            }
            if (ImGui::Button(WidgetLabel(displayLabel, "hotkey"), ImVec2(-1, 26 * scale))) registry.BeginCapture(action, std::move(parameters));
            ImGui::PopFont();
            if (ImGui::IsItemHovered() || editing)
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
                if (binding) ImGui::TextUnformatted(label.c_str());
                ImGui::TextUnformatted(Text(editing ? capture.message.c_str() : "点击绑定快捷键，支持 Ctrl / Shift / Alt 组合。\nEscape 取消，Delete 清除；仅菜单隐藏时生效。\n参数与当前设置一同保存。"));
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        void ParameterInput(const gameplay::ParamDef &param, ParameterEditor &editor, bool showLabel = true, float width = 0)
        {
            if (param.type == gameplay::ParamType::Name && !settings.debug)
            {
                ImGui::TextDisabled(Text("请从物品列表选择"));
                return;
            }
            ImGui::PushID(param.name.c_str());
            if (showLabel)
                ImGui::TextUnformatted(Text(param.label));
            ImGui::SetNextItemWidth(width > 0 ? width : ImGui::GetContentRegionAvail().x);
            switch (param.type)
            {
            case gameplay::ParamType::Float:
                ImGui::InputDouble("##value", &editor.number, 0, 0, "%.9g");
                break;
            case gameplay::ParamType::Int:
                ImGui::InputScalar("##value", ImGuiDataType_S64, &editor.integer);
                break;
            case gameplay::ParamType::Bool:
                ImGui::Checkbox(WidgetLabel("参数值 true / false"), &editor.boolean);
                break;
            default:
                ImGui::InputTextWithHint("##value", Text(param.type == gameplay::ParamType::Name ? "输入真实 FName ID" : "输入参数"),
                    editor.text.data(), editor.text.size());
                break;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (param.type == gameplay::ParamType::Float || param.type == gameplay::ParamType::Int)
                    ImGui::SetTooltip(Text("%s\n允许范围：%g ～ %g\n输入仅暂存，点击应用后提交。"), Text(param.label), param.minimum, param.maximum);
                else if (param.type == gameplay::ParamType::Name)
                    ImGui::SetTooltip(Text("输入游戏中已存在的 FName 比较 ID：十进制或 0x 十六进制，可附加 :number。"));
            }
            ImGui::PopID();
        }
        void FeatureCard(const gameplay::FeatureDef &feature, const gameplay::Snapshot &snapshot, float scale)
        {
            ImGui::PushID(feature.id.c_str());
            BeginAutoCard("feature-card", scale);
            Label(feature.title.c_str(), 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            if (feature.persistent)
            {
                ImGui::SameLine(0, 14 * scale);
                ImGui::TextColored(accent, Text("可能写入存档"));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", Text(feature.description));
            ImGui::PopStyleColor();
            auto &editors = EditorsFor(feature);
            if (!feature.params.empty() && ImGui::BeginTable("parameters", 2, ImGuiTableFlags_SizingStretchSame))
            {
                for (std::size_t index = 0; index < feature.params.size(); ++index)
                {
                    ImGui::TableNextColumn();
                    ParameterInput(feature.params[index], editors[index]);
                }
                ImGui::EndTable();
            }
            auto values = ParameterValues(feature, editors);
            std::string error;
            const bool valid = gameplay::ValidateParameters(feature, values, error);
            const bool ready = FeatureReady(snapshot, feature);
            const auto *latest = LatestRequest(snapshot, feature.id);
            const bool active = latest && (latest->state == gameplay::RequestState::Pending || latest->state == gameplay::RequestState::Running);
            ImGui::BeginDisabled(!ready || !valid || active);
            if (ImGui::Button(WidgetLabel(feature.params.empty() ? "执行一次" : "应用参数", "submit"), ImVec2(128 * scale, 30 * scale)))
            {
                // Backend owns fixed arguments; submit only user parameter values.
                const auto result = gameplay::GetBackend().Submit(feature.id, std::move(values));
                submissionNotice = {true, result.accepted, result.requestId, feature.id, result.message};
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 14 * scale);
            if (latest)
                ImGui::TextColored(ResultColor(latest->state), "#%llu · %s",
                    static_cast<unsigned long long>(latest->request.requestId), Text(gameplay::RequestStateLabel(latest->state)));
            else
                ImGui::TextDisabled("%s", Text(ready ? "可提交 · 尚无执行记录" : "当前不可用"));
            if (!ready)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", Text(AvailabilityReason(snapshot, feature)));
                ImGui::PopStyleColor();
            }
            if (!valid)
                ImGui::TextWrapped("%s", Text(error));
            EndCard();
            ImGui::PopID();
        }
        void Overview(const char *renderer, const gameplay::Snapshot &snapshot, float scale)
        {
            PageHeading("概览", "影之刃零 · 游戏内工具面板", scale);
            if (!settings.debug)
            {
                BeginAutoCard("game-status", scale);
                Label(snapshot.ready ? "已连接游戏" : "等待游戏", 22, accent);
                ImGui::TextWrapped("%s", Text(snapshot.ready ? "选择修改功能或物品，开始使用游戏内工具。" : "进入游戏存档后即可使用可用功能。"));
                ImGui::Spacing();
                if (ImGui::Button(WidgetLabel("修改功能"), ImVec2(136 * scale, 32 * scale)))
                    SelectPage(1);
                ImGui::SameLine();
                if (ImGui::Button(WidgetLabel("浏览物品"), ImVec2(136 * scale, 32 * scale)))
                    SelectPage(2);
                EndCard();
                return;
            }
            const auto width = ImGui::GetContentRegionAvail().x;
            BeginCard("status", ImVec2((width - 14 * scale) * .5f, 96 * scale), scale);
            ImGui::TextDisabled(Text("运行时状态"));
            Label(snapshot.ready ? "可提交游戏请求" : "等待游戏运行时", 22, accent);
            EndCard();
            ImGui::SameLine(0, 14 * scale);
            BeginCard("renderer", ImVec2(0, 96 * scale), scale);
            ImGui::TextDisabled(Text("渲染后端"));
            Label(renderer && *renderer ? renderer : "未知", 22, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            EndCard();
            BeginAutoCard("runtime-readback", scale);
            RuntimeStatus(snapshot, scale);
            ImGui::Separator();
            Statistics(snapshot, scale);
            EndCard();
            RequestHistory(snapshot, 0, 140 * scale, scale);
        }
        bool ActiveRequest(const gameplay::Snapshot &snapshot, const std::string &featureId)
        {
            return std::any_of(snapshot.requests.begin(), snapshot.requests.end(), [&](const auto &record)
            {
                return record.request.featureId == featureId &&
                    (record.state == gameplay::RequestState::Pending || record.state == gameplay::RequestState::Running);
            });
        }
        void RowTooltip(const std::string &description, const std::string &reason, float scale)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340 * scale);
            ImGui::TextUnformatted(Text(description));
            if (!reason.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted(Text(reason));
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        void RowText(const char *text, float scale, bool disabled = false)
        {
            const auto position = ImGui::GetCursorScreenPos();
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(position, ImVec2(position.x + ImGui::GetContentRegionAvail().x, position.y + 44 * scale), true);
            if (disabled)
                ImGui::TextDisabled("%s", Text(text));
            else
                ImGui::TextUnformatted(Text(text));
            draw->PopClipRect();
        }
        bool Toggle(const char *id, bool enabled, bool known, float scale)
        {
            const ImVec2 size(34 * scale, 20 * scale);
            const auto origin = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                ImGui::GetColorU32(known && enabled ? accent : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)), size.y * .5f);
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
                draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                    ImGui::GetColorU32(accent), size.y * .5f);
            // An unknown state uses a centered muted thumb, never the visual position for "off".
            const float thumbX = !known ? size.x * .5f : enabled ? size.x - size.y * .5f : size.y * .5f;
            draw->AddCircleFilled(ImVec2(origin.x + thumbX, origin.y + size.y * .5f),
                size.y * .5f - 3 * scale, ImGui::GetColorU32(known ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            return pressed;
        }
        void ToggleRow(const gameplay::ToggleDef &definition, const gameplay::Snapshot &snapshot, float scale)
        {
            const auto observed = std::find_if(snapshot.toggleStates.begin(), snapshot.toggleStates.end(),
                [&](const auto &state) { return state.id == definition.id; });
            const bool known = observed != snapshot.toggleStates.end() && observed->known;
            const bool enabled = known && observed->enabled;
            const auto *onFeature = gameplay::FindFeature(definition.onFeatureId);
            const auto *offFeature = gameplay::FindFeature(definition.offFeatureId);
            const bool onReady = onFeature && FeatureReady(snapshot, *onFeature);
            const bool offReady = offFeature && FeatureReady(snapshot, *offFeature);
            const bool pending = ActiveRequest(snapshot, definition.onFeatureId) || ActiveRequest(snapshot, definition.offFeatureId);
            const auto savedParameters = SavedToggleParameters(definition, snapshot);
            std::string reason = settings.debug ? (observed == snapshot.toggleStates.end() ? "尚无游戏状态回读" : observed->reason) :
                known ? "切换此功能的开启或关闭状态。" : "暂未取得当前状态，请进入存档后稍候。";
            if (!onFeature || !offFeature)
                reason += "\n开关动作定义不完整";
            else if (!FeatureReady(snapshot, *onFeature))
                reason += "\n" + AvailabilityReason(snapshot, *onFeature);
            else if (!FeatureReady(snapshot, *offFeature))
                reason += "\n" + AvailabilityReason(snapshot, *offFeature);
            if (pending)
                reason += settings.debug ? "\n请求处理中，等待真实回读；开关位置保留最近读回值。" : "\n正在处理，请稍候。";
            const auto description = settings.debug ? definition.description : definition.title;
            ImGui::PushID(definition.id.c_str());
            const float rowHeight = std::max(44 * scale, ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            RowText(definition.title.c_str(), scale);
            RowTooltip(description, reason, scale);
            ImGui::TableSetColumnIndex(1);
            hotkeys::GetRegistry().SetParameters(ToggleHotkeyId(definition.id), savedParameters);
            HotkeyCell(ToggleHotkeyId(definition.id), savedParameters, scale);
            ImGui::TableSetColumnIndex(2);
            std::vector<std::string> values;
            std::string error;
            bool valid = onFeature != nullptr;
            if (onFeature && !gameplay::ToggleParameters(definition).empty())
            {
                auto& editors = EditorsFor(*onFeature, &savedParameters);
                if (onFeature->params.size() == 1)
                {
                    const float inputWidth = std::max(50 * scale, ImGui::GetContentRegionAvail().x - 72 * scale);
                    ParameterInput(onFeature->params.front(), editors.front(), false, inputWidth);
                    ImGui::SameLine(0, 8 * scale);
                }
                else
                {
                    for (std::size_t index = 0; index < onFeature->params.size(); ++index)
                        ParameterInput(onFeature->params[index], editors[index]);
                }
                values = ParameterValues(*onFeature, editors);
                valid = gameplay::ValidateParameters(*onFeature, values, error);
                ImGui::BeginDisabled(!known || !enabled || !onReady || !valid || pending);
                if (ImGui::Button(WidgetLabel("应用", "apply-toggle-parameters"), ImVec2(64 * scale, 0)))
                {
                    const auto result = gameplay::GetBackend().Submit(definition.onFeatureId, values);
                    submissionNotice = {true, result.accepted, result.requestId, definition.onFeatureId, result.message};
                }
                ImGui::EndDisabled();
                RowTooltip(description, !valid ? error : "开启后点击应用更新参数。", scale);
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("—");
            }
            ImGui::TableSetColumnIndex(3);
            const auto origin = ImGui::GetCursorScreenPos();
            const float contentHeight = rowHeight - ImGui::GetStyle().CellPadding.y * 2;
            if (!known && offReady)
            {
                // An uncertain partial write can still retain a valid recovery path.
                // Recovery is an explicit off request and ignores editor drafts.
                ImGui::BeginDisabled(pending);
                if (ImGui::Button(WidgetLabel("关闭", "recover-toggle-off"), ImVec2(64 * scale, 0)))
                {
                    const auto result = gameplay::GetBackend().Submit(definition.offFeatureId, {});
                    submissionNotice = {true, result.accepted, result.requestId, definition.offFeatureId, result.message};
                }
                ImGui::EndDisabled();
                RowTooltip(description, pending ? reason : "当前状态未确认；点击关闭尝试恢复原状态。", scale);
                ImGui::PopID();
                return;
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (contentHeight - 20 * scale) * .5f));
            // A bad editor draft must never prevent disabling an active feature.
            ImGui::BeginDisabled(!known || pending || (enabled ? !offReady : (!onReady || !valid)));
            if (Toggle("##toggle", enabled, known, scale))
            {
                // The target action is based only on the observed game state, never request history.
                const auto &featureId = enabled ? definition.offFeatureId : definition.onFeatureId;
                const auto result = gameplay::GetBackend().Submit(featureId, enabled ? std::vector<std::string>{} : values);
                submissionNotice = {true, result.accepted, result.requestId, featureId, result.message};
            }
            ImGui::EndDisabled();
            RowTooltip(description, !enabled && !valid ? error : reason, scale);
            const char *label = Text(pending ? "处理中" : !known ? "状态未知" : enabled ? "已启用" : "已关闭");
            ImGui::PushFont(nullptr, 14);
            const auto textSize = ImGui::CalcTextSize(label);
            const ImVec2 textPosition(origin.x + 42 * scale, origin.y + (contentHeight - textSize.y) * .5f);
            ImGui::SetCursorScreenPos(textPosition);
            ImGui::Dummy(textSize);
            ImGui::GetWindowDrawList()->AddText(textPosition,
                ImGui::GetColorU32(known && enabled && !pending ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)), label);
            ImGui::PopFont();
            RowTooltip(description, reason, scale);
            ImGui::PopID();
        }
        void ActionRow(const gameplay::FeatureDef &feature, const gameplay::Snapshot &snapshot, float scale)
        {
            ImGui::PushID(feature.id.c_str());
            const bool ready = FeatureReady(snapshot, feature);
            const bool active = ActiveRequest(snapshot, feature.id);
            const auto *latest = LatestRequest(snapshot, feature.id);
            auto &editors = EditorsFor(feature);
            const auto reason = ready ? (settings.debug && latest ? latest->message : std::string("点击按钮执行一次操作。")) :
                AvailabilityReason(snapshot, feature);
            const auto description = (settings.debug ? feature.description : feature.title) + (feature.persistent ? "\n此操作可能写入存档。" : "");
            ImGui::TableNextRow(ImGuiTableRowFlags_None,
                std::max(44 * scale, ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2));
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            RowText(feature.title.c_str(), scale);
            RowTooltip(description, reason, scale);
            ImGui::TableSetColumnIndex(1);
            HotkeyCell(ActionHotkeyId(feature.id), ParameterValues(feature, editors), scale);
            ImGui::TableSetColumnIndex(2);
            if (feature.params.size() == 1)
            {
                const float inputWidth = std::max(50 * scale, ImGui::GetContentRegionAvail().x - 72 * scale);
                ParameterInput(feature.params.front(), editors.front(), false, inputWidth);
                ImGui::SameLine(0, 8 * scale);
            }
            else if (feature.params.size() > 1)
            {
                for (std::size_t index = 0; index < feature.params.size(); ++index)
                    ParameterInput(feature.params[index], editors[index]);
            }
            auto values = ParameterValues(feature, editors);
            std::string error;
            const bool valid = gameplay::ValidateParameters(feature, values, error);
            if (valid) hotkeys::GetRegistry().SetParameters(ActionHotkeyId(feature.id), values);
            ImGui::BeginDisabled(!ready || !valid || active);
            const bool flightStep = feature.id == "flight_up" || feature.id == "flight_down";
            if (ImGui::Button(WidgetLabel(feature.params.empty() || flightStep ? "执行" : "应用", "submit"), ImVec2((feature.params.empty() ? 84 : 64) * scale, 0)))
            {
                const auto result = gameplay::GetBackend().Submit(feature.id, std::move(values));
                submissionNotice = {true, result.accepted, result.requestId, feature.id, result.message};
            }
            ImGui::EndDisabled();
            RowTooltip(description, !valid ? error : reason, scale);
            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            const char *label = active ? "处理中" : !ready ? (feature.functionName.empty() ? "未适配" : "不可用") :
                !valid ? "参数无效" : "可用";
            ImVec4 statusColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if (!active && ready && valid && latest && latest->request.generation == snapshot.generation)
            {
                switch (latest->state)
                {
                case gameplay::RequestState::Dispatched: label = settings.debug ? "已调用" : "结果待确认"; break;
                case gameplay::RequestState::Verified: label = settings.debug ? "已回读验证" : "已完成"; break;
                case gameplay::RequestState::Failed: label = "调用失败"; break;
                case gameplay::RequestState::Indeterminate: label = "结果未知"; break;
                default: break;
                }
                statusColor = ResultColor(latest->state);
            }
            ImGui::PushFont(nullptr, 14);
            ImGui::TextColored(statusColor, "%s", Text(label));
            ImGui::PopFont();
            RowTooltip(description, !valid ? error : reason, scale);
            ImGui::PopID();
        }
        void FeaturePage(const gameplay::Snapshot &snapshot, float scale)
        {
            static ImGuiTextFilter search;
            static int category{};
            ImGui::PushID("feature-page");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * scale, 6 * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 12 * scale));
            const auto paired = [](const std::string &id)
            {
                return std::any_of(gameplay::ToggleCatalog().begin(), gameplay::ToggleCatalog().end(),
                    [&](const auto &toggle) { return toggle.onFeatureId == id || toggle.offFeatureId == id; });
            };
            // The catalogs are fixed at startup.
            static const auto count = std::count_if(gameplay::Catalog().begin(), gameplay::Catalog().end(),
                [&](const auto &feature) { return feature.page == 1 && !feature.functionName.empty() && !paired(feature.id); }) +
                std::count_if(gameplay::ToggleCatalog().begin(), gameplay::ToggleCatalog().end(),
                    [](const auto &toggle) { return toggle.page == 1 && toggle.id != "godmode"; });
            const auto matchesSearch = [&](const auto &entry)
            {
                if (!search.IsActive())
                    return true;
                const auto searchable = entry.title + " " + entry.id + " " + entry.description + " " +
                    localization::Translate(entry.title) + " " + localization::Translate(entry.description);
                return search.PassFilter(searchable.c_str());
            };
            const auto headingY = ImGui::GetCursorPosY();
            const auto right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            Label("修改功能", 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SameLine(0, 16 * scale);
            ImGui::TextDisabled(Text("共 %zu 项"), static_cast<std::size_t>(count));
            RowTooltip(snapshot.ready ? "已连接游戏" : "等待游戏", settings.debug ? snapshot.status : "进入存档后使用可用功能。", scale);
            ImGui::SetCursorPos(ImVec2(right - 268 * scale, headingY));
            ImGui::SetNextItemWidth(268 * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34 * scale, 10 * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, settings.light ? Color(248, 249, 251) : Color(30, 33, 39));
            if (ImGui::InputTextWithHint("##feature-search", Text(settings.debug ? "搜索功能或 ID" : "搜索功能…"), search.InputBuf, sizeof(search.InputBuf)))
                search.Build();
            const auto searchOrigin = ImGui::GetItemRectMin();
            const auto searchEnd = ImGui::GetItemRectMax();
            const ImVec2 searchCenter(searchOrigin.x + 17 * scale, (searchOrigin.y + searchEnd.y) * .5f - scale);
            auto *draw = ImGui::GetWindowDrawList();
            const auto searchColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            draw->AddCircle(searchCenter, 5 * scale, searchColor, 0, 1.5f * scale);
            draw->AddLine(ImVec2(searchCenter.x + 3.5f * scale, searchCenter.y + 3.5f * scale),
                ImVec2(searchCenter.x + 7.5f * scale, searchCenter.y + 7.5f * scale), searchColor, 1.5f * scale);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::SetCursorPosY(headingY + 52 * scale);
            const auto categoryStart = ImGui::GetCursorScreenPos();
            const float categoryWidth = ImGui::GetContentRegionAvail().x / categories.size();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * scale, 6 * scale));
            ImGui::PushFont(nullptr, 14);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            for (int index = 0; index < static_cast<int>(categories.size()); ++index)
            {
                if (index) ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_Text, category == index ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Button(WidgetLabel(categories[index]), ImVec2(categoryWidth, 36 * scale)))
                    category = index;
                ImGui::PopStyleColor();
                if (category == index)
                {
                    const auto start = ImGui::GetItemRectMin();
                    const auto end = ImGui::GetItemRectMax();
                    draw->AddLine(ImVec2(start.x + 14 * scale, end.y - scale),
                        ImVec2(end.x - 14 * scale, end.y - scale), ImGui::GetColorU32(accent), 2 * scale);
                }
            }
            draw->AddLine(ImVec2(categoryStart.x, categoryStart.y + 36 * scale),
                ImVec2(categoryStart.x + categoryWidth * categories.size(), categoryStart.y + 36 * scale),
                ImGui::GetColorU32(ImGuiCol_Border));
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
            ImGui::SetCursorPosY(headingY + 104 * scale);
            const char *historyLabel = WidgetLabel(submissionNotice.present && !submissionNotice.accepted ?
                "请求历史 · 提交未受理###feature-history" : "请求历史###feature-history");
            const bool historyOpen = settings.debug && ImGui::GetStateStorage()->GetBool(ImGui::GetID(historyLabel), false);
            const float historyHeight = 132 * scale;
            const float historySpacing = ImGui::GetStyle().ItemSpacing.y;
            const float historyReserve = settings.debug ? ImGui::GetFrameHeight() + historySpacing * 2 : 0;
            const float tableHeight = std::max(120 * scale, ImGui::GetContentRegionAvail().y - historyReserve -
                (historyOpen ? historyHeight + historySpacing : 0));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14 * scale, 8 * scale));
            constexpr auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings;
            if (ImGui::BeginTable("feature-list", 4, flags, ImVec2(0, tableHeight)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(Text("功能"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(Text("快捷键"), ImGuiTableColumnFlags_WidthFixed, 108 * scale);
                ImGui::TableSetupColumn(Text("参数"), ImGuiTableColumnFlags_WidthFixed, 190 * scale);
                ImGui::TableSetupColumn(Text("状态"), ImGuiTableColumnFlags_WidthFixed, 134 * scale);
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 36 * scale);
                for (int column = 0; column < 4; ++column)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * scale);
                    ImGui::TableHeader(ImGui::TableGetColumnName(column));
                }
                unsigned matches{};
                for (const auto &toggle : gameplay::ToggleCatalog())
                {
                    if (toggle.page != 1 || toggle.id == "godmode" || (category && toggle.category != category) || !matchesSearch(toggle))
                        continue;
                    ++matches;
                    ToggleRow(toggle, snapshot, scale);
                }
                for (const auto &feature : gameplay::Catalog())
                {
                    if (feature.page != 1 || feature.functionName.empty() || (category && feature.category != category) ||
                        paired(feature.id) || !matchesSearch(feature))
                        continue;
                    ++matches;
                    ActionRow(feature, snapshot, scale);
                }
                if (!matches)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 44 * scale);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(Text("没有匹配的功能"));
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            if (settings.debug && ImGui::CollapsingHeader(historyLabel))
                RequestHistory(snapshot, 1, historyHeight, scale, false);
            ImGui::PopStyleVar(2);
            ImGui::PopID();
        }
        void CatalogPage(int page, const gameplay::Snapshot &snapshot, float scale)
        {
            static std::array<ImGuiTextFilter, 3> searches;
            static std::array<int, 3> selectedCategories{};
            auto &search = searches[page - 1];
            auto &category = selectedCategories[page - 1];
            ImGui::PushID(page);
            const auto headingY = ImGui::GetCursorPosY();
            const auto right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            Label(pages[page], 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SetCursorPos(ImVec2(right - 268 * scale, headingY));
            ImGui::SetNextItemWidth(268 * scale);
            if (ImGui::InputTextWithHint("##catalog-search", Text("搜索名称、功能 ID…"), search.InputBuf, sizeof(search.InputBuf)))
                search.Build();
            ImGui::SetCursorPosY(headingY + 42 * scale);
            RuntimeStatus(snapshot, scale);
            if (settings.debug && ImGui::CollapsingHeader(WidgetLabel("实际读回")))
                Statistics(snapshot, scale);
            if (page == 2)
                ImGui::TextDisabled(Text("物品名称目录待接入；仅接受游戏中真实存在的 FName ID。"));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * scale, 6 * scale));
            const auto categoryWidth = (ImGui::GetContentRegionAvail().x - 20 * scale) / categories.size();
            for (int index = 0; index < static_cast<int>(categories.size()); ++index)
            {
                if (index) ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, category == index ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushStyleColor(ImGuiCol_Button, category == index ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImVec4(0, 0, 0, 0));
                if (ImGui::Button(WidgetLabel(categories[index]), ImVec2(categoryWidth, 30 * scale)))
                    category = index;
                ImGui::PopStyleColor(2);
            }
            ImGui::PopStyleVar();
            const char *historyLabel = WidgetLabel("请求历史###catalog-history");
            const bool historyOpen = settings.debug && ImGui::GetStateStorage()->GetBool(ImGui::GetID(historyLabel), false);
            const float historyHeight = 132 * scale;
            const float listHeight = std::max(130 * scale, ImGui::GetContentRegionAvail().y - (settings.debug ? 36 * scale : 0) -
                (historyOpen ? historyHeight + 10 * scale : 0));
            ImGui::BeginChild("catalog-list", ImVec2(0, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            unsigned matches{};
            for (const auto &feature : gameplay::Catalog())
            {
                const auto searchText = feature.title + " " + feature.id + " " + feature.description + " " +
                    localization::Translate(feature.title) + " " + localization::Translate(feature.description);
                if (feature.page != page || (category && feature.category != category) || !search.PassFilter(searchText.c_str()))
                    continue;
                ++matches;
                FeatureCard(feature, snapshot, scale);
            }
            if (!matches)
                ImGui::TextDisabled(Text("没有匹配的功能"));
            ImGui::EndChild();
            if (settings.debug && ImGui::CollapsingHeader(historyLabel))
                RequestHistory(snapshot, page, historyHeight, scale, false);
            ImGui::PopID();
        }
        ItemToast &ShowItemToast(std::string message, std::string detail, ToastTone tone, std::uint64_t requestId = 0)
        {
            if (requestId)
            {
                const auto existing = std::find_if(itemToasts.begin(), itemToasts.end(),
                    [&](const auto &toast) { return toast.requestId == requestId; });
                if (existing != itemToasts.end())
                {
                    existing->text = std::move(message);
                    existing->detail = std::move(detail);
                    existing->tone = tone;
                    existing->shownAt = GetTickCount64();
                    existing->format.clear();
                    existing->argument.clear();
                    return *existing;
                }
            }
            itemToasts.push_back({nextToastId++, requestId, std::move(message), std::move(detail), tone, GetTickCount64()});
            while (itemToasts.size() > 4)
                itemToasts.pop_front();
            return itemToasts.back();
        }
        void ShowFormattedToast(std::string format, std::string argument, std::string detail, ToastTone tone,
            std::uint64_t requestId = 0, int quantity = 0, bool translateArgument = false)
        {
            auto &toast = ShowItemToast({}, std::move(detail), tone, requestId);
            toast.format = std::move(format);
            toast.argument = std::move(argument);
            toast.quantity = quantity;
            toast.translateArgument = translateArgument;
        }
        void ProcessHotkeys(const gameplay::Snapshot& snapshot)
        {
            auto& registry = hotkeys::GetRegistry();
            registry.PumpCapture(DataDirectory() / L"hotkeys.ini");
            for (auto& invocation : registry.Drain())
            {
                const gameplay::FeatureDef* feature{};
                std::string title, error;
                if (Visible() || GetForegroundWindow() != currentWindow)
                {
                    registry.Complete(invocation.action);
                    continue;
                }
                if (invocation.action.starts_with("toggle:"))
                {
                    const auto id = std::string_view(invocation.action).substr(7);
                    const auto definition = std::find_if(gameplay::ToggleCatalog().begin(), gameplay::ToggleCatalog().end(),
                        [&](const auto& toggle) { return toggle.id == id; });
                    if (definition != gameplay::ToggleCatalog().end())
                    {
                        title = definition->title;
                        const auto state = std::find_if(snapshot.toggleStates.begin(), snapshot.toggleStates.end(),
                            [&](const auto& value) { return value.id == definition->id; });
                        if (state == snapshot.toggleStates.end() || !state->known) error = "尚未取得功能的当前状态。";
                        else if (ActiveRequest(snapshot, definition->onFeatureId) || ActiveRequest(snapshot, definition->offFeatureId)) error = "请求正在处理中，请稍候。";
                        else
                        {
                            feature = gameplay::FindFeature(state->enabled ? definition->offFeatureId : definition->onFeatureId);
                            // Shortcuts use the last successful on-parameters, never
                            // unsubmitted editor drafts or stale queued binding values.
                            invocation.parameters = state->enabled ? std::vector<std::string>{} : SavedToggleParameters(*definition, snapshot);
                        }
                    }
                }
                else if (invocation.action.starts_with("action:")) feature = gameplay::FindFeature(std::string_view(invocation.action).substr(7));
                if (feature)
                {
                    if (title.empty()) title = feature->title;
                    if (!FeatureReady(snapshot, *feature)) error = AvailabilityReason(snapshot, *feature);
                    else if (ActiveRequest(snapshot, feature->id)) error = "请求正在处理中，请稍候。";
                    else gameplay::ValidateParameters(*feature, invocation.parameters, error);
                }
                else if (error.empty()) error = "此功能已不再提供。";
                if (error.empty() && feature)
                {
                    const auto result = gameplay::GetBackend().Submit(feature->id, std::move(invocation.parameters));
                    submissionNotice = {true, result.accepted, result.requestId, feature->id, result.message};
                    ShowFormattedToast(result.accepted ? "已提交：{}" : "无法执行：{}", title, result.message,
                        result.accepted ? ToastTone::Info : ToastTone::Failure, 0, 0, true);
                }
                else ShowFormattedToast("快捷键无法执行：{}", title, error, ToastTone::Failure, 0, 0, true);
                registry.Complete(invocation.action);
            }
        }
        void UpdateItemReceipts(const gameplay::Snapshot &snapshot)
        {
            for (auto receipt = itemReceipts.begin(); receipt != itemReceipts.end();)
            {
                const auto record = std::find_if(snapshot.requests.begin(), snapshot.requests.end(),
                    [&](const auto &entry) { return entry.request.requestId == receipt->requestId; });
                if (record != snapshot.requests.end() &&
                    (record->state == gameplay::RequestState::Pending || record->state == gameplay::RequestState::Running))
                {
                    ++receipt;
                    continue;
                }
                if (record != snapshot.requests.end())
                {
                    switch (record->state)
                    {
                    case gameplay::RequestState::Verified:
                        ShowFormattedToast("获取 {0} 个{1}成功", receipt->name, record->message,
                            ToastTone::Success, receipt->requestId, receipt->quantity);
                        break;
                    case gameplay::RequestState::Failed:
                    case gameplay::RequestState::Expired:
                    case gameplay::RequestState::Cancelled:
                        ShowFormattedToast("获取 {0} 个{1}失败", receipt->name, record->message,
                            ToastTone::Failure, receipt->requestId, receipt->quantity);
                        break;
                    default:
                        ShowFormattedToast("获取 {0} 个{1}结果未确认", receipt->name, record->message,
                            ToastTone::Uncertain, receipt->requestId, receipt->quantity);
                        break;
                    }
                }
                else if (snapshot.generation != receipt->generation)
                    ShowFormattedToast("获取 {0} 个{1}结果未确认", receipt->name,
                        "游戏会话已变化，请检查背包；不会自动重试。", ToastTone::Uncertain, receipt->requestId, receipt->quantity);
                else if (GetTickCount64() - receipt->submittedAt < 10000)
                {
                    ++receipt;
                    continue;
                }
                else
                    ShowFormattedToast("获取 {0} 个{1}结果未确认", receipt->name,
                        "暂未取得操作结果，请检查背包；不会自动重试。", ToastTone::Uncertain, receipt->requestId, receipt->quantity);
                receipt = itemReceipts.erase(receipt);
            }
        }
        void DrawItemToasts(float scale)
        {
            const auto now = GetTickCount64();
            std::erase_if(itemToasts, [&](const auto &toast) { return now - toast.shownAt > 6500; });
            const auto *viewport = ImGui::GetMainViewport();
            float bottom = viewport->WorkPos.y + viewport->WorkSize.y - 24 * scale;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * scale, 12 * scale));
            for (auto toast = itemToasts.rbegin(); toast != itemToasts.rend(); ++toast)
            {
                ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 24 * scale, bottom),
                    ImGuiCond_Always, ImVec2(1, 1));
                ImGui::SetNextWindowSize(ImVec2(380 * scale, 0));
                const auto id = std::format("##item-toast-{}", toast->id);
                ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                const auto color = toast->tone == ToastTone::Success ? ResultColor(gameplay::RequestState::Verified) :
                    toast->tone == ToastTone::Failure ? ResultColor(gameplay::RequestState::Failed) : accent;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                const auto argument = toast->translateArgument ? localization::Translate(toast->argument) : toast->argument;
                const auto message = toast->format.empty() ? localization::Translate(toast->text) :
                    toast->quantity > 0 ? FormatText(toast->format, toast->quantity, argument) : FormatText(toast->format, argument);
                ImGui::TextWrapped("%s", message.c_str());
                ImGui::PopStyleColor();
                if (settings.debug && !toast->detail.empty())
                    ImGui::TextWrapped("%s", Text(toast->detail));
                bottom -= ImGui::GetWindowHeight() + 10 * scale;
                ImGui::End();
            }
            ImGui::PopStyleVar();
        }
        std::string SearchText(std::string text)
        {
            for (auto &value : text)
                if (value >= 'A' && value <= 'Z')
                    value = static_cast<char>(value - 'A' + 'a');
            return text;
        }
        bool ItemPending(const std::string &key)
        {
            return std::any_of(itemReceipts.begin(), itemReceipts.end(),
                [&](const auto &receipt) { return receipt.key == key; });
        }
        bool BrowsableItem(const gameplay::ItemEntry &item, bool loading)
        {
            return settings.debug || (!item.name.empty() && item.name != "未命名" &&
                (loading || item.available || (item.maxQuantity == 1 && item.owned > 0)));
        }
        void ItemThumbnail(const gameplay::ItemEntry &item, ImVec2 position, ImVec2 size, float scale)
        {
            auto *draw = ImGui::GetWindowDrawList();
            if (const auto icon = ui::FindItemIcon(item.iconKey))
            {
                if (icon->size.x > 0 && icon->size.y > 0)
                {
                    const float ratio = std::min(size.x / icon->size.x, size.y / icon->size.y);
                    const ImVec2 fitted(icon->size.x * ratio, icon->size.y * ratio);
                    const ImVec2 topLeft(position.x + (size.x - fitted.x) * .5f, position.y + (size.y - fitted.y) * .5f);
                    draw->AddImage(icon->texture, topLeft, ImVec2(topLeft.x + fitted.x, topLeft.y + fitted.y), icon->uv0, icon->uv1);
                    return;
                }
            }
            const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const float edge = std::min(size.x, size.y) * .36f;
            const ImVec2 topLeft(position.x + (size.x - edge) * .5f, position.y + (size.y - edge) * .5f);
            const auto point = [&](float x, float y) { return ImVec2(topLeft.x + x * edge, topLeft.y + y * edge); };
            draw->AddRect(topLeft, point(1, 1), color, 4 * scale, 0, 1.4f * scale);
            draw->AddCircle(point(.7f, .29f), edge * .085f, color, 12, 1.4f * scale);
            draw->AddLine(point(.13f, .79f), point(.39f, .47f), color, 1.4f * scale);
            draw->AddLine(point(.39f, .47f), point(.59f, .69f), color, 1.4f * scale);
            draw->AddLine(point(.59f, .69f), point(.72f, .56f), color, 1.4f * scale);
            draw->AddLine(point(.72f, .56f), point(.88f, .79f), color, 1.4f * scale);
        }
        void ItemDetails(const gameplay::ItemEntry &item, const gameplay::Snapshot &snapshot, float scale)
        {
            const float width = std::min(340 * scale, ImGui::GetIO().DisplaySize.x - 32 * scale);
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0),
                ImVec2(width, std::max(180 * scale, ImGui::GetIO().DisplaySize.y - 32 * scale)));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale, 16 * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * scale, 8 * scale));
            if (ImGui::BeginTooltip())
            {
                const float contentWidth = width - 36 * scale;
                const float imageHeight = 132 * scale;
                const auto imagePosition = ImGui::GetCursorScreenPos();
                ItemThumbnail(item, imagePosition, ImVec2(contentWidth, imageHeight), scale);
                ImGui::Dummy(ImVec2(contentWidth, imageHeight));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
                Label(item.name.c_str(), 22, ImGui::GetStyleColorVec4(ImGuiCol_Text), false);
                ImGui::TextDisabled("%s", Text(item.category));
                ImGui::Separator();
                ImGui::TextUnformatted(item.description.empty() ? Text("暂无详细描述。") : item.description.c_str());
                if (item.owned >= 0)
                    ImGui::TextDisabled(Text("已持有：%lld"), static_cast<long long>(item.owned));
                const bool busy = ItemPending(item.key);
                const bool ownedUnique = item.maxQuantity == 1 && item.owned > 0;
                if (busy)
                    ImGui::TextColored(accent, Text("正在获取，请稍候…"));
                else if (ownedUnique)
                    ImGui::TextDisabled(Text("已拥有，不能重复获取。"));
                else if (!snapshot.ready)
                    ImGui::TextDisabled(Text("进入游戏存档后获取。"));
                else if (!snapshot.itemsReady)
                    ImGui::TextDisabled(Text("物品列表正在加载，请稍候。"));
                else if (item.available)
                    ImGui::TextColored(accent, Text("点击图标获取"));
                else
                    ImGui::TextDisabled(Text("此物品暂不可获取。"));
                if (settings.debug)
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", item.key.c_str());
                    if (!item.reason.empty())
                        ImGui::TextUnformatted(Text(item.reason));
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::PopStyleVar(2);
        }
        bool ItemTile(const gameplay::ItemEntry &item, const gameplay::Snapshot &snapshot, float width, float scale)
        {
            const float imageHeight = 102 * scale;
            const float titleHeight = ImGui::GetTextLineHeight() * 2;
            const float height = imageHeight + titleHeight + 24 * scale;
            const auto position = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("##item-tile", ImVec2(width, height), ImGuiButtonFlags_EnableNav);
            const bool hovered = ImGui::IsItemHovered();
            const bool focused = ImGui::IsItemFocused();
            const bool busy = ItemPending(item.key);
            const bool ownedUnique = item.maxQuantity == 1 && item.owned > 0;
            const bool canGrant = snapshot.ready && snapshot.itemsReady && item.available && !busy;
            auto *draw = ImGui::GetWindowDrawList();
            const ImVec2 end(position.x + width, position.y + height);
            draw->AddRectFilled(position, end, ImGui::GetColorU32(hovered || focused ? ImGuiCol_FrameBgHovered : ImGuiCol_ChildBg), 6 * scale);
            draw->AddRect(position, end, ImGui::GetColorU32(hovered || focused ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Border)),
                6 * scale, 0, (hovered || focused ? 1.5f : 1.f) * scale);
            draw->PushClipRect(position, end, true);
            ItemThumbnail(item, ImVec2(position.x + 10 * scale, position.y + 8 * scale),
                ImVec2(width - 20 * scale, imageHeight), scale);
            const float textWidth = width - 16 * scale;
            const auto measured = ImGui::CalcTextSize(item.name.c_str(), nullptr, false, textWidth);
            const ImVec2 title(position.x + (width - std::min(measured.x, textWidth)) * .5f, position.y + imageHeight + 14 * scale);
            const ImVec4 clip(position.x + 8 * scale, title.y, end.x - 8 * scale, title.y + titleHeight);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), title,
                ImGui::GetColorU32(ImGuiCol_Text), item.name.c_str(), nullptr, textWidth, &clip);
            const char *badge = busy ? Text("处理中") : ownedUnique ? Text("已拥有") : nullptr;
            if (badge)
            {
                const auto badgeSize = ImGui::CalcTextSize(badge);
                const ImVec2 topLeft(position.x + 7 * scale, position.y + 7 * scale);
                draw->AddRectFilled(topLeft, ImVec2(topLeft.x + badgeSize.x + 10 * scale, topLeft.y + badgeSize.y + 6 * scale),
                    ImGui::GetColorU32(ImGuiCol_ButtonActive), 3 * scale);
                draw->AddText(ImVec2(topLeft.x + 5 * scale, topLeft.y + 3 * scale), ImGui::GetColorU32(accent), badge);
            }
            draw->PopClipRect();
            if (hovered || focused)
                ItemDetails(item, snapshot, scale);
            return pressed && canGrant;
        }
        void ItemsPage(const gameplay::Snapshot &snapshot, float scale)
        {
            const auto browsable = [&](const gameplay::ItemEntry &item)
                { return BrowsableItem(item, !snapshot.itemsReady); };
            static std::array<char, 256> search{};
            static std::string category;
            static std::size_t page{}, lastFingerprint{};
            static std::uint64_t lastGeneration{};
            static bool resetScroll{};
            // Scanning every row belongs to a new publication, not to every frame.
            static gameplay::ItemList derivedItems;
            static std::string derivedStatus;
            static bool derivedReady{}, derivedDebug{}, derivedValid{};
            static std::vector<std::string> itemCategories;
            static std::size_t browsableCount{};
            const bool publicationChanged = !derivedValid || derivedItems.Identity() != snapshot.items.Identity() ||
                derivedReady != snapshot.itemsReady || derivedDebug != settings.debug || derivedStatus != snapshot.itemCatalogStatus;
            if (publicationChanged)
            {
                std::size_t fingerprint = std::hash<std::string>{}(snapshot.itemCatalogStatus);
                const auto combine = [&](std::size_t value)
                    { fingerprint ^= value + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2); };
                combine(snapshot.itemsReady);
                itemCategories.clear();
                browsableCount = 0;
                for (const auto &item : snapshot.items)
                {
                    combine(std::hash<std::string>{}(item.key));
                    combine(std::hash<std::string>{}(item.name));
                    combine(std::hash<std::string>{}(item.category));
                    combine(std::hash<std::string>{}(item.iconKey));
                    combine(std::hash<std::string>{}(item.reason));
                    combine(static_cast<std::size_t>(item.maxQuantity));
                    combine(item.available);
                    if (!browsable(item))
                        continue;
                    ++browsableCount;
                    if (!item.category.empty() && std::find(itemCategories.begin(), itemCategories.end(), item.category) == itemCategories.end())
                        itemCategories.push_back(item.category);
                }
                derivedItems = snapshot.items;
                derivedStatus = snapshot.itemCatalogStatus;
                derivedReady = snapshot.itemsReady;
                derivedDebug = settings.debug;
                derivedValid = true;
                if (lastFingerprint != fingerprint)
                {
                    itemSelection = {};
                    lastFingerprint = fingerprint;
                    resetScroll = true;
                }
            }
            if (lastGeneration != snapshot.generation)
            {
                itemSelection = {};
                lastGeneration = snapshot.generation;
                resetScroll = true;
            }
            if (!category.empty() && std::find(itemCategories.begin(), itemCategories.end(), category) == itemCategories.end())
            {
                category.clear();
                page = 0;
            }
            ImGui::PushID("items-page");
            const auto headingY = ImGui::GetCursorPosY();
            const auto right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            Label("物品", 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SameLine(0, 16 * scale);
            ImGui::TextDisabled(Text("共 %zu 项"), browsableCount);
            ImGui::SetCursorPos(ImVec2(right - 268 * scale, headingY));
            ImGui::SetNextItemWidth(268 * scale);
            if (ImGui::InputTextWithHint("##item-search", Text("搜索物品名称…"), search.data(), search.size()))
            {
                page = 0;
                itemSelection = {};
                resetScroll = true;
            }
            ImGui::SetCursorPosY(headingY + 48 * scale);
            const auto chooseCategory = [&](const std::string &value)
            {
                category = value;
                page = 0;
                itemSelection = {};
                resetScroll = true;
            };
            if (itemCategories.size() <= 6)
            {
                const float width = ImGui::GetContentRegionAvail().x / static_cast<float>(itemCategories.size() + 1);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                for (std::size_t index = 0; index <= itemCategories.size(); ++index)
                {
                    if (index) ImGui::SameLine(0, 0);
                    const auto value = index ? itemCategories[index - 1] : std::string{};
                    const bool selected = category == value;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Button(WidgetLabel(index ? value.c_str() : "全部"), ImVec2(width, 34 * scale)))
                        chooseCategory(value);
                    ImGui::PopStyleColor(2);
                    if (selected)
                    {
                        const auto start = ImGui::GetItemRectMin();
                        const auto end = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + 10 * scale, end.y - scale),
                            ImVec2(end.x - 10 * scale, end.y - scale), ImGui::GetColorU32(accent), 2 * scale);
                    }
                }
                ImGui::PopStyleVar();
            }
            else
            {
                ImGui::SetNextItemWidth(190 * scale);
                if (ImGui::BeginCombo("##item-category", Text(category.empty() ? "全部分类" : category.c_str())))
                {
                    if (ImGui::Selectable(WidgetLabel("全部分类"), category.empty()))
                        chooseCategory({});
                    for (const auto &value : itemCategories)
                        if (ImGui::Selectable(WidgetLabel(value), category == value))
                            chooseCategory(value);
                    ImGui::EndCombo();
                }
            }
            if (!snapshot.itemsReady)
            {
                ImGui::TextDisabled("%s", Text(snapshot.ready ? "正在加载物品列表…" : "进入游戏存档后加载物品列表。"));
                if (settings.debug)
                    ImGui::TextWrapped("%s", Text(snapshot.itemCatalogStatus));
            }
            const auto query = SearchText(search.data());
            // The pointers stay valid because derivedItems shares ownership of the rows.
            static std::vector<const gameplay::ItemEntry *> filtered;
            static std::string filteredQuery, filteredCategory;
            if (publicationChanged || filteredQuery != query || filteredCategory != category)
            {
                filtered.clear();
                for (const auto &item : derivedItems)
                    if (browsable(item) && (category.empty() || item.category == category) &&
                        (SearchText(item.name).find(query) != std::string::npos ||
                            (item.key == "resource:consciousness_dust" && std::string_view("意识粉尘").find(query) != std::string_view::npos) ||
                            (settings.debug && SearchText(item.key).find(query) != std::string::npos)))
                        filtered.push_back(&item);
                filteredQuery = query;
                filteredCategory = category;
            }
            constexpr std::size_t pageSize = 20;
            const auto pageCount = std::max<std::size_t>(1, (filtered.size() + pageSize - 1) / pageSize);
            page = std::min(page, pageCount - 1);
            const char *historyLabel = WidgetLabel("请求历史###item-history");
            const bool historyOpen = settings.debug && ImGui::GetStateStorage()->GetBool(ImGui::GetID(historyLabel), false);
            const float historyHeight = 132 * scale;
            const float lineReserve = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2;
            const float listHeight = std::max(110 * scale, ImGui::GetContentRegionAvail().y - lineReserve -
                (settings.debug ? lineReserve : 0) - (historyOpen ? historyHeight + ImGui::GetStyle().ItemSpacing.y : 0));
            bool openQuantity{};
            const auto selectItem = [&](const gameplay::ItemEntry &item)
            {
                itemSelection = {item.key, 1, snapshot.generation};
                openQuantity = true;
            };
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2 * scale));
            if (ImGui::BeginChild("items-grid", ImVec2(0, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                if (resetScroll)
                {
                    ImGui::SetScrollY(0);
                    resetScroll = false;
                }
                const int columns = std::clamp(static_cast<int>((ImGui::GetContentRegionAvail().x + 10 * scale) / (134 * scale)), 1, 8);
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5 * scale, 5 * scale));
                constexpr auto flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
                if (ImGui::BeginTable("item-tiles", columns, flags))
                {
                    const auto first = page * pageSize;
                    const auto last = std::min(filtered.size(), first + pageSize);
                    for (auto index = first; index < last; ++index)
                    {
                        if ((index - first) % columns == 0)
                            ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(static_cast<int>((index - first) % columns));
                        const auto &item = *filtered[index];
                        ImGui::PushID(item.key.c_str());
                        if (ItemTile(item, snapshot, ImGui::GetContentRegionAvail().x, scale))
                            selectItem(item);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();
                if (filtered.empty())
                {
                    ImGui::TextDisabled("%s", Text(snapshot.itemsReady ? "没有匹配的物品" : "物品列表尚未加载"));
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::BeginDisabled(page == 0);
            if (ImGui::Button(WidgetLabel("上一页"), ImVec2(80 * scale, 0)))
            {
                --page;
                resetScroll = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu / %zu", page + 1, pageCount);
            ImGui::SameLine();
            ImGui::BeginDisabled(page + 1 >= pageCount);
            if (ImGui::Button(WidgetLabel("下一页"), ImVec2(80 * scale, 0)))
            {
                ++page;
                resetScroll = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 18 * scale);
            ImGui::TextDisabled(Text("共 %zu 项 · 每页 %zu 项"), filtered.size(), pageSize);
            if (settings.debug && ImGui::CollapsingHeader(historyLabel))
                RequestHistory(snapshot, 2, historyHeight, scale, false);
            if (openQuantity)
                ImGui::OpenPopup(WidgetLabel("获取物品###item-quantity"));
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
            ImGui::SetNextWindowSize(ImVec2(360 * scale, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 18 * scale));
            if (ImGui::BeginPopupModal(WidgetLabel("获取物品###item-quantity"), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                const auto selected = std::find_if(snapshot.items.begin(), snapshot.items.end(),
                    [&](const auto &item) { return item.key == itemSelection.key; });
                if (selected == snapshot.items.end() || itemSelection.generation != snapshot.generation || itemSelection.key.empty())
                    ImGui::CloseCurrentPopup();
                else
                {
                    const auto &item = *selected;
                    ImGui::TextWrapped("%s", item.name.c_str());
                    if (item.owned >= 0)
                        ImGui::TextDisabled(Text("已持有：%lld"), static_cast<long long>(item.owned));
                    const int maximum = std::max(1, item.maxQuantity);
                    const bool unrestricted = maximum == gameplay::MaxItemQuantity;
                    if (unrestricted)
                        ImGui::TextDisabled("%s", Text("数量（至少 1）"));
                    else
                        ImGui::TextDisabled(Text("数量（1 ～ %d）"), maximum);
                    bool validQuantity = true;
                    ImGui::BeginDisabled(maximum == 1);
                    if (unrestricted)
                    {
                        const float buttonSize = ImGui::GetFrameHeight();
                        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * (buttonSize + spacing));
                        ImGui::InputText("##quantity", itemSelection.quantityText.data(), itemSelection.quantityText.size(),
                            ImGuiInputTextFlags_CharsDecimal);
                        validQuantity = gameplay::ParseItemQuantity(itemSelection.quantityText.data(), itemSelection.quantity);
                        const auto stepQuantity = [&](int step) {
                            itemSelection.quantity += step;
                            const auto result = std::to_chars(itemSelection.quantityText.data(),
                                itemSelection.quantityText.data() + itemSelection.quantityText.size() - 1, itemSelection.quantity);
                            *result.ptr = '\0';
                        };
                        ImGui::SameLine(0, spacing);
                        ImGui::PushButtonRepeat(true);
                        ImGui::BeginDisabled(!validQuantity || itemSelection.quantity <= 1);
                        if (ImGui::Button("-##quantity-minus", ImVec2(buttonSize, buttonSize)))
                            stepQuantity(ImGui::GetIO().KeyCtrl ? -std::min(10, itemSelection.quantity - 1) : -1);
                        ImGui::EndDisabled();
                        ImGui::SameLine(0, spacing);
                        ImGui::BeginDisabled(!validQuantity || itemSelection.quantity >= maximum);
                        if (ImGui::Button("+##quantity-plus", ImVec2(buttonSize, buttonSize)))
                            stepQuantity(ImGui::GetIO().KeyCtrl ? std::min(10, maximum - itemSelection.quantity) : 1);
                        ImGui::EndDisabled();
                        ImGui::PopButtonRepeat();
                    }
                    else
                    {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputInt("##quantity", &itemSelection.quantity, 1, 10))
                            itemSelection.quantity = std::clamp(itemSelection.quantity, 1, maximum);
                    }
                    ImGui::EndDisabled();
                    const bool busy = ItemPending(item.key);
                    const bool fitsInventory = !unrestricted || item.owned < 0 ||
                        gameplay::ItemQuantityFitsInventory(item.owned, itemSelection.quantity);
                    const bool canGrant = snapshot.ready && snapshot.itemsReady && item.available && item.maxQuantity > 0 && !busy &&
                        validQuantity && fitsInventory && itemSelection.quantity >= 1 && itemSelection.quantity <= maximum;
                    if (!canGrant)
                        ImGui::TextWrapped("%s", Text(busy ? "正在获取此物品，请稍候。" : !snapshot.ready ? "请先进入游戏存档。" :
                            !snapshot.itemsReady ? "物品列表正在加载，请稍候。" : !validQuantity ? "请输入有效的正整数。" :
                            !fitsInventory ? "当前数量加请求数量会溢出，未执行" : item.reason.empty() ? "此物品暂不可获取。" : item.reason.c_str()));
                    ImGui::Spacing();
                    if (ImGui::Button(WidgetLabel("取消"), ImVec2(140 * scale, 32 * scale)))
                    {
                        itemSelection = {};
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!canGrant);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(WidgetLabel("确定"), ImVec2(140 * scale, 32 * scale)))
                    {
                        const auto quantity = itemSelection.quantity;
                        const auto result = gameplay::GetBackend().SubmitItem(item.key, quantity);
                        submissionNotice = {true, result.accepted, result.requestId, "grant_item", result.message};
                        if (result.accepted)
                        {
                            itemReceipts.push_back({result.requestId, snapshot.generation, item.key, item.name, quantity, GetTickCount64()});
                            ShowFormattedToast("正在获取 {0} 个{1}…", item.name, result.message,
                                ToastTone::Info, result.requestId, quantity);
                        }
                        else
                            ShowFormattedToast("获取 {0} 个{1}失败", item.name, result.message, ToastTone::Failure, 0, quantity);
                        itemSelection = {};
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndDisabled();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        void SettingsPage(const gameplay::Snapshot &snapshot, float scale)
        {
            PageHeading("设置", "让界面更合你的习惯", scale);
            BeginCard("appearance-settings", ImVec2(0, 152 * scale), scale);
            Label("外观", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Text("主题"));
            ImGui::SameLine(112 * scale);
            if (ImGui::RadioButton(WidgetLabel("暗色"), !settings.light))
            {
                settings.light = false;
                dirty = true;
            }
            ImGui::SameLine(0, 36 * scale);
            if (ImGui::RadioButton(WidgetLabel("亮色"), settings.light))
            {
                settings.light = true;
                dirty = true;
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Text("界面缩放"));
            ImGui::SameLine(112 * scale);
            ImGui::SetNextItemWidth(std::min(300 * scale, ImGui::GetContentRegionAvail().x));
            if (ImGui::SliderFloat("##scale", &settings.scale, .8f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
            {
                dirty = true;
                forcePosition = true;
            }
            EndCard();
            BeginAutoCard("language-settings", scale);
            Label("界面语言", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            static std::string languageError;
            const auto setLanguage = [&](localization::Language language)
            {
                languageError.clear();
                localization::SetLanguage(language, &languageError);
            };
            if (ImGui::RadioButton("中文###language-chinese", localization::GetLanguage() == localization::Language::Chinese))
                setLanguage(localization::Language::Chinese);
            ImGui::SameLine(0, 36 * scale);
            if (ImGui::RadioButton("English###language-english", localization::GetLanguage() == localization::Language::English))
                setLanguage(localization::Language::English);
            if (!languageError.empty())
                ImGui::TextWrapped("%s", Text(languageError));
            EndCard();
            BeginAutoCard("layout-settings", scale);
            Label("菜单与布局", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::Spacing();
            auto& registry = hotkeys::GetRegistry();
            static std::string menuHotkeyError;
            ImGui::TextUnformatted(Text("界面开关按键"));
            const auto menuKeyLabel = hotkeys::ChordLabel(registry.MenuChord());
            const float resetKeyWidth = std::max(112 * scale,
                ImGui::CalcTextSize(Text("恢复默认")).x + ImGui::GetStyle().FramePadding.x * 2);
            const float keyWidth = std::min(std::max(160 * scale,
                ImGui::CalcTextSize(menuKeyLabel.c_str()).x + 28 * scale),
                ImGui::GetContentRegionAvail().x - resetKeyWidth - ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::Button(WidgetLabel(menuKeyLabel, "settings-menu-key"), ImVec2(keyWidth, 32 * scale)))
            {
                menuHotkeyError.clear();
                registry.BeginMenuCapture();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::SameLine();
            if (ImGui::Button(WidgetLabel("恢复默认###reset-menu-key"), ImVec2(resetKeyWidth, 32 * scale)))
            {
                menuHotkeyError.clear();
                registry.SetMenu({VK_INSERT, 0}, DataDirectory() / L"hotkeys.ini", menuHotkeyError);
            }
            MutedWrapped("点击按键框后按下组合键，Esc 取消；设置自动保存。");
            if (!menuHotkeyError.empty())
                ImGui::TextWrapped("%s", Text(menuHotkeyError));
            ImGui::Spacing();
            MutedWrapped("点击修改功能的快捷键列绑定；仅菜单隐藏时生效。");
            MutedWrapped("拖动顶部空白区域可移动窗口，设置自动保存。");
            if (ImGui::Button(WidgetLabel("重置布局"), ImVec2(124 * scale, 32 * scale)))
            {
                settings.scale = 1;
                settings.x = settings.y = -1;
                forcePosition = true;
                dirty = true;
            }
            EndCard();
            if (registry.Capture().action == hotkeys::MenuAction && !ImGui::IsPopupOpen(WidgetLabel("设置界面开关按键###menu-key-capture")))
                ImGui::OpenPopup(WidgetLabel("设置界面开关按键###menu-key-capture"));
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
            ImGui::SetNextWindowSize(ImVec2(440 * scale, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 20 * scale));
            if (ImGui::BeginPopupModal(WidgetLabel("设置界面开关按键###menu-key-capture"), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                const auto capture = registry.Capture();
                if (capture.action != hotkeys::MenuAction)
                    ImGui::CloseCurrentPopup();
                ImGui::TextWrapped("%s", Text("按下要使用的按键或组合键…"));
                MutedWrapped("支持 Ctrl / Shift / Alt，Esc 取消。");
                MutedWrapped("界面开关按键始终可用，不能留空。");
                if (!capture.message.empty() && capture.message != "按下界面开关键，可组合 Ctrl / Shift / Alt；Escape 取消。")
                    ImGui::TextWrapped("%s", Text(capture.message));
                ImGui::Spacing();
                if (ImGui::Button(WidgetLabel("取消###cancel-menu-key"), ImVec2(112 * scale, 32 * scale)))
                {
                    registry.CancelCapture();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            BeginAutoCard("debug-settings", scale);
            if (ImGui::Checkbox(WidgetLabel("Debug（调试信息）"), &settings.debug))
                dirty = true;
            MutedWrapped("显示实际读回、请求历史和诊断详情。");
            EndCard();
            BeginAutoCard("feature-memory", scale);
            Label("功能开关自动记忆", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            MutedWrapped("进入游戏后恢复已保存的开关设置；一次性操作不会重复执行。");
            EndCard();
            BeginAutoCard("update-settings", scale);
            Label("版本更新", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const auto updateState = update::GetSnapshot();
            const float updateWidth = std::max(112 * scale,
                ImGui::CalcTextSize(Text("检查更新")).x + ImGui::GetStyle().FramePadding.x * 2);
            ImGui::BeginDisabled(updateState.status == update::Status::Checking);
            if (ImGui::Button(WidgetLabel("检查更新"), ImVec2(updateWidth, 30 * scale)))
                update::CheckAsync();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(Text("当前版本 v%s"), Version);
            ImGui::TextWrapped("%s", Text(updateState.message));
            if (updateState.status == update::Status::UpdateAvailable && ImGui::TextLink(WidgetLabel("前往 GitHub 下载")))
                update::OpenReleasePage();
            EndCard();
            if (settings.debug)
            {
                if (ImGui::CollapsingHeader(WidgetLabel("实际读回")))
                    Statistics(snapshot, scale);
                if (ImGui::CollapsingHeader(WidgetLabel("请求历史")))
                    RequestHistory(snapshot, 0, 156 * scale, scale, false);
            }
        }
        void AboutPage(float scale)
        {
            Label("关于", 28, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::Dummy(ImVec2(0, 2 * scale));
            const auto state = update::GetSnapshot();
            BeginCard("about-version", ImVec2(0, 172 * scale), scale);
            Label("版本信息", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const float updateWidth = std::max(112 * scale,
                ImGui::CalcTextSize(Text("检查更新")).x + ImGui::GetStyle().FramePadding.x * 2);
            ImGui::SameLine(ImGui::GetWindowWidth() - 22 * scale - updateWidth);
            ImGui::BeginDisabled(state.status == update::Status::Checking);
            if (ImGui::Button(WidgetLabel("检查更新"), ImVec2(updateWidth, 30 * scale)))
                update::CheckAsync();
            ImGui::EndDisabled();
            ImGui::SetCursorPosY(62 * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
            if (ImGui::BeginTable("about-versions", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled(Text("当前版本"));
                const auto version = std::format("v{}", Version);
                Label(version.c_str(), 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TableNextColumn();
                ImGui::TextDisabled(Text("最新版本"));
                std::string_view latestVersion = state.latestVersion;
                constexpr std::string_view prefix = "PhantomBladeZero/";
                if (latestVersion.starts_with(prefix))
                    latestVersion.remove_prefix(prefix.size());
                const std::string latestLabel = latestVersion.empty() ? "—" : std::string(latestVersion);
                Label(latestLabel.c_str(), 24, accent);
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            ImGui::SetCursorPosY(134 * scale);
            ImGui::PushFont(nullptr, 13);
            ImGui::TextWrapped("%s", Text(state.message));
            ImGui::PopFont();
            EndCard();
            BeginCard("about-project", ImVec2(0, 272 * scale), scale);
            Label("项目信息", 20, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            static const char *failedLink{};
            if (ImGui::BeginTable("about-links", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                    (localization::GetLanguage() == localization::Language::English ? 240 : 190) * scale);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                const auto row = [&](const char *label, const char *value, const wchar_t *url = nullptr, const char *displayUrl = nullptr)
                {
                    ImGui::PushID(label);
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 32 * scale);
                    ImGui::TableNextColumn();
                    ImGui::PushFont(nullptr, 14);
                    MutedWrapped(label);
                    ImGui::PopFont();
                    ImGui::TableNextColumn();
                    if (url)
                    {
                        ImGui::PushStyleColor(ImGuiCol_TextLink, accent);
                        if (ImGui::TextLink(value))
                        {
                            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
                            failedLink = result > 32 ? nullptr : displayUrl;
                            if (result <= 32)
                                Log(std::format("About: could not open {} ({})", displayUrl, result));
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
                            ImGui::SetTooltip("%s", displayUrl);
                    }
                    else
                        ImGui::TextUnformatted(value);
                    ImGui::PopID();
                };
                row("作者", "小莫");
                row("开源地址", "github.com/GlossMod/GlossTrainer", L"https://github.com/GlossMod/GlossTrainer", "https://github.com/GlossMod/GlossTrainer");
                row("下载地址", "Releases", L"https://github.com/GlossMod/GlossTrainer/releases", "https://github.com/GlossMod/GlossTrainer/releases");
                row("LICENSE", "GNU GPL v3", L"https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE", "https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE");
                row("赞助作者", "sponsor.aoe.top", L"https://sponsor.aoe.top/", "https://sponsor.aoe.top/");
                row("申请制作游戏修改器", "glossmod.com/commissions", L"https://www.glossmod.com/commissions", "https://www.glossmod.com/commissions");
                ImGui::EndTable();
            }
            EndCard();
            if (failedLink)
            {
                ImGui::TextWrapped(Text("无法打开链接，请手动访问："));
                ImGui::TextWrapped("%s", failedLink);
            }
            if (state.status == update::Status::UpdateAvailable &&
                ImGui::Button(WidgetLabel("前往 GitHub 下载"), ImVec2(220 * scale, 36 * scale)))
                update::OpenReleasePage();
        }
    }

    bool Visible() { return visible.load(); }
    void SetVisible(bool show)
    {
        std::lock_guard lock(OverlayMutex());
        if (show && !rendererReady.load())
            return;
        if (visible.exchange(show) == show)
            return;
        gameplay::GetBackend().SetItemPageActive(show && settings.page == 2);
        gameplay::GetBackend().SetStatusWanted(show);
        input::SetMenuVisible(show);
        if (ImGui::GetCurrentContext())
        {
            ImGui::GetIO().ClearInputKeys();
            ImGui::GetIO().ClearInputMouse();
        }
        if (!show)
        {
            hotkeys::GetRegistry().CancelCapture();
            SaveSettings();
            // Window-thread menu toggles only request persistence; the rendering
            // thread flushes the shortcut file on its next frame, even when hidden.
            hotkeysSavedAt = 0;
            featuresSavedAt = 0;
        }
        Log(show ? "Menu opened" : "Menu hidden");
    }
    void SetRendererReady(bool ready)
    {
        rendererReady = ready;
        if (!ready)
            SetVisible(false);
    }
    void OnFocusLost()
    {
        std::lock_guard lock(OverlayMutex());
        hotkeys::GetRegistry().OnFocusLost();
        SetVisible(false);
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().AddFocusEvent(false);
    }
    bool Initialize(HWND window)
    {
        localization::Initialize();
        InitializeHotkeys();
        if (currentWindow == window && ImGui::GetCurrentContext())
            return true;
        if (!ImGui::GetCurrentContext())
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto &io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
            wchar_t windowsDirectory[MAX_PATH]{};
            GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
            auto font = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyh.ttc";
            if (!std::filesystem::exists(font))
                font = std::filesystem::path(windowsDirectory) / L"Fonts" / L"simhei.ttf";
            const auto utf8 = font.u8string();
            if (!std::filesystem::exists(font) || !io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char *>(utf8.c_str()), 16))
            {
                io.Fonts->AddFontDefault();
                Log("Chinese font unavailable; using default font");
            }
            const auto boldFont = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyhbd.ttc";
            if (std::filesystem::exists(boldFont))
            {
                const auto boldUtf8 = boldFont.u8string();
                headingFont = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char *>(boldUtf8.c_str()), 24);
            }
            LoadSettings();
            initializedAt = GetTickCount64();
        }
        else if (currentWindow)
        {
            SetVisible(false);
            ImGui_ImplWin32_Shutdown();
            input::Shutdown();
        }
        if (!ImGui_ImplWin32_Init(window))
        {
            Log("ImGui Win32 backend initialization failed");
            return false;
        }
        currentWindow = window;
        forcePosition = true;
        if (!input::Attach(window))
        {
            ImGui_ImplWin32_Shutdown();
            currentWindow = nullptr;
            Log("Window subclass failed; overlay disabled");
            return false;
        }
        ImGui::GetPlatformIO().Platform_SetImeDataFn = [](ImGuiContext *, ImGuiViewport *viewport, ImGuiPlatformImeData *data)
        {
            const auto target = static_cast<HWND>(viewport->PlatformHandleRaw);
            if (!target)
                return;
            input::ime::TextInput request;
            request.wantTextInput = data->WantTextInput;
            request.wantVisible = data->WantVisible;
            request.position = {static_cast<LONG>(data->InputPos.x - viewport->Pos.x),
                                static_cast<LONG>(data->InputPos.y - viewport->Pos.y)};
            request.lineHeight = static_cast<LONG>(data->InputLineHeight);
            input::ime::Request(target, request);
        };
        Log("ImGui initialized with PBZ base UI");
        return true;
    }
    void DrawFrame(const char *renderer)
    {
        if (!currentWindow)
            return;
        localization::BeginFrame();
        static std::atomic_flag updateStarted = ATOMIC_FLAG_INIT;
        if (!updateStarted.test_and_set())
            update::CheckAsync();
        auto &io = ImGui::GetIO();
        io.MouseDrawCursor = Visible();
        if (Visible())
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        const auto dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(currentWindow);
        RECT client{};
        GetClientRect(currentWindow, &client);
        const auto fit = std::min((client.right - client.left) / 1000.f, (client.bottom - client.top) / 720.f);
        const float scale = std::max(.5f, std::min(dpi * settings.scale, fit));
        if (previousScale != scale || previousLight != settings.light)
        {
            if (previousScale != scale)
                forcePosition = true;
            ApplyStyle(scale);
            previousScale = scale;
            previousLight = settings.light;
        }
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ui::BeginItemIconFrame();
        // Copying the model takes the lock the game thread needs. Revision is read
        // first, so a change during the copy is simply fetched again next frame.
        static gameplay::Snapshot snapshot;
        static std::uint64_t snapshotRevision{};
        if (const auto revision = gameplay::GetBackend().Revision(); revision != snapshotRevision)
        {
            snapshot = gameplay::GetBackend().GetSnapshot();
            snapshotRevision = revision;
        }
        ProcessHotkeys(snapshot);
        UpdateItemReceipts(snapshot);
        if (Visible())
        {
            const ImVec2 size(960 * scale, 680 * scale);
            ImGui::SetNextWindowSize(size);
            if (forcePosition)
            {
                ImVec2 position(settings.x, settings.y);
                if (position.x < 0 || position.y < 0)
                    position = ImVec2((io.DisplaySize.x - size.x) * .5f, (io.DisplaySize.y - size.y) * .5f);
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
            if (settings.x != origin.x || settings.y != origin.y)
            {
                settings.x = origin.x;
                settings.y = origin.y;
                dirty = true;
            }
            auto *draw = ImGui::GetWindowDrawList();
            const auto border = ImGui::GetColorU32(ImGuiCol_Border);
            draw->AddLine(ImVec2(origin.x, origin.y + 64 * scale), ImVec2(origin.x + size.x, origin.y + 64 * scale), border);
            draw->AddLine(ImVec2(origin.x, origin.y + 632 * scale), ImVec2(origin.x + size.x, origin.y + 632 * scale), border);
            draw->AddLine(ImVec2(origin.x + 160 * scale, origin.y + 64 * scale), ImVec2(origin.x + 160 * scale, origin.y + 632 * scale), border);
            ImGui::SetCursorPos(ImVec2(24 * scale, 18 * scale));
            Label("Gloss", 24, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SameLine(0, 0);
            Label("Trainer", 24, accent);
            ImGui::SameLine(0, 20 * scale);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0, 20 * scale);
            ImGui::TextUnformatted(Text("影之刃零"));
            ImGui::SameLine(0, 24 * scale);
            ImGui::BeginGroup();
            ImGui::TextDisabled("v%s", Version);
            if (update::HasUpdate())
            {
                const auto versionMin = ImGui::GetItemRectMin();
                const auto versionMax = ImGui::GetItemRectMax();
                ImGui::PushFont(nullptr, 10);
                const auto textSize = ImGui::CalcTextSize("new");
                const ImVec2 badgeMin(versionMax.x + 3 * scale, versionMin.y - 5 * scale);
                const ImVec2 badgeSize(textSize.x + 8 * scale, textSize.y + 2 * scale);
                draw->AddRectFilled(badgeMin, ImVec2(badgeMin.x + badgeSize.x, badgeMin.y + badgeSize.y),
                                    IM_COL32(215, 44, 60, 255), 5 * scale);
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(badgeMin.x + 4 * scale, badgeMin.y + scale), IM_COL32(255, 255, 255, 255), "new");
                ImGui::PopFont();
                // Reserve horizontal space so the superscript never overlaps the author.
                ImGui::SameLine(0, 3 * scale);
                ImGui::Dummy(ImVec2(badgeSize.x, versionMax.y - versionMin.y));
            }
            ImGui::EndGroup();
            ImGui::SameLine(0, 24 * scale);
            ImGui::TextDisabled(Text("By: 小莫"));
            ImGui::SetCursorPos(ImVec2(size.x - 48 * scale, 16 * scale));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, settings.light ? Color(222, 225, 232) : Color(43, 47, 55));
            const auto closePosition = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##close", ImVec2(32 * scale, 32 * scale)))
                SetVisible(false);
            ImGui::PopStyleColor(2);
            const auto closeColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            draw->AddLine(ImVec2(closePosition.x + 10 * scale, closePosition.y + 10 * scale),
                          ImVec2(closePosition.x + 22 * scale, closePosition.y + 22 * scale), closeColor, 1.5f * scale);
            draw->AddLine(ImVec2(closePosition.x + 22 * scale, closePosition.y + 10 * scale),
                          ImVec2(closePosition.x + 10 * scale, closePosition.y + 22 * scale), closeColor, 1.5f * scale);
            ImGui::SetCursorPos(ImVec2(8 * scale, 88 * scale));
            ImGui::BeginGroup();
            for (int index = 0; index < static_cast<int>(pages.size()); ++index)
            {
                if (index == 3)
                    continue;
                ImGui::PushID(index);
                if (NavigationItem(pages[index], index, index == settings.page, scale))
                    SelectPage(index);
                ImGui::PopID();
            }
            ImGui::EndGroup();
            ImGui::SetCursorPos(ImVec2(184 * scale, 88 * scale));
            ImGui::BeginChild("content", ImVec2(752 * scale, 520 * scale), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            gameplay::GetBackend().SetItemPageActive(Visible() && settings.page == 2);
            switch (settings.page)
            {
            case 0: Overview(renderer, snapshot, scale); break;
            case 1: FeaturePage(snapshot, scale); break;
            case 2: ItemsPage(snapshot, scale); break;
            case 3: CatalogPage(settings.page, snapshot, scale); break;
            case 4: SettingsPage(snapshot, scale); break;
            default: AboutPage(scale); break;
            }
            ImGui::EndChild();
            ImGui::SetCursorPos(ImVec2(24 * scale, 643 * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9 * scale, 4 * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            const auto menuKeyLabel = hotkeys::ChordLabel(hotkeys::GetRegistry().MenuChord());
            const float menuKeyWidth = std::max(66 * scale, ImGui::CalcTextSize(menuKeyLabel.c_str()).x + 18 * scale);
            ImGui::Button(WidgetLabel(menuKeyLabel, "menu-keycap"), ImVec2(menuKeyWidth, 26 * scale));
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::SameLine(0, 12 * scale);
            ImGui::TextDisabled(Text("显示或隐藏菜单"));
            ImGui::SetCursorPos(ImVec2(size.x - 104 * scale, 649 * scale));
            ImGui::TextDisabled(Text("动态适配"));
            ImGui::End();
        }
        else if (GetTickCount64() - initializedAt < 6500)
        {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, 36 * scale), ImGuiCond_Always, ImVec2(.5f, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * scale, 12 * scale));
            ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored(accent, "GlossTrainer");
            ImGui::SameLine();
            const auto menuKeyLabel = hotkeys::ChordLabel(hotkeys::GetRegistry().MenuChord());
            ImGui::Text(Text("UI 已加载  |  %s 打开菜单"), menuKeyLabel.c_str());
            ImGui::End();
            ImGui::PopStyleVar();
        }
        DrawItemToasts(scale);
        ImGui::Render();
        if (dirty && GetTickCount64() - savedAt > 1000 && !ImGui::IsMouseDown(0))
            SaveSettings();
        if (hotkeys::GetRegistry().Dirty() && GetTickCount64() - hotkeysSavedAt > 1000 && !ImGui::IsMouseDown(0))
            SaveHotkeys();
        if (GetTickCount64() - featuresSavedAt > 1000)
        {
            std::string error;
            if (!gameplay::GetBackend().FlushSettings(&error))
                Log("Feature settings: " + error);
            featuresSavedAt = GetTickCount64();
        }
    }
}
