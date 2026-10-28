#include "features.hpp"
#include "feature_settings.hpp"
#include "runtime.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <exception>
#include <format>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace gloss::features
{
    namespace
    {
        using G = Group;
        using K = Kind;
        constexpr std::array<Definition, Count> definitions{{{Id::GodMode, G::Survival, K::Toggle, "无敌模式／无视伤害判定", "忽略受击伤害判定。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteHealth, G::Survival, K::Toggle, "无限生命", "维持当前角色生命。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteMana, G::Abilities, K::Toggle, "无限法力", "维持当前角色法力。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteStamina, G::Survival, K::Toggle, "无限气力", "维持当前角色气力。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteFocus, G::Abilities, K::Toggle, "无限棍势", "维持棍势资源。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteGourdUses, G::Survival, K::Toggle, "无限葫芦使用次数", "使用葫芦时保留可用次数。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::SpellCooldown, G::Abilities, K::Toggle, "法术瞬间冷却", "缩短法术冷却等待。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::TransformationCooldown, G::Abilities, K::Toggle, "变身瞬间冷却", "缩短变身冷却等待。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::TransformationDuration, G::Abilities, K::Toggle, "无限变身时间", "维持变身持续资源。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteSpiritEnergy, G::Abilities, K::Toggle, "无限化身技元气", "维持化身技元气。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteVesselEnergy, G::Abilities, K::Toggle, "无限法宝元气", "维持法宝元气。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::MaximumCriticalChance, G::Combat, K::Toggle, "最高暴击率", "提高角色暴击触发概率。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::MaximumPoise, G::Combat, K::Toggle, "最高韧性", "提高角色抗打断能力。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::PreventAbnormalStatus, G::Survival, K::Toggle, "防止异常状态", "抑制寒冻、火焚、毒伤与雷蛰等异常状态。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::WillMultiplier, G::Resources, K::ToggleValue, "灵蕴倍率", "调整新获得灵蕴的倍率。", "", 0, 0, 2, 0, 1000, .1, false},
                                                             {Id::ExperienceMultiplier, G::Resources, K::ToggleValue, "经验倍率", "调整新获得经验的倍率。", "", 0, 0, 2, 0, 1000, .1, false},
                                                             {Id::RelicPoints, G::Resources, K::ToggleValue, "编辑根器点数", "设置根器点数；启用前确认目标数值。", "", 0, 0, 3, 0, 999, 1, true},
                                                             {Id::GuaranteedDrop, G::Resources, K::Toggle, "100% 掉宝率", "提高可掉落物品的掉落概率。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::OneHitKill, G::Combat, K::Toggle, "超级伤害／一击必杀", "显著提高对敌伤害。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::DamageMultiplier, G::Combat, K::ToggleValue, "伤害倍率", "调整对敌伤害倍率。", "", 0, 0, 2, 0, 1000, .1, false},
                                                             {Id::DefenseMultiplier, G::Combat, K::ToggleValue, "防御倍率", "调整防御效果倍率。", "", 0, 0, 2, 0, 1000, .1, false},
                                                             {Id::StaminaConsumption, G::Survival, K::ToggleValue, "气力消耗倍率", "调整气力消耗倍率。", "", 0, 0, .5, 0, 1000, .1, false},
                                                             {Id::GameSpeed, G::Movement, K::ToggleValue, "设置游戏速度", "调整进程计时倍率，同时影响世界和界面动画。", "", 0, 0, 2.5, .1, 10, .1, false},
                                                             {Id::EditWill, G::Resources, K::Action, "编辑灵蕴", "点击应用写入灵蕴；编辑输入框本身不会修改游戏。", "", 0, 0, 9999999, 0, 999999999, 1000, true},
                                                             {Id::EditSelectedItemCount, G::Resources, K::Action, "编辑点击物品数量", "应用后在游戏背包中点击目标物品，30秒后未触发则取消。", "", 0, 0, 999, 0, 999999, 1, true},
                                                             {Id::AddExperience, G::Resources, K::Action, "增加经验值", "点击应用增加指定经验值。", "", 0, 0, 50000, 0, 999999999, 1000, true},
                                                             {Id::EditSparks, G::Resources, K::Action, "编辑灵光点", "点击应用设置灵光点。", "", 0, 0, 999, 0, 999999, 1, true},
                                                             {Id::PlayerSpeed, G::Movement, K::ToggleValue, "设置玩家速度", "调整玩家动作速度倍率。", "", 0, 0, 2, .1, 10, .1, false},
                                                             {Id::MovementSpeed, G::Movement, K::ToggleValue, "设置移动速度", "调整玩家移动速度倍率。", "", 0, 0, 2, .1, 10, .1, false},
                                                             {Id::JumpHeight, G::Movement, K::ToggleValue, "设置跳跃高度", "调整玩家跳跃高度倍率。", "", 0, 0, 2, .1, 20, .1, false},
                                                             {Id::MaximumHealth, G::Attributes, K::ToggleValue, "编辑生命上限", "设置角色生命上限。", "", 0, 0, 1000, 1, 9999999, 10, true},
                                                             {Id::MaximumMana, G::Attributes, K::ToggleValue, "编辑法力上限", "设置角色法力上限。", "", 0, 0, 1000, 1, 9999999, 10, true},
                                                             {Id::MaximumStamina, G::Attributes, K::ToggleValue, "编辑气力上限", "设置角色气力上限。", "", 0, 0, 1000, 1, 9999999, 10, true},
                                                             {Id::Attack, G::Attributes, K::ToggleValue, "编辑攻击", "设置角色攻击属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::Defense, G::Attributes, K::ToggleValue, "编辑防御", "设置角色防御属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::StaminaRecovery, G::Attributes, K::ToggleValue, "编辑气力恢复", "设置角色气力恢复属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::CriticalChance, G::Attributes, K::ToggleValue, "编辑暴击率", "设置角色暴击率，单位为百分比。", "", 0, 0, 100, 0, 100, 1, true},
                                                             {Id::CriticalDamage, G::Attributes, K::ToggleValue, "增加暴击伤害", "设置额外暴击伤害属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::DamageBonus, G::Attributes, K::ToggleValue, "编辑伤害加成", "设置角色伤害加成属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::DamageReduction, G::Attributes, K::ToggleValue, "编辑伤害减免", "设置角色伤害减免百分比。", "", 0, 0, 50, 0, 100, 1, true},
                                                             {Id::ChillResistance, G::Resistance, K::ToggleValue, "编辑寒冻耐性", "设置寒冻耐性属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::BurnResistance, G::Resistance, K::ToggleValue, "编辑火焚耐性", "设置火焚耐性属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::PoisonResistance, G::Resistance, K::ToggleValue, "编辑毒伤耐性", "设置毒伤耐性属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::ShockResistance, G::Resistance, K::ToggleValue, "编辑雷蛰耐性", "设置雷蛰耐性属性。", "", 0, 0, 999, 0, 9999999, 10, true},
                                                             {Id::InfiniteItems, G::Resources, K::Toggle, "物品不减", "保留消耗物品的数量。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::Stealth, G::Survival, K::Toggle, "潜行不遇敌", "使角色不被通常敌人感知。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::BossStealth, G::Survival, K::Toggle, "不遇敌（含首领）", "使角色无法成为敌人的选定目标，可能影响战斗流程。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::NoHitCollision, G::Survival, K::Toggle, "受击不被碰撞", "免疫受击硬直与抓投，不改变地面碰撞。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteBuffDuration, G::Abilities, K::Toggle, "丹药效果无限时间", "保持启用后使用的丹药增益剩余时间；关闭后继续计时。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::InfiniteVesselDuration, G::Abilities, K::Toggle, "无限法宝时间", "保持法宝效果持续时间。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::BulletTime, G::World, K::ToggleValue, "子弹时间", "调整世界时间倍率，菜单保持正常速度。", "", 0, 0, .25, .01, 1, .05, false},
                                                             {Id::HideHud, G::Camera, K::Toggle, "隐藏游戏界面", "隐藏游戏 HUD 与游戏菜单，关闭后恢复已记录的显示状态。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::HidePlayer, G::Camera, K::Toggle, "隐藏人物", "隐藏当前玩家角色，保留镜头与碰撞。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::PauseBackground, G::World, K::Toggle, "后台时暂停", "失去前台焦点时暂停，返回时恢复此前暂停状态。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::FreezeWorld, G::World, K::Toggle, "凝滞画面", "暂停世界；菜单仍可操作。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::ContinuousJump, G::Movement, K::Toggle, "连续跳跃", "在空中再次按空格继续跳跃；每次按下触发一次。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::NoGravity, G::Movement, K::Toggle, "消除重力", "将角色移动组件的重力倍率设为零。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::FreeCamera, G::Camera, K::Toggle, "自由视角", "切换自由观察镜头。", "", 0, 0, 0, 0, 0, 0, false},
                                                             {Id::FixedCameraDistance, G::Camera, K::ToggleValue, "固定视距", "固定角色镜头距离。", "", 0, 0, 500, 50, 3000, 25, false},
                                                             {Id::SummonCloud, G::Movement, K::Action, "召唤／收起筋斗云", "切换筋斗云，遵守当前场景和技能的可用条件。", "", 0, 0, 0, 0, 0, 1, true},
                                                             {Id::UnlockCloud, G::Unlocks, K::Action, "解锁筋斗云", "向当前存档解锁筋斗云。", "", 0, 0, 0, 0, 0, 1, true},
                                                             {Id::UnlockShrines, G::Unlocks, K::Action, "解锁所有土地庙", "向当前存档解锁可传送土地庙。", "", 0, 0, 0, 0, 0, 1, true},
                                                             {Id::UnlockBossRush, G::Unlocks, K::Action, "挑战敌人全解锁", "向当前存档解锁挑战敌人。", "", 0, 0, 0, 0, 0, 1, true},
                                                             {Id::NewGameCycle, G::Unlocks, K::Action, "设置周目", "设置当前存档的周目数。", "", 0, 0, 2, 1, 100, 1, true}}};
        constexpr bool CatalogIsValid()
        {
            for (std::size_t index = 0; index < definitions.size(); ++index)
            {
                const auto &item = definitions[index];
                if (static_cast<std::size_t>(item.id) != index)
                    return false;
                if (item.kind != K::Toggle && (item.minimum > item.initialValue || item.initialValue > item.maximum || item.step <= 0))
                    return false;
                for (std::size_t other = index + 1; other < definitions.size(); ++other)
                    if (item.virtualKey && item.virtualKey == definitions[other].virtualKey && item.modifiers == definitions[other].modifiers)
                        return false;
            }
            return true;
        }
        static_assert(CatalogIsValid(), "Feature IDs, values and shortcuts must be valid and unique");
        struct EditorState
        {
            double value;
            std::string message;
            bool actionFailed{};
        };
        std::array<EditorState, Count> MakeEditorState()
        {
            std::array<EditorState, Count> result{};
            for (std::size_t index = 0; index < Count; ++index)
                result[index].value = definitions[index].initialValue;
            return result;
        }
        std::array<EditorState, Count> editors = MakeEditorState();
        std::mutex modelMutex;
        // Serialize backend writes with an update transition. Backend callbacks may
        // read the model, but must not recursively start another write transaction.
        std::mutex operationMutex;
        std::atomic<bool> updateRequired{};
        std::atomic<std::uint64_t> updateGeneration{};
        Backend backend;
        bool hotkeysEnabled = true;
        std::array<std::pair<unsigned, unsigned>, Count> bindings{};
        int capturing = -1;
        std::pair<unsigned, unsigned> menuBinding{VK_INSERT, NoModifier};
        bool capturingMenu{};
        Result menuHotkeyResult{true, {}};
        std::recursive_mutex hotkeyFileMutex;
        std::function<std::string(unsigned, unsigned)> externalHotkeyConflict;
        // Lock order: operationMutex -> settingsFileMutex -> modelMutex. No code
        // holds modelMutex during backend calls, disk I/O, or logging. Completion
        // callbacks never acquire operationMutex (they can run on the game thread).
        std::mutex settingsFileMutex;
        bool settingsInitialized{};
        std::uint64_t settingsRevision{}, persistedSettingsRevision{};
        ULONGLONG nextSaveAttempt{};
        settings::Data savedSettings;
        enum class RestorePhase { None, Waiting, InFlight, Cleanup };
        struct RestoreState
        {
            RestorePhase phase{};
            ULONGLONG nextAttempt{};
            unsigned cleanupAttempts{};
        };
        struct PendingWrite
        {
            bool active{}, restoring{}, enabled{}, cancelled{};
            double value{};
        };
        std::array<RestoreState, Count> restorations{};
        std::array<PendingWrite, Count> pendingWrites{};
        std::size_t restoreCursor{};
        Result Record(Id id, Result result);
        void LogResult(Id id, const Result &result)
        {
            Log(std::format("Feature {}: {}{}", static_cast<unsigned>(id), result.success ? "success" : "failed", result.message.empty() ? "" : ": " + result.message));
        }
        void SaveSettings()
        {
            std::lock_guard fileLock(settingsFileMutex);
            settings::Data data;
            std::uint64_t revision{};
            {
                std::lock_guard lock(modelMutex);
                if (!settingsInitialized || settingsRevision == persistedSettingsRevision) return;
                data = savedSettings;
                revision = settingsRevision;
                nextSaveAttempt = GetTickCount64() + 5000;
            }
            try {
                if (settings::Save(DataDirectory() / L"features.ini", data)) {
                    std::lock_guard lock(modelMutex);
                    persistedSettingsRevision = revision;
                    return;
                }
            }
            catch (...) {}
            Log("Feature settings save failed; original features.ini retained");
        }
        void CancelRestoration(std::size_t index, bool cancelUserWrite = false)
        {
            auto &restore = restorations[index];
            auto &pending = pendingWrites[index];
            if (pending.active && pending.enabled && (pending.restoring || cancelUserWrite)) {
                pending.cancelled = true;
                restore.phase = RestorePhase::Cleanup;
                restore.nextAttempt = 0;
            }
            else if (restore.phase != RestorePhase::Cleanup)
                restore.phase = RestorePhase::None;
        }
        // Called with modelMutex. The request is installed BEFORE invoking the
        // backend, so an unusually fast CompleteAction cannot lose its result.
        bool FinishWrite(std::size_t index, const Result &result, bool completion)
        {
            auto &pending = pendingWrites[index];
            if (!pending.active) return false;
            if (!completion && result.success && result.pending) return false;
            const auto finished = pending;
            pending = {};
            auto &restore = restorations[index];
            if (finished.cancelled) {
                restore.phase = RestorePhase::Cleanup;
                restore.nextAttempt = 0;
                return false;
            }
            if (finished.restoring) {
                if (!finished.enabled) {
                    // Cancellation compensation is bounded as well. A failed
                    // shutdown is reported, never converted back into an enable.
                    if (!result.success && !result.retryable) ++restore.cleanupAttempts;
                    restore.phase = !result.success && (result.retryable || restore.cleanupAttempts < 5) ? RestorePhase::Cleanup : RestorePhase::None;
                }
                else restore.phase = !result.success && result.retryable ? RestorePhase::Waiting : RestorePhase::None;
                restore.nextAttempt = GetTickCount64() + 1000;
                return false; // Runtime failure never overwrites saved startup intent.
            }
            if (!result.success) return false;
            auto &saved = savedSettings.features[index];
            saved.enabled = finished.enabled;
            if (finished.enabled) saved.appliedValue = finished.value;
            ++settingsRevision;
            return true;
        }
        Result SubmitWrite(const Backend &current, Id id, bool enabled, double value, bool restoring)
        {
            const auto index = static_cast<std::size_t>(id);
            {
                std::lock_guard lock(modelMutex);
                if (settingsInitialized) {
                    if (pendingWrites[index].active) return {false, "上一项操作尚未完成", false, true};
                    pendingWrites[index] = {true, restoring, enabled, false, value};
                }
            }
            Result result;
            try { result = current.setEnabled(id, enabled, value); }
            catch (...) { result = {false, "切换功能失败；请查看日志"}; }
            bool save{}, record{};
            {
                std::lock_guard lock(modelMutex);
                record = !settingsInitialized || pendingWrites[index].active;
                save = FinishWrite(index, result, false);
                if (record) {
                    editors[index].message = result.message;
                    editors[index].actionFailed = !result.success;
                }
            }
            if (save) SaveSettings();
            // Completion has already recorded a more precise result. Do not let
            // the caller replace it with a late "queued" acknowledgement.
            if (record) LogResult(id, result);
            return result;
        }
        std::string ExternalHotkeyConflict(unsigned key, unsigned modifiers)
        {
            std::function<std::string(unsigned, unsigned)> probe;
            { std::lock_guard lock(modelMutex); probe = externalHotkeyConflict; }
            return key && probe ? probe(key, modifiers) : std::string{};
        }
        std::string HotkeyLabel(unsigned key, unsigned modifiers)
        {
            if (!key)
                return {};
            std::string result;
            if (modifiers & Ctrl)
                result += "Ctrl+";
            if (modifiers & Shift)
                result += "Shift+";
            if (modifiers & Alt)
                result += "Alt+";
            switch (key)
            {
            case VK_LBUTTON: return result + "鼠标左键";
            case VK_RBUTTON: return result + "鼠标右键";
            case VK_MBUTTON: return result + "鼠标中键";
            case VK_XBUTTON1: return result + "鼠标侧键 1";
            case VK_XBUTTON2: return result + "鼠标侧键 2";
            }
            wchar_t name[64]{};
            auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
            if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_DELETE || key == VK_INSERT || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT || key == VK_DIVIDE || key == VK_NUMLOCK)
                scan |= 0x100;
            if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0)
            {
                char text[256]{};
                WideCharToMultiByte(CP_UTF8, 0, name, -1, text, 256, nullptr, nullptr);
                result += text;
            }
            else
                result += std::format("VK {}", key);
            return result;
        }
        bool ModifierKey(unsigned key)
        {
            return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
                   key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT || key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
                   key == VK_LWIN || key == VK_RWIN;
        }
        bool Bindable(unsigned key, unsigned modifiers)
        {
            const bool mouse = key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2;
            return key < 0xff && (!key || mouse || key >= VK_BACK) && !(modifiers & ~7u) && !ModifierKey(key) && key != VK_ESCAPE &&
                   key != VK_PROCESSKEY && key != VK_PACKET && !(key == VK_HOME && modifiers == (Ctrl | Shift));
        }
        bool ParseHotkey(const wchar_t *text, unsigned &key, unsigned &modifiers)
        {
            wchar_t *end{};
            errno = 0;
            const auto parsedKey = std::wcstoul(text, &end, 10);
            if (end == text || errno == ERANGE || parsedKey >= 256 || *end != L',')
                return false;
            const auto *modifierText = end + 1;
            errno = 0;
            const auto parsedModifiers = std::wcstoul(modifierText, &end, 10);
            if (end == modifierText || errno == ERANGE || parsedModifiers > 7 || *end)
                return false;
            key = static_cast<unsigned>(parsedKey);
            modifiers = static_cast<unsigned>(parsedModifiers);
            return true;
        }
        Result RecordMenuHotkey(Result result)
        {
            {
                std::lock_guard lock(modelMutex);
                menuHotkeyResult = result;
            }
            Log(std::format("Menu hotkey: {}{}", result.success ? "success" : "failed", result.message.empty() ? "" : ": " + result.message));
            return result;
        }
        std::size_t Index(Id id)
        {
            const auto index = static_cast<std::size_t>(id);
            if (index >= Count)
                throw std::out_of_range("Invalid feature ID");
            return index;
        }
        Backend GetBackend()
        {
            std::lock_guard lock(modelMutex);
            return backend;
        }
        BackendState Query(const Backend &current, Id id)
        {
            if (!current.query)
                return {Availability::Unavailable, false, "尚未连接游戏功能后端"};
            try
            {
                return current.query(id);
            }
            catch (...)
            {
                return {Availability::Error, false, "读取功能状态失败"};
            }
        }
        Result Record(Id id, Result result)
        {
            {
                std::lock_guard lock(modelMutex);
                editors[Index(id)].message = result.message;
                editors[Index(id)].actionFailed = !result.success;
            }
            LogResult(id, result);
            return result;
        }
        Result Unavailable(Id id, const BackendState &state)
        {
            return Record(id, {false, state.message.empty() ? "当前功能不可用" : state.message});
        }
        void DisableEnabledFeatures(const Backend &current)
        {
            if (!current.setEnabled)
                return;
            for (const auto &definition : definitions)
            {
                if (definition.kind == K::Action || !Query(current, definition.id).enabled)
                    continue;
                double value{};
                {
                    std::lock_guard lock(modelMutex);
                    value = editors[Index(definition.id)].value;
                }
                try
                {
                    Record(definition.id, current.setEnabled(definition.id, false, value));
                }
                catch (...)
                {
                    Record(definition.id, {false, "关闭功能失败；请查看日志"});
                }
            }
        }
    }
    void RegisterBackend(Backend value)
    {
        std::lock_guard operationLock(operationMutex);
        {
            std::lock_guard lock(modelMutex);
            backend = std::move(value);
            for (auto &editor : editors)
            {
                editor.message.clear();
                editor.actionFailed = false;
            }
        }
        if (updateRequired.load())
            DisableEnabledFeatures(GetBackend());
    }
    void InitializeSettings()
    {
        std::lock_guard operationLock(operationMutex);
        std::lock_guard fileLock(settingsFileMutex);
        auto loaded = settings::Defaults();
        bool valid{};
        try { valid = settings::Load(DataDirectory() / L"features.ini", loaded); }
        catch (...) {}
        {
            std::lock_guard lock(modelMutex);
            savedSettings = loaded;
            hotkeysEnabled = loaded.hotkeysEnabled;
            settingsInitialized = true;
            settingsRevision = persistedSettingsRevision = 0;
            nextSaveAttempt = 0;
            restorations = {};
            pendingWrites = {};
            restoreCursor = 0;
            for (std::size_t i = 0; i < Count; ++i) {
                editors[i].value = loaded.features[i].value;
                editors[i].message.clear();
                editors[i].actionFailed = false;
                if (loaded.features[i].enabled && !updateRequired.load()) restorations[i].phase = RestorePhase::Waiting;
            }
        }
        Log(valid ? "Feature settings loaded" : "Feature settings missing or invalid; using defaults");
    }
    void RestoreSettings()
    {
        std::lock_guard operationLock(operationMutex);
        const auto now = GetTickCount64();
        bool save{};
        {
            std::lock_guard lock(modelMutex);
            if (!settingsInitialized) return;
            save = settingsRevision != persistedSettingsRevision && now >= nextSaveAttempt;
        }
        if (save) SaveSettings();
        const auto current = GetBackend();
        if (!current.setEnabled) return;
        // Fairly examine every feature; synchronous restores can all finish in
        // one poll, while a single-slot asynchronous backend applies one at a time.
        const auto first = restoreCursor++ % Count;
        for (std::size_t offset = 0; offset < Count; ++offset) {
            const auto index = (first + offset) % Count;
            const auto id = definitions[index].id;
            bool cleanup{};
            double value{};
            {
                std::lock_guard lock(modelMutex);
                auto &restore = restorations[index];
                cleanup = restore.phase == RestorePhase::Cleanup;
                if ((!cleanup && (restore.phase != RestorePhase::Waiting || updateRequired.load())) ||
                    pendingWrites[index].active || now < restore.nextAttempt) continue;
                value = savedSettings.features[index].appliedValue;
            }
            // Query is observational; not-ready features keep their intent for
            // a future player capture or engine initialization.
            if (!cleanup && Query(current, id).availability != Availability::Ready) continue;
            {
                std::lock_guard lock(modelMutex);
                auto &restore = restorations[index];
                restore.phase = cleanup ? RestorePhase::Cleanup : RestorePhase::InFlight;
                restore.nextAttempt = now + 1000;
            }
            const auto result = SubmitWrite(current, id, !cleanup, value, true);
            if (result.pending) break; // Do not compete with the accepted queue slot.
        }
    }
    void SetUpdateRequired(bool required)
    {
        std::lock_guard operationLock(operationMutex);
        if (updateRequired.exchange(required) == required)
            return;
        if (required)
        {
            updateGeneration.fetch_add(1);
            {
                std::lock_guard lock(modelMutex);
                for (std::size_t i = 0; i < Count; ++i) CancelRestoration(i, true);
            }
            DisableEnabledFeatures(GetBackend());
        }
    }
    bool UpdateRequired() { return updateRequired.load(); }
    std::uint64_t UpdateGeneration() { return updateGeneration.load(); }
    std::span<const Definition> Definitions() { return definitions; }
    const Definition &GetDefinition(Id id) { return definitions[Index(id)]; }
    const char *GroupName(Group group)
    {
        constexpr const char *names[] = {"生存", "法术", "战斗", "资源", "移动", "属性", "抗性", "世界", "镜头", "解锁"};
        const auto index = static_cast<std::size_t>(group);
        return index < std::size(names) ? names[index] : "全部";
    }
    Snapshot GetSnapshot(Id id)
    {
        const auto index = Index(id);
        const auto current = GetBackend();
        const auto state = Query(current, id);
        std::lock_guard lock(modelMutex);
        const auto &editor = editors[index];
        return {state.availability, state.enabled, editor.value,
                editor.actionFailed || state.message.empty() ? editor.message : state.message, editor.actionFailed,
                restorations[index].phase == RestorePhase::Waiting || restorations[index].phase == RestorePhase::InFlight};
    }
    bool SetValue(Id id, double value)
    {
        std::lock_guard operationLock(operationMutex);
        const auto &definition = GetDefinition(id);
        if (definition.kind == K::Toggle || !std::isfinite(value))
            return false;
        value = std::clamp(value, definition.minimum, definition.maximum);
        if (definition.integer)
            value = std::round(value);
        {
            std::lock_guard lock(modelMutex);
            const auto index = Index(id);
            editors[index].value = value;
            editors[index].message.clear();
            editors[index].actionFailed = false;
            CancelRestoration(index);
            if (settingsInitialized) {
                savedSettings.features[index].value = value;
                ++settingsRevision;
            }
        }
        SaveSettings();
        return true;
    }
    Result SetEnabled(Id id, bool enabled)
    {
        std::lock_guard operationLock(operationMutex);
        const auto &definition = GetDefinition(id);
        if (enabled && UpdateRequired())
            return Record(id, {false, "请下载最新版本后使用"});
        if (definition.kind == K::Action)
            return Record(id, {false, "该功能需要点击应用"});
        bool cancelledIntent{}, stillPending{}, savedEnabled{};
        {
            std::lock_guard lock(modelMutex);
            const auto index = Index(id);
            // Cancelling a saved switch is a valid local operation even before
            // the engine is ready. An accepted old enable is compensated later.
            savedEnabled = settingsInitialized && savedSettings.features[index].enabled;
            cancelledIntent = settingsInitialized && !enabled &&
                (restorations[index].phase != RestorePhase::None ||
                 (pendingWrites[index].active && pendingWrites[index].enabled));
            stillPending = pendingWrites[index].active;
            if (cancelledIntent) {
                CancelRestoration(index, true);
                savedSettings.features[index].enabled = false;
                ++settingsRevision;
            }
            else if (!stillPending) {
                CancelRestoration(index);
                // A new explicit enable supersedes an old compensation request
                // once its previous asynchronous write has finished.
                if (enabled) restorations[index] = {};
            }
        }
        if (cancelledIntent) SaveSettings();
        if (stillPending) {
            if (cancelledIntent) return Record(id, {true, "已关闭"});
            return Record(id, {false, "上一项操作尚未完成", false, true});
        }
        const auto current = GetBackend();
        const auto state = Query(current, id);
        if (cancelledIntent && !state.enabled) return Record(id, {true, "已关闭"});
        if (!enabled && savedEnabled && !state.enabled) {
            {
                std::lock_guard lock(modelMutex);
                savedSettings.features[Index(id)].enabled = false;
                ++settingsRevision;
            }
            SaveSettings();
            return Record(id, {true, "已关闭"});
        }
        // A previously enabled feature must remain switchable off even if the target disappeared.
        const bool canDisable = !enabled && (state.enabled || UpdateRequired()) && static_cast<bool>(current.setEnabled);
        if (!canDisable && (state.availability != Availability::Ready || !current.setEnabled))
            return Unavailable(id, state);
        double value{};
        {
            std::lock_guard lock(modelMutex);
            value = editors[Index(id)].value;
        }
        return SubmitWrite(current, id, enabled, value, false);
    }
    Result Apply(Id id)
    {
        std::lock_guard operationLock(operationMutex);
        const auto &definition = GetDefinition(id);
        if (UpdateRequired())
            return Record(id, {false, "请下载最新版本后使用"});
        if (definition.kind == K::Toggle)
            return Record(id, {false, "该功能不需要参数"});
        if (definition.kind == K::ToggleValue) {
            bool busy{};
            {
                std::lock_guard lock(modelMutex);
                const auto index = Index(id);
                busy = pendingWrites[index].active;
                if (!busy) {
                    CancelRestoration(index);
                    restorations[index] = {}; // An explicit apply supersedes old cancellation cleanup.
                }
            }
            if (busy) return Record(id, {false, "上一项操作尚未完成", false, true});
        }
        const auto current = GetBackend();
        const auto state = Query(current, id);
        if (state.availability != Availability::Ready)
            return Unavailable(id, state);
        double value{};
        {
            std::lock_guard lock(modelMutex);
            value = editors[Index(id)].value;
        }
        try
        {
            if (definition.kind == K::ToggleValue)
            {
                if (!state.enabled)
                    return Record(id, {false, "请先启用功能，再应用新参数"});
                if (current.setEnabled)
                    return SubmitWrite(current, id, true, value, false);
            }
            else if (current.apply)
                return Record(id, current.apply(id, value));
            return Record(id, {false, "此功能尚无应用处理程序"});
        }
        catch (...)
        {
            return Record(id, {false, "应用参数失败；请查看日志"});
        }
    }
    bool HandleHotkey(unsigned virtualKey, unsigned modifiers)
    {
        std::unique_lock captureLock(hotkeyFileMutex);
        int recording{};
        bool recordingMenu{};
        {
            std::lock_guard lock(modelMutex);
            recording = capturing;
            recordingMenu = capturingMenu;
        }
        if (recordingMenu)
        {
            if (virtualKey == VK_ESCAPE)
            {
                CancelHotkeyCapture();
                RecordMenuHotkey({true, "已取消菜单快捷键录制"});
                return true;
            }
            if (ModifierKey(virtualKey) || !virtualKey)
                return true;
            if (SetMenuHotkey(virtualKey, modifiers).success)
                CancelHotkeyCapture();
            return true;
        }
        if (recording >= 0)
        {
            if (virtualKey == VK_ESCAPE)
            {
                CancelHotkeyCapture();
                return true;
            }
            if (virtualKey == VK_BACK || virtualKey == VK_DELETE)
            {
                SetHotkey(static_cast<Id>(recording), 0, 0);
                CancelHotkeyCapture();
                return true;
            }
            if (!ModifierKey(virtualKey) && virtualKey)
            {
                if (SetHotkey(static_cast<Id>(recording), virtualKey, modifiers).success)
                    CancelHotkeyCapture();
            }
            return true;
        }
        captureLock.unlock();
        if (virtualKey == VK_HOME && modifiers == (Ctrl | Shift))
        {
            SetHotkeysEnabled(!HotkeysEnabled());
            return true;
        }
        const bool enabled = HotkeysEnabled();
        if (!enabled && !UpdateRequired())
            return false;
        std::array<std::pair<unsigned, unsigned>, Count> shortcuts;
        {
            std::lock_guard lock(modelMutex);
            shortcuts = bindings;
        }
        for (const auto &definition : definitions)
        {
            const auto [key, mods] = shortcuts[static_cast<std::size_t>(definition.id)];
            if (!key || key != virtualKey || mods != modifiers)
                continue;
            if (UpdateRequired() || !enabled)
                return true;
            if (definition.kind == K::Action)
                Apply(definition.id);
            else
            {
                const auto state = GetSnapshot(definition.id);
                SetEnabled(definition.id, !(state.enabled || state.restorePending));
            }
            return true;
        }
        return false;
    }
    bool HotkeysEnabled()
    {
        std::lock_guard lock(modelMutex);
        return hotkeysEnabled;
    }
    void SetHotkeysEnabled(bool enabled)
    {
        {
            std::lock_guard lock(modelMutex);
            hotkeysEnabled = enabled;
            if (settingsInitialized) {
                savedSettings.hotkeysEnabled = enabled;
                ++settingsRevision;
            }
        }
        SaveSettings();
        Log(enabled ? "Feature hotkeys enabled" : "Feature hotkeys disabled");
    }
    void InitializeHotkeys()
    {
        std::lock_guard fileLock(hotkeyFileMutex);
        std::array<std::pair<unsigned, unsigned>, Count> loaded{};
        const auto file = DataDirectory() / L"hotkeys.ini";
        std::pair<unsigned, unsigned> loadedMenu{VK_INSERT, NoModifier};
        wchar_t menuValue[64]{};
        GetPrivateProfileStringW(L"Menu", L"Toggle", L"", menuValue, 64, file.c_str());
        unsigned menuKey{}, menuModifiers{};
        if (ParseHotkey(menuValue, menuKey, menuModifiers) && menuKey && Bindable(menuKey, menuModifiers))
            loadedMenu = {menuKey, menuModifiers};
        for (std::size_t i = 0; i < Count; ++i)
        {
            const auto keyName = std::format(L"{}", i);
            wchar_t value[64]{};
            GetPrivateProfileStringW(L"Bindings", keyName.c_str(), L"", value, 64, file.c_str());
            unsigned key{}, mods{};
            if (!ParseHotkey(value, key, mods) || !Bindable(key, mods) || (key && loadedMenu == std::pair{key, mods}))
                continue;
            bool duplicate{};
            for (std::size_t j = 0; j < i; ++j)
                duplicate |= key && loaded[j] == std::pair{key, mods};
            if (!duplicate)
                loaded[i] = {key, key ? mods : 0};
        }
        std::lock_guard lock(modelMutex);
        bindings = loaded;
        menuBinding = loadedMenu;
        capturing = -1;
        capturingMenu = false;
        menuHotkeyResult = {true, {}};
    }
    Hotkey GetHotkey(Id id)
    {
        std::lock_guard lock(modelMutex);
        const auto [key, mods] = bindings[Index(id)];
        return {key, mods, HotkeyLabel(key, mods)};
    }
    Result SetHotkey(Id id, unsigned key, unsigned modifiers)
    {
        if (!Bindable(key, modifiers))
            return Record(id, {false, "此按键由菜单或系统保留，请重新设置"});
        if (!key)
            modifiers = 0;
        std::lock_guard fileLock(hotkeyFileMutex);
        std::string conflict;
        {
            std::lock_guard lock(modelMutex);
            if (key && menuBinding == std::pair{key, modifiers})
                conflict = "界面开关";
            for (std::size_t i = 0; conflict.empty() && i < Count; ++i)
                if (i != Index(id) && key && bindings[i] == std::pair{key, modifiers})
                {
                    conflict = definitions[i].name;
                    break;
                }
        }
        if (!conflict.empty())
            return Record(id, {false, "快捷键已被「" + conflict + "」使用"});
        if (auto external = ExternalHotkeyConflict(key, modifiers); !external.empty())
            return Record(id, {false, "快捷键已被「" + external + "」使用"});
        try
        {
            std::filesystem::create_directories(DataDirectory());
            const auto name = std::format(L"{}", Index(id)), value = std::format(L"{},{}", key, modifiers);
            if (!WritePrivateProfileStringW(L"Bindings", name.c_str(), value.c_str(), (DataDirectory() / L"hotkeys.ini").c_str()))
                return Record(id, {false, "快捷键保存失败"});
        }
        catch (...)
        {
            return Record(id, {false, "快捷键保存失败"});
        }
        {
            std::lock_guard lock(modelMutex);
            bindings[Index(id)] = {key, modifiers};
        }
        return Record(id, {true, key ? "快捷键已保存" : "快捷键已清除"});
    }
    void BeginHotkeyCapture(Id id)
    {
        std::lock_guard fileLock(hotkeyFileMutex);
        std::lock_guard lock(modelMutex);
        const auto index = Index(id);
        capturing = static_cast<int>(index);
        capturingMenu = false;
        editors[index].message.clear();
        editors[index].actionFailed = false;
    }
    void CancelHotkeyCapture()
    {
        std::lock_guard fileLock(hotkeyFileMutex);
        std::lock_guard lock(modelMutex);
        capturing = -1;
        capturingMenu = false;
    }
    bool CapturingHotkey(Id id)
    {
        std::lock_guard lock(modelMutex);
        return capturing == static_cast<int>(Index(id));
    }
    bool AnyHotkeyCapture()
    {
        std::lock_guard lock(modelMutex);
        return capturing >= 0 || capturingMenu;
    }
    Hotkey GetMenuHotkey()
    {
        std::lock_guard lock(modelMutex);
        const auto [key, modifiers] = menuBinding;
        return {key, modifiers, HotkeyLabel(key, modifiers)};
    }
    Result SetMenuHotkey(unsigned key, unsigned modifiers)
    {
        if (!key)
            return RecordMenuHotkey({false, "界面开关快捷键不能为空"});
        if (!Bindable(key, modifiers))
            return RecordMenuHotkey({false, "此按键由系统保留，请重新设置"});
        std::lock_guard fileLock(hotkeyFileMutex);
        std::string conflict;
        {
            std::lock_guard lock(modelMutex);
            for (std::size_t index = 0; index < Count; ++index)
                if (bindings[index] == std::pair{key, modifiers})
                {
                    conflict = definitions[index].name;
                    break;
                }
        }
        if (!conflict.empty())
            return RecordMenuHotkey({false, "快捷键已被「" + conflict + "」使用"});
        if (auto external = ExternalHotkeyConflict(key, modifiers); !external.empty())
            return RecordMenuHotkey({false, "快捷键已被「" + external + "」使用"});
        try
        {
            std::filesystem::create_directories(DataDirectory());
            const auto value = std::format(L"{},{}", key, modifiers);
            if (!WritePrivateProfileStringW(L"Menu", L"Toggle", value.c_str(), (DataDirectory() / L"hotkeys.ini").c_str()))
                return RecordMenuHotkey({false, "菜单快捷键保存失败"});
        }
        catch (...)
        {
            return RecordMenuHotkey({false, "菜单快捷键保存失败"});
        }
        {
            std::lock_guard lock(modelMutex);
            menuBinding = {key, modifiers};
        }
        return RecordMenuHotkey({true, "菜单快捷键已保存"});
    }
    void BeginMenuHotkeyCapture()
    {
        std::lock_guard fileLock(hotkeyFileMutex);
        std::lock_guard lock(modelMutex);
        capturing = -1;
        capturingMenu = true;
        menuHotkeyResult = {true, {}};
    }
    bool CapturingMenuHotkey()
    {
        std::lock_guard lock(modelMutex);
        return capturingMenu;
    }
    Result GetMenuHotkeyResult()
    {
        std::lock_guard lock(modelMutex);
        return menuHotkeyResult;
    }
    bool IsMenuHotkey(unsigned key, unsigned modifiers)
    {
        std::lock_guard lock(modelMutex);
        return key && menuBinding == std::pair{key, modifiers};
    }
    Result CheckHotkeyConflict(unsigned key, unsigned modifiers)
    {
        if (!Bindable(key, modifiers)) return {false, "此按键或组合键由系统保留"};
        if (!key) return {true, {}};
        std::lock_guard lock(modelMutex);
        if (menuBinding == std::pair{key, modifiers}) return {false, "该组合键已被界面开关使用"};
        for (std::size_t i = 0; i < Count; ++i)
            if (bindings[i] == std::pair{key, modifiers}) return {false, "该组合键已被「" + std::string(definitions[i].name) + "」使用"};
        return {true, {}};
    }
    void RegisterHotkeyConflictProbe(std::function<std::string(unsigned, unsigned)> probe)
    {
        std::lock_guard lock(modelMutex);
        externalHotkeyConflict = std::move(probe);
    }
    Result RunHotkeyTransaction(std::function<Result()> operation)
    {
        std::lock_guard lock(hotkeyFileMutex);
        return operation();
    }
    void CompleteAction(Id id, Result result)
    {
        bool save{};
        {
            std::lock_guard lock(modelMutex);
            const auto index = Index(id);
            save = FinishWrite(index, result, true);
            editors[index].message = result.message;
            editors[index].actionFailed = !result.success;
        }
        LogResult(id, result);
        if (save) SaveSettings();
    }
}
