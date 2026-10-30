#include "../src/localization.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace gloss {
    const std::filesystem::path& DataDirectory() {
        static const auto path = std::filesystem::absolute("output/pbz-basics-20261029/localization-default");
        return path;
    }
}
namespace {
    using namespace gloss::localization;
    int failures{};
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)
    std::string Read(const std::filesystem::path& file) {
        std::ifstream stream(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), {}};
    }
    void Write(const std::filesystem::path& file, std::string_view text) {
        std::ofstream stream(file, std::ios::binary);
        stream << text;
    }
    bool HasChinese(std::string_view text) {
        for (std::size_t i = 0; i + 2 < text.size(); ++i) {
            const auto a = static_cast<unsigned char>(text[i]);
            const auto b = static_cast<unsigned char>(text[i + 1]);
            if ((a == 0xe4 && b >= 0xb8) || (a >= 0xe5 && a <= 0xe9)) return true;
        }
        return false;
    }
    void TestTranslationAndPointerLifetime(const std::filesystem::path& file) {
        Initialize(file);
        CHECK(GetLanguage() == Language::Chinese);
        CHECK(Translate("关于") == "关于");
        const auto* before = Text(std::string("关于"));
        std::string error;
        CHECK(SetLanguage(Language::English, &error));
        CHECK(error.empty());
        CHECK(std::string(before) == "关于");
        CHECK(Translate("关于") == "About");
        const auto* english = Text(std::string("关于"));
        for (int i = 0; i < 1000; ++i) Text("unknown " + std::to_string(i));
        CHECK(std::string(english) == "About");
        for (const auto* key : {"概览", "修改功能", "物品", "设置", "关于", "界面语言", "检查更新", "版本信息",
            "功能开关自动记忆", "无限暗源", "无限杀气", "自动完美格挡判定", "自动完美闪避", "无敌模式",
            "暂无正式发布版本", "检查更新失败", "语言设置保存失败，已保留原设置",
            "界面开关按键", "设置界面开关按键", "恢复默认",
            "数量（至少 1）", "请输入有效的正整数。", "发放数量必须为有效的正整数",
            "当前数量加请求数量会溢出，未执行",
            "点击按键框后按下组合键，Esc 取消；设置自动保存。",
            "按下要使用的按键或组合键…", "支持 Ctrl / Shift / Alt，Esc 取消。",
            "界面开关按键始终可用，不能留空。", "界面开关快捷键不能为空。",
            "此快捷键已用于界面开关。", "按下界面开关键，可组合 Ctrl / Shift / Alt；Escape 取消。"}) {
            if (HasChinese(Translate(key))) { std::cerr << "Missing translation: " << key << '\n'; ++failures; }
        }
        for (const auto* key : {"无限生命", "无限纯杀气", "隐身", "武器切换无冷却", "瞬间破防",
            "姿态伤害倍率", "一击必杀", "伤害倍率", "防御倍率", "杀气消耗倍率", "纯杀气获取倍率",
            "无限药品", "无限物品", "无限商店库存", "移动速度倍率", "玩家速度倍率",
            "AI 速度倍率", "游戏速度倍率", "跳跃高度倍率", "重力倍率", "视野范围", "视野角度",
            "飞行", "飞行穿墙", "飞行速度", "设置剩余天数", "剩余天数", "解锁 66 天", "飞行上升", "飞行下降", "移动距离",
            "开启后点击应用更新参数。", "本地玩家参数实时回读", "移动开关参数错误",
            "解锁六十六天难度", "解锁六十六天难度选项；执行后不会自动重放。",
            "等待本地玩家战斗属性核对", "战斗结算开关参数错误", "战斗倍率必须为0.1至100",
            "战斗结算入口和参数已核对；实际攻击效果仍需实战验证",
            "上次移动写入或恢复未通过回读，状态不确定", "飞行状态或恢复结果未通过实际回读",
            "本地武器组件、槽位或冷却查询契约未通过核对", "武器冷却查询入口或钩子拥有者已变化",
            "武器冷却开关参数错误", "已停止武器冷却查询覆盖；游戏计时数据保持原值",
            "武器冷却查询钩子安装或对象核对失败", "本地武器公共与槽位冷却查询已覆盖；实际切换仍需实战验证",
            "武器组件身份已变化，冷却查询覆盖已停止", "已覆盖 {} 次本地武器冷却查询；未改写计时或天赋",
            "仅保护已蓄纯杀气段数；充能满额转为段数保持游戏规则", "已停止生命扣减保护",
            "已停止纯杀气段数扣减保护；充能转换保持游戏规则", "已恢复纯杀气获取倍率启用前原值并核对实际回读",
            "已按实际段数上限补满并保护纯杀气段数；充能进度和满额转换保持游戏规则",
            "已按实际生命上限补满并启用本地生命扣减保护；未改变上限",
            "已调用一次资源补满，但实际回读未匹配上限；未启用新的扣减保护",
            "当前玩家未启用纯杀气获取倍率修改", "本地玩家资源属性、实际数值或扣减入口未通过契约核对",
            "本地玩家资源属性或扣减入口已变化，未启用", "本地生命扣减保护；生命与上限来自实际回读",
            "玩家资源属性身份变化，未写入旧原值", "纯杀气获取倍率属性不可读，未执行",
            "纯杀气获取倍率或计算结果无效", "纯杀气获取倍率未全部匹配实际回读；已尝试恢复编辑前原值",
            "纯杀气获取倍率格式错误", "纯杀气获取倍率的基础值与实际值均已回读",
            "纯杀气获取倍率被游戏覆盖、写入未确认或玩家对象变化",
            "补满后玩家资源对象已变化，未启用新的扣减保护", "资源保护绑定或钩子所有权已变化",
            "资源功能定义无效", "资源开关参数无效", "资源补满调用异常，未启用新的扣减保护",
            "本地能力列表、主动杀气消耗蓝图或 ActorInfo 契约尚未通过核对",
            "杀气消耗 VM 入口或钩子所有权已变化", "杀气消耗绑定已变化或输出写入未确认；自动覆盖已停止",
            "已停止主动技能消耗结果覆盖；技能与杀气原始数据保持游戏值",
            "主动杀气消耗钩子或当前玩家绑定失败，未提交新倍率",
            "主动技能蓝图消耗输出覆盖已安装；受击削杀气不变，原生风雷转换尚未覆盖，需实战核验",
            "已调整 {} 次主动技能蓝图消耗；受击不变，原生风雷转换未覆盖，需实战核验", "杀气消耗倍率格式错误",
            "剩余天数写入后回读不符", "剩余天数写入失败", "剩余天数参数或字段无效", "剩余天数字段未通过核对",
            "玩家剩余天数回读 {}；由游戏正常保存进度", "玩家剩余天数字段已核对；存档由游戏正常保存",
            "五项感知忽略标签各新增一份并已回读；敌人识别和仇恨效果需实战验证",
            "前次感知标签贡献尚未清理，未重复添加",
            "已移除本修改器添加的感知标签贡献，保留游戏原有标签；敌人感知需实测",
            "感知忽略标签仍在游戏能力系统中；敌人实战效果需另行验证",
            "感知忽略标签未全部回读，或玩家能力系统身份已变化", "感知忽略标签计数未通过实际回读",
            "感知标签基础计数无法确认，已回滚本次新增贡献",
            "感知标签新增未全部通过回读，已回滚本次新增贡献",
            "感知标签部分新增未通过回读，回滚尚未确认；不会自动重试",
            "旧玩家感知标签仍有未确认贡献，但原能力系统暂不可用；未写入新玩家",
            "本修改器未添加感知忽略标签", "本地玩家能力系统或感知标签 Add/Remove 接口契约未通过",
            "游戏中尚未找到所需感知忽略标签", "游戏感知忽略标签已回读；敌人实战效果仍需验证",
            "玩家能力系统身份已变化，未添加感知标签",
            "部分感知标签贡献未能确认移除；不会重复减计数或写入新玩家", "隐身开关参数无效",
            "噩梦难度解锁与查询接口尚未通过核对", "难度解锁参数无效", "无法读取当前难度解锁状态",
            "六十六天难度已经解锁；当前难度保持原值", "难度解锁调用异常，未重复提交",
            "难度解锁调用已返回，但查询未匹配，未重复提交",
            "六十六天难度解锁已由游戏接口回读；由游戏正常保存，当前难度保持原值"}) {
            const auto translated = Translate(key);
            if (translated.empty() || HasChinese(translated)) { std::cerr << "Missing expanded-feature translation: " << key << '\n'; ++failures; }
        }
        CHECK(Translate("飞行上升") == "Fly up");
        CHECK(Translate("飞行下降") == "Fly down");
        CHECK(Translate("已调整 {} 次主动技能蓝图消耗；受击不变，原生风雷转换未覆盖，需实战核验").find("{}") != std::string::npos);
        CHECK(Translate("飞行速度：参数为空、格式错误或超出允许范围") ==
            "Flight speed: the value is empty, invalid or outside the allowed range");
        CHECK(!HasChinese(Translate("此快捷键已绑定：伤害倍率")));
        CHECK(!HasChinese(Translate("此快捷键已绑定：飞行上升")));
        CHECK(Translate("my own item / 用户物品名") == "my own item / 用户物品名");
        CHECK(Translate("关于###stable-id") == "About###stable-id");
        CHECK(Translate("关于\n设置") == "About\nSettings");
        std::thread other([] { BeginFrame(); Text("设置"); BeginFrame(); });
        other.join();
        CHECK(std::string(english) == "About");
        BeginFrame();
        CHECK(Translate("当前版本 v%s").find("%s") != std::string::npos);
        CHECK(Translate("UI 已加载  |  %s 打开菜单") == "UI loaded  |  Press %s to open the menu");
        CHECK(Translate("获取 {0} 个{1}结果未确认").find("{0}") != std::string::npos);
        CHECK(!HasChinese(Translate("数量：参数为空、格式错误或超出允许范围")));
        CHECK(!HasChinese(Translate("此快捷键已绑定：无限暗源")));
        CHECK(Translate("游戏物品名：库存 2 → 5，已核对增加 3") ==
            "游戏物品名: inventory 2 -> 5, verified increase of 3");
    }
    void TestPersistenceAndFailedWrite(const std::filesystem::path& file) {
        Initialize(file);
        CHECK(GetLanguage() == Language::English);
        CHECK(SetLanguage(Language::Chinese));
        Initialize(file);
        CHECK(GetLanguage() == Language::Chinese);
        const auto original = Read(file);
        const HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(held != INVALID_HANDLE_VALUE);
        if (held != INVALID_HANDLE_VALUE) {
            std::string error;
            CHECK(!SetLanguage(Language::English, &error));
            CHECK(!error.empty());
            CHECK(GetLanguage() == Language::Chinese);
            CHECK(Read(file) == original);
            CloseHandle(held);
        }
        CHECK(SetLanguage(Language::English));
        Initialize(file);
        CHECK(GetLanguage() == Language::English);
        Write(file, "[Interface]\nLanguage=bad-value\n");
        Initialize(file);
        CHECK(GetLanguage() == Language::Chinese);
        CHECK(!SetLanguage(static_cast<Language>(99)));
        CHECK(GetLanguage() == Language::Chinese);
    }
}
int main(int argc, char** argv) {
    const auto root = std::filesystem::absolute(argc > 1 ? argv[1] : "output/pbz-basics-20261029") /
        ("localization-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto file = root / "language.ini";
    TestTranslationAndPointerLifetime(file);
    TestPersistenceAndFailedWrite(file);
    if (failures) return 1;
    std::cout << "PASS: bilingual UI keys, frame pointer lifetime, thread isolation, persistence, malformed preferences and failed save rollback\n";
}
