#include "catalog.hpp"
#include "item_quantity.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <system_error>

namespace gloss::gameplay {
    namespace {
        ParamDef Number(std::string name, std::string label, std::string value, double minimum,
            double maximum, ParamType type = ParamType::Float) {
            return {std::move(name), std::move(label), type, std::move(value), minimum, maximum, 32};
        }
        ParamDef Name(std::string label = "现有 FName ID") {
            return {"id", std::move(label), ParamType::Name, "", 0, 0, 32};
        }
        bool UnsignedId(std::string_view value) {
            int base = 10;
            if (value.starts_with("0x") || value.starts_with("0X")) {
                value.remove_prefix(2);
                base = 16;
            }
            if (value.empty()) return false;
            std::uint32_t parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, base);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size();
        }
        bool NameId(std::string_view value) {
            const auto separator = value.find(':');
            return separator == std::string_view::npos ? UnsignedId(value) :
                UnsignedId(value.substr(0, separator)) && UnsignedId(value.substr(separator + 1));
        }
        struct ExtendedToggle {
            ToggleDef toggle;
            std::vector<ParamDef> parameters;
        };
        const std::vector<ExtendedToggle>& ExtendedToggles() {
            static const auto definitions = [] {
                std::vector<ExtendedToggle> result;
                const auto add = [&](std::string id, std::string title, std::string description,
                    int category, std::vector<ParamDef> parameters = {}) {
                    const auto on = id + "_on", off = id + "_off";
                    result.push_back({{std::move(id), std::move(title), std::move(description),
                        category, 1, on, off}, std::move(parameters)});
                };
                const auto value = [](std::string label, std::string initial, double low, double high) {
                    return std::vector<ParamDef>{Number("value", std::move(label), std::move(initial), low, high)};
                };
                add("infinite_health", "无限生命", "保护当前生命；关闭后恢复正常生命变化。", 1);
                add("infinite_pure_shachi", "无限纯杀气", "保护玩家纯杀气消耗；以游戏实际状态为准。", 2);
                add("invisible", "隐身", "调整敌人对玩家的感知；开关与感知效果需回读验证。", 1);
                add("weapon_switch_cooldown", "武器切换无冷却", "保护武器切换冷却路径；关闭后恢复正常冷却。", 2);
                add("instant_break", "瞬间破防", "修改实际破防路径；只在适配完成后可用。", 2);
                add("poise_damage", "姿态伤害倍率", "调整玩家造成的姿态伤害。", 2, value("姿态伤害倍率", "1", 0.1, 100));
                add("one_hit_kill", "一击必杀", "调整玩家攻击的伤害结算；效果需实际回读验证。", 2);
                add("damage", "伤害倍率", "调整玩家造成的伤害，关闭后恢复原始结算。", 2, value("伤害倍率", "1", 0.1, 100));
                add("defense", "防御倍率", "调整玩家防御结算，关闭后恢复原始结算。", 1, value("防御倍率", "1", 0.1, 100));
                add("shachi_cost", "杀气消耗倍率", "调整玩家杀气消耗。", 2, value("杀气消耗倍率", "1", 0, 10));
                add("pure_shachi_gain", "纯杀气获取倍率", "调整玩家纯杀气获取。", 2, value("纯杀气获取倍率", "1", 0, 100));
                add("infinite_medicine", "无限药品", "阻止药品使用次数降低；关闭后恢复正常消耗。", 1);
                add("infinite_items", "无限物品", "阻止可消耗物品扣减；不发放新物品。", 3);
                add("infinite_shop_stock", "无限商店库存", "保护商店库存消耗；需核对实际商店交易路径。", 3);
                add("move_speed", "移动速度倍率", "调整玩家移动速度；关闭后恢复原值。", 4, value("移动速度倍率", "1", 0.1, 5));
                add("player_speed", "玩家速度倍率", "调整玩家时间速度，不改变全局时间倍率。", 4, value("玩家速度倍率", "1", 0.1, 5));
                add("ai_speed", "AI 速度倍率", "调整 AI 时间速度；关闭后恢复原值。", 4, value("AI 速度倍率", "1", 0.1, 5));
                add("game_speed", "游戏速度倍率", "全局时间倍率，同时影响角色、AI 和世界时间。", 4, value("游戏速度倍率", "1", 0.1, 3));
                add("jump_height", "跳跃高度倍率", "调整玩家跳跃高度；关闭后恢复原值。", 4, value("跳跃高度倍率", "1", 0.1, 10));
                add("gravity", "重力倍率", "调整玩家重力；关闭后恢复原值。", 4, value("重力倍率", "1", 0, 5));
                add("fov", "视野范围", "调整游戏相机视野；关闭后恢复原值。", 5, value("视野角度", "90", 30, 150));
                add("flight", "飞行", "启用玩家飞行并指定速度；关闭后恢复原移动模式。", 4, value("飞行速度", "600", 50, 5000));
                return result;
            }();
            return definitions;
        }
    }

    const std::vector<FeatureDef>& Catalog() {
        static const auto features = [] {
            std::vector<FeatureDef> result;
            const auto add = [&](std::string id, std::string title, std::string description,
                int category, int page, std::string function, std::vector<ParamDef> params = {},
                bool persistent = false, std::vector<std::string> fixed = {}) {
                result.push_back({std::move(id), std::move(title), std::move(description), category, page,
                    std::move(params), std::move(function), std::move(fixed), persistent,
                    "等待适配器核对对象、参数签名与调用条件；效果尚未验证"});
            };
            add("get_health", "读取生命", "读取当前生命；只显示实际回读结果。", 1, 1, "GM_GetHealth");
            add("set_max_health", "设置最大生命值", "设置玩家最大生命值，同时恢复生命至实际生效的上限。", 1, 1, "Native_SetMaxHealth",
                {Number("health", "最大生命值", "1000", 1, 100000000)});
            add("full_heal", "恢复生命", "恢复生命并比较当前生命与上限；读回一致后显示已验证。", 1, 1, "GM_FullHeal");
            for (const bool enabled : {true, false}) {
                add(enabled ? "infinite_dark_source_on" : "infinite_dark_source_off", "无限暗源", "开启时补满暗源并保护消耗；关闭后恢复正常扣减，不改变上限。", 2, 1,
                    "Native_InfiniteDarkSource", {}, false, {enabled ? "on" : "off"});
                add(enabled ? "infinite_shachi_on" : "infinite_shachi_off", "无限杀气", "阻止本地玩家杀气降低，关闭后恢复正常扣减。", 2, 1,
                    "Native_InfiniteShachi", {}, false, {enabled ? "on" : "off"});
                add(enabled ? "perfect_deflect_on" : "perfect_deflect_off", "自动完美格挡判定", "启用游戏自带的完美格挡判定标签；关闭时仅撤销本修改器添加的状态。", 2, 1,
                    "Native_PerfectDeflect", {}, false, {enabled ? "on" : "off"});
                add(enabled ? "perfect_dodge_on" : "perfect_dodge_off", "自动完美闪避", "读取真实来袭帧，在命中前按游戏规则尝试完美闪避。", 2, 1,
                    "Native_PerfectDodge", {}, false, {enabled ? "on" : "off"});
            }
            for (const auto& state : std::vector<std::pair<std::string, std::string>>{
                {"invulnerable", "无敌"}}) {
                for (const bool enabled : {true, false}) {
                    add("state_" + state.first + (enabled ? "_on" : "_off"),
                        state.second + (enabled ? "：开启" : "：关闭"),
                        "提交明确的状态操作；开关结果、死亡与换图后的行为需回读验证。", 1, 1,
                        "GM_State", {}, false, {"preset", state.first, enabled ? "on" : "off"});
                }
            }
            add("refill_medicine", "补满药品", "将已装备药品补满至当前上限（含能力加成）；并修复旧版写入的异常上限。", 1, 1, "Native_RefillMedicine", {}, true);
            add("refill_ultimate", "终结技大量补给", "提高背包终结技次数和上限至 2147083648 并充满；可能写入进度，不能按普通补满撤销。", 2, 1, "GM_InfiniteUltimateShachi", {}, true);
            add("grant_item", "获取物品", "从物品目录选择并获取指定数量。", 3, 2, "Native_GrantItem",
                {{"item_key", "物品", ParamType::String, "", 0, 0, 256},
                 Number("quantity", "数量", "1", 1, MaxItemQuantity, ParamType::Int)}, true);
            add("fill_materials", "材料补满", "材料范围、上限和存档结果待验证。", 3, 2, "GM_FillAllMaterilas", {}, true);
            for (const auto& item : std::vector<std::pair<std::string, std::string>>{
                {"medicine", "药品"}, {"accessory", "饰品"}}) {
                add("give_" + item.first, "发放指定" + item.second,
                "使用游戏中已存在的 FName ID；入口没有数量参数，实际发放数量与保存行为待验证。", 3, 2,
                    item.first == "medicine" ? "GM_GiveMedicine" : "GM_GiveAccessory",
                    {Name(item.second + " FName ID")}, true);
            }
            const auto upgradeLevel = [] {
                return Number("level", "提升级数", "1", 1, 999, ParamType::Int);
            };
            add("upgrade_main_weapon", "升级主武器", "已验证提升级数 +2 可使主武器 1→3；等级上限及材料消耗待确认。", 3, 2,
                "UpgradeMainWeapon", {Name(), upgradeLevel()}, true);
            add("max_main_weapon", "主武器升满", "单项满级候选；只对已核对 ID 调用。", 3, 2, "UpgradeMainWeaponMax", {Name()}, true);
            add("max_all_main_weapons", "全部主武器升满", "批量进度操作，拥有条件与保存行为待验证。", 3, 2, "UpgradeAllMainWeaponsMax", {}, true);
            add("upgrade_shachi_weapon", "升级 ShaChi 武器", "参数表示提升级数；ShaChi 武器效果与材料消耗待验证。", 3, 2,
                "UpgradeShaChiWeapon", {Name(), upgradeLevel()}, true);
            add("max_shachi_weapon", "ShaChi 武器升满", "单项满级候选。", 3, 2, "UpgradeShaChiWeaponMax", {Name()}, true);
            add("max_all_shachi_weapons", "全部 ShaChi 武器升满", "批量进度操作，效果与保存行为待验证。", 3, 2, "UpgradeAllShaChiWeaponsMax", {}, true);
            add("upgrade_medicine", "升级药品", "增加已拥有药品等级；已验证单项 +1，等级上限和保存行为待验证。", 3, 2,
                "UpgradeMedicine", {Name(), upgradeLevel()}, true);
            add("max_medicine", "药品升满", "单项满级候选。", 3, 2, "UpgradeMedicineMax", {Name()}, true);
            add("max_all_medicines", "全部药品升满", "批量进度操作，效果与保存行为待验证。", 3, 2, "UpgradeAllMedicinesMax", {}, true);
            add("upgrade_accessory", "升级饰品", "增加已拥有饰品等级；已验证单项 +1，等级上限和保存行为待验证。", 3, 2,
                "UpgradeAccessory", {Name(), upgradeLevel()}, true);
            add("activate_talent", "激活指定天赋", "前置节点、点数扣除、重复调用和保存行为待验证。", 3, 2, "GM_ActivateTalent", {Name("天赋 FName ID")}, true);
            add("activate_all_talents", "激活全部天赋", "批量进度操作，不能保证可撤销。", 3, 2, "GM_ActivateAllTalent", {}, true);
            add("discover_enemies", "发现敌人图鉴", "新增图鉴发现，覆盖范围与保存行为待验证。", 5, 2, "GM_DiscoverAllEnemies", {}, true);
            add("get_location", "读取坐标", "只显示游戏实际返回的当前位置。", 4, 3, "GM_GetPlayerLocation");
            add("save_position", "记录位置", "保存槽名称与写入范围待验证。", 4, 3, "GM_SavePosition",
                {{"slot_name", "位置槽名称", ParamType::String, "GlossTrainer", 0, 0, 64}}, true);
            add("move_to", "移动到坐标", "坐标使用 X,Y,Z 格式；可能启动异步移动任务，模式、超时和到达结果待验证。", 4, 3, "GM_MoveTo",
                {{"coordinates", "坐标（X,Y,Z）", ParamType::String, "0,0,0", 0, 0, 64},
                 Number("acceptance_radius", "到达半径", "100", 0, 100000),
                 Number("timeout_seconds", "超时秒数", "30", 0, 86400),
                 {"mode", "移动模式（auto/nav/direct）", ParamType::String, "auto", 0, 0, 32}});
            add("stop_move", "停止移动", "停止 GM 移动任务的候选入口。", 4, 3, "GM_StopMove");
            add("is_move_complete", "查询移动任务", "回读移动任务状态，需核对返回值。", 4, 3, "GM_IsMoveToComplete");
            add("travel", "跨地图移动", "目标坐标、朝向、地图名称及任务、加载、保存约束均待验证。", 5, 3, "GM_Travel",
                {{"coordinates", "目标坐标（X,Y,Z）", ParamType::String, "", 0, 0, 64},
                 {"rotation", "目标朝向", ParamType::String, "0,0,0", 0, 0, 64},
                 {"map_name", "目标地图名称", ParamType::String, "", 0, 0, 128}}, true);
            add("reload_map", "当前位置重载地图", "重载与保存行为未验证。", 5, 3, "GM_ReloadMapAtPosition", {}, true);
            add("is_travel_complete", "查询跨地图任务", "回读跨地图加载任务状态。", 5, 3, "GM_IsTravelComplete");
            add("set_cycle", "设置周目", "周目参数与进度影响未验证。", 5, 3, "GM_SetCycleCount",
                {Number("cycle", "周目", "1", 0, 999, ParamType::Int)}, true);
            for (const auto& definition : ExtendedToggles()) {
                for (const bool enabled : {true, false}) {
                    const auto& toggle = definition.toggle;
                    add(enabled ? toggle.onFeatureId : toggle.offFeatureId, toggle.title,
                        toggle.description, toggle.category, toggle.page, "Native_ExtendedFeature",
                        enabled ? definition.parameters : std::vector<ParamDef>{}, false,
                        {toggle.id, enabled ? "on" : "off"});
                }
            }
            add("remaining_days", "设置剩余天数", "修改剩余天数；可能写入存档，执行后不会自动重放。", 5, 1,
                "Native_ExtendedAction", {Number("value", "剩余天数", "66", 0, 66, ParamType::Int)}, true, {"remaining_days"});
            add("unlock_66_days", "解锁六十六天难度", "解锁六十六天难度选项；执行后不会自动重放。", 5, 1,
                "Native_ExtendedAction", {}, true, {"unlock_66_days"});
            for (const bool up : {true, false}) {
                const std::string id = up ? "flight_up" : "flight_down";
                add(id, up ? "飞行上升" : "飞行下降", "按指定距离调整飞行高度。", 4, 1,
                    "Native_ExtendedAction", {Number("value", "移动距离", "100", 1, 10000)}, false, {id});
            }
            // Page zero keeps legacy request IDs available without duplicate UI entries.
            add("game_speed", "游戏速度", "兼容旧调用；使用游戏速度开关管理自动恢复。", 4, 0,
                "SetGlobalTimeDilation", {Number("speed", "游戏速度", "1", 0.1, 3)});
            add("money", "设置金币", "缺少可验证的玩家属性或调用路径。", 3, 0, "");
            return result;
        }();
        return features;
    }

    const FeatureDef* FindFeature(std::string_view id) {
        const auto& features = Catalog();
        const auto found = std::find_if(features.begin(), features.end(), [&](const auto& feature) { return feature.id == id; });
        return found == features.end() ? nullptr : &*found;
    }

    const std::vector<ToggleDef>& ToggleCatalog() {
        static const auto toggles = [] {
            std::vector<ToggleDef> result{
            {"infinite_dark_source", "无限暗源", "补满并锁定械兵使用的暗源；不改变暗源上限。", 2, 1,
                "infinite_dark_source_on", "infinite_dark_source_off"},
            {"infinite_shachi", "无限杀气", "锁定当前杀气，允许自然恢复；不改变杀气上限。", 2, 1,
                "infinite_shachi_on", "infinite_shachi_off"},
            {"perfect_deflect", "自动完美格挡判定", "使用游戏的完美格挡判定状态；需处于可格挡动作。", 2, 1,
                "perfect_deflect_on", "perfect_deflect_off"},
            {"perfect_dodge", "自动完美闪避", "根据真实来袭帧自动尝试完美闪避；保留游戏的动作与目标检查。", 2, 1,
                "perfect_dodge_on", "perfect_dodge_off"},
            {"invulnerable", "无敌模式", "能力系统无敌标签；状态根据游戏实时回读。", 1, 1,
                "state_invulnerable_on", "state_invulnerable_off"}
            };
            for (const auto& definition : ExtendedToggles()) result.push_back(definition.toggle);
            return result;
        }();
        return toggles;
    }

    const std::vector<ParamDef>& ToggleParameters(const ToggleDef& toggle) {
        static const std::vector<ParamDef> empty;
        const auto* feature = FindFeature(toggle.onFeatureId);
        return feature ? feature->params : empty;
    }

    std::vector<std::string> DefaultParameters(const FeatureDef& feature) {
        std::vector<std::string> values;
        for (const auto& param : feature.params) values.push_back(param.defaultValue);
        return values;
    }

    bool EquivalentParameters(const FeatureDef& feature, const std::vector<std::string>& left,
        const std::vector<std::string>& right) {
        std::string error;
        if (!ValidateParameters(feature, left, error) || !ValidateParameters(feature, right, error)) return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (left[i] == right[i]) continue;
            if (feature.params[i].type == ParamType::Float || feature.params[i].type == ParamType::Int) {
                double a{}, b{};
                std::from_chars(left[i].data(), left[i].data() + left[i].size(), a);
                std::from_chars(right[i].data(), right[i].data() + right[i].size(), b);
                if (std::abs(a - b) <= 0.000001 * std::max({1.0, std::abs(a), std::abs(b)})) continue;
            }
            return false;
        }
        return true;
    }

    bool ValidateParameters(const FeatureDef& feature, const std::vector<std::string>& values, std::string& error) {
        error.clear();
        if (values.size() != feature.params.size()) {
            error = "参数数量与功能定义不符";
            return false;
        }
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto& param = feature.params[index];
            const auto& value = values[index];
            bool valid = !value.empty() && value.size() <= param.maxLength &&
                std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
            if (valid && param.type == ParamType::Float) {
                double parsed{};
                const auto converted = std::from_chars(value.data(), value.data() + value.size(), parsed);
                valid = converted.ec == std::errc{} && converted.ptr == value.data() + value.size() &&
                    std::isfinite(parsed) && parsed >= param.minimum && parsed <= param.maximum;
            } else if (valid && param.type == ParamType::Int) {
                std::int64_t parsed{};
                const auto converted = std::from_chars(value.data(), value.data() + value.size(), parsed);
                valid = converted.ec == std::errc{} && converted.ptr == value.data() + value.size() &&
                    static_cast<double>(parsed) >= param.minimum && static_cast<double>(parsed) <= param.maximum;
            } else if (valid && param.type == ParamType::Name) {
                valid = NameId(value);
            } else if (valid && param.type == ParamType::Bool) {
                valid = value == "true" || value == "false" || value == "0" || value == "1";
            }
            if (!valid) {
                error = param.label + "：参数为空、格式错误或超出允许范围";
                return false;
            }
        }
        return true;
    }
}
