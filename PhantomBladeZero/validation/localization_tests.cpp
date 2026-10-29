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
            "暂无正式发布版本", "检查更新失败", "语言设置保存失败，已保留原设置"}) {
            if (HasChinese(Translate(key))) { std::cerr << "Missing translation: " << key << '\n'; ++failures; }
        }
        CHECK(Translate("my own item / 用户物品名") == "my own item / 用户物品名");
        CHECK(Translate("关于###stable-id") == "About###stable-id");
        CHECK(Translate("关于\n设置") == "About\nSettings");
        std::thread other([] { BeginFrame(); Text("设置"); BeginFrame(); });
        other.join();
        CHECK(std::string(english) == "About");
        BeginFrame();
        CHECK(Translate("当前版本 v%s").find("%s") != std::string::npos);
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
