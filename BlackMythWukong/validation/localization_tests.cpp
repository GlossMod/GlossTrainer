#include "../src/localization.hpp"
#include "../src/localization_model.hpp"
#include "../src/runtime.hpp"
#include "../src/gameplay/transformation_adapter.hpp"
#include "../src/gameplay/transformation_bindings.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace test_state
{
    std::filesystem::path dataDirectory;
    int failures{};
}

namespace gloss
{
    const std::filesystem::path &DataDirectory() { return test_state::dataDirectory; }
    void Log(std::string_view) {}
}

// Compile the production persistence and translation path in this test binary.
#include "../src/localization.cpp"

namespace gloss::localization
{
    void ResetForTest()
    {
        std::lock_guard lock(stateMutex);
        state = {};
        language.store(Language::English, std::memory_order_relaxed);
        initialized.store(false, std::memory_order_release);
    }
}

namespace
{
    using gloss::localization::Language;
    using gloss::localization::Preference;

#define CHECK(expression)                                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(expression))                                                                \
        {                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
            ++test_state::failures;                                                       \
        }                                                                                 \
    } while (false)

    struct Entry
    {
        const char *source;
        const char *english;
    };

    const std::vector<Entry> entries = {
#include "../src/localization_strings.inc"
#include "../src/localization_catalog.inc"
    };
    const std::vector<Entry> patterns = {
#include "../src/localization_patterns.inc"
    };

    struct TemporaryDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("GlossTrainer-localization-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        bool owned{};
        TemporaryDirectory() { owned = std::filesystem::create_directory(path); }
        ~TemporaryDirectory()
        {
            if (owned)
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        }
    };

    void WriteFile(const std::filesystem::path &path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    std::vector<std::string> PrintfSignature(std::string_view text)
    {
        std::vector<std::string> result;
        constexpr std::string_view flags = "-+ #0'";
        constexpr std::string_view conversions = "diouxXfFeEgGaAcspn%";
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] != '%')
            {
                ++i;
                continue;
            }
            std::size_t end = i + 1;
            if (end < text.size() && text[end] == '%')
            {
                result.emplace_back("%%");
                i = end + 1;
                continue;
            }
            while (end < text.size() && flags.find(text[end]) != std::string_view::npos) ++end;
            if (end < text.size() && text[end] == '*')
            {
                ++end;
                while (end < text.size() && IsDigit(text[end])) ++end;
                if (end < text.size() && text[end] == '$') ++end;
            }
            else
                while (end < text.size() && IsDigit(text[end])) ++end;
            if (end < text.size() && text[end] == '.')
            {
                ++end;
                if (end < text.size() && text[end] == '*')
                {
                    ++end;
                    while (end < text.size() && IsDigit(text[end])) ++end;
                    if (end < text.size() && text[end] == '$') ++end;
                }
                else
                    while (end < text.size() && IsDigit(text[end])) ++end;
            }
            if (end + 1 < text.size() && ((text[end] == 'h' && text[end + 1] == 'h') || (text[end] == 'l' && text[end + 1] == 'l')))
                end += 2;
            else if (end < text.size() && std::string_view("hljztL").find(text[end]) != std::string_view::npos)
                ++end;
            if (end < text.size() && conversions.find(text[end]) != std::string_view::npos)
            {
                result.emplace_back(text.substr(i, end - i + 1));
                i = end + 1;
            }
            else
                ++i; // A percent sign in prose, such as "100% drop rate".
        }
        return result;
    }

    std::vector<std::string> BraceSignature(std::string_view text)
    {
        std::vector<std::string> result;
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{')
            {
                i += 2;
                continue;
            }
            if (text[i] == '}' && i + 1 < text.size() && text[i + 1] == '}')
            {
                i += 2;
                continue;
            }
            if (text[i] != '{')
            {
                if (text[i] == '}') result.emplace_back("<unmatched-close>");
                ++i;
                continue;
            }
            const auto close = text.find('}', i + 1);
            if (close == std::string_view::npos)
            {
                result.emplace_back("<unmatched-open>");
                break;
            }
            const auto field = text.substr(i + 1, close - i - 1);
            if (field.empty()) result.emplace_back("{}");
            else if (std::all_of(field.begin(), field.end(), IsDigit)) result.emplace_back(std::string(field));
            else result.emplace_back("<invalid-field>");
            i = close + 1;
        }
        return result;
    }

    bool ContainsCjk(std::string_view text)
    {
        for (std::size_t i = 0; i + 2 < text.size(); ++i)
        {
            const auto first = static_cast<unsigned char>(text[i]);
            const auto second = static_cast<unsigned char>(text[i + 1]);
            const auto third = static_cast<unsigned char>(text[i + 2]);
            if (first >= 0xE3 && first <= 0xE9 && (second & 0xC0) == 0x80 && (third & 0xC0) == 0x80)
            {
                const auto codepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
                if (codepoint >= 0x3400 && codepoint <= 0x9FFF) return true;
            }
        }
        return false;
    }

    std::vector<std::string> CppStringLiterals(std::string_view line)
    {
        std::vector<std::string> result;
        for (std::size_t i = 0; i < line.size();)
        {
            if (line[i] != '"')
            {
                ++i;
                continue;
            }
            ++i;
            std::string value;
            while (i < line.size() && line[i] != '"')
            {
                if (line[i] == '\\' && i + 1 < line.size())
                {
                    const auto escape = line[++i];
                    switch (escape)
                    {
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case '\\': value.push_back('\\'); break;
                    case '"': value.push_back('"'); break;
                    default: value.push_back(escape); break;
                    }
                }
                else
                    value.push_back(line[i]);
                ++i;
            }
            if (i < line.size()) ++i;
            result.push_back(std::move(value));
        }
        return result;
    }

    void CheckSourceTranslationKeys(const std::filesystem::path &sourcePath, std::string_view sourceName,
                                   const std::set<std::string_view> &keys, bool requireTranslationCall)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        CHECK(input.good());
        if (!input.good()) return;

        std::string line;
        int lineNumber{};
        while (std::getline(input, line))
        {
            ++lineNumber;
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line.compare(first, 2, "//") == 0) continue;
            if (requireTranslationCall && line.find("Text(") == std::string::npos &&
                line.find("Translate(") == std::string::npos && line.find("UiLabel(") == std::string::npos)
                continue;
            for (const auto &literal : CppStringLiterals(line))
            {
                if (!ContainsCjk(literal) || literal == "简体中文") continue; // Language names intentionally stay native.
                if (!keys.contains(literal))
                {
                    std::cerr << "Missing translation key at " << sourceName << ':' << lineNumber << ": " << literal << '\n';
                    ++test_state::failures;
                }
            }
        }
    }

    void TestApplicationTranslationKeyCoverage()
    {
        auto sourceDirectory = std::filesystem::path(__FILE__).parent_path().parent_path() / "src";
        if (!std::filesystem::exists(sourceDirectory / "overlay.cpp"))
        {
            auto directory = std::filesystem::current_path();
            while (!directory.empty())
            {
                if (std::filesystem::exists(directory / "BlackMythWukong" / "src" / "overlay.cpp"))
                {
                    sourceDirectory = directory / "BlackMythWukong" / "src";
                    break;
                }
                if (std::filesystem::exists(directory / "src" / "overlay.cpp"))
                {
                    sourceDirectory = directory / "src";
                    break;
                }
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
        }
        std::set<std::string_view> keys;
        for (const auto &entry : entries) keys.emplace(entry.source);
        for (const auto &pattern : patterns) keys.emplace(pattern.source);
        CheckSourceTranslationKeys(sourceDirectory / "overlay.cpp", "overlay.cpp", keys, true);
        // Feature result strings are translated after the model returns them to the UI.
        CheckSourceTranslationKeys(sourceDirectory / "features.cpp", "features.cpp", keys, false);
        CheckSourceTranslationKeys(sourceDirectory / "localization.cpp", "localization.cpp", keys, false);
        const auto gameplayDirectory = sourceDirectory / "gameplay";
        CHECK(std::filesystem::is_directory(gameplayDirectory));
        if (std::filesystem::is_directory(gameplayDirectory))
        {
            // Runtime results and inline adapter/configuration errors reach the
            // UI after they are produced. Scan their source even when it does
            // not call Text/Translate, including newly added gameplay modules.
            // Catalog data is checked separately through entries above.
            for (const auto &file : std::filesystem::recursive_directory_iterator(gameplayDirectory))
            {
                if (!file.is_regular_file() || (file.path().extension() != ".cpp" && file.path().extension() != ".hpp")) continue;
                const auto name = file.path().lexically_relative(sourceDirectory).generic_string();
                CheckSourceTranslationKeys(file.path(), name, keys, false);
            }
        }
    }

    void TestCultureAndPreferenceModel()
    {
        using namespace gloss::localization::model;
        CHECK(CultureToLanguage("zh-CN") == Language::Chinese);
        CHECK(CultureToLanguage("zh-Hans") == Language::Chinese);
        CHECK(CultureToLanguage("zh-TW") == Language::Chinese);
        CHECK(CultureToLanguage("zh-Hant") == Language::Chinese);
        CHECK(CultureToLanguage("en") == Language::English);
        CHECK(CultureToLanguage("en-US") == Language::English);
        CHECK(CultureToLanguage("fr-FR") == Language::English); // Valid but unsupported cultures use English.
        CHECK(!CultureToLanguage(""));
        CHECK(!CultureToLanguage("en--US"));
        CHECK(!CultureToLanguage("zh_"));
        CHECK(!CultureToLanguage("1n-US"));

        CHECK(ParsePreference("auto") == Preference::FollowGame);
        CHECK(ParsePreference("ZH-cn") == Preference::Chinese);
        CHECK(ParsePreference("en") == Preference::English);
        CHECK(!ParsePreference("zh-TW"));
        CHECK(!ParsePreference("unsupported"));
        CHECK(PreferenceValue(Preference::FollowGame) == "auto");
        CHECK(PreferenceValue(Preference::Chinese) == "zh-CN");
        CHECK(PreferenceValue(Preference::English) == "en");
        CHECK(EffectiveLanguage(Preference::FollowGame, Language::Chinese) == Language::Chinese);
        CHECK(EffectiveLanguage(Preference::FollowGame, Language::English) == Language::English);
        CHECK(EffectiveLanguage(Preference::Chinese, Language::English) == Language::Chinese);
        CHECK(EffectiveLanguage(Preference::English, Language::Chinese) == Language::English);

        const auto configured = ReadIniCulture("\xEF\xBB\xBF; header\r\n[Other]\r\nLanguage=fr-FR\r\n[Internationalization]\r\nCulture=en-US\r\nLanguage=zh-Hant\r\n");
        CHECK(configured && *configured == "zh-Hant");
        const auto cultureFallback = ReadIniCulture("[Internationalization]\nCulture=fr-FR\nLanguage=en--US\n");
        CHECK(cultureFallback && *cultureFallback == "fr-FR");
        CHECK(!ReadIniCulture("[Internationalization]\nCulture=en--US\n"));
        const auto languageOnly = ReadIniCulture("[Internationalization]\nLanguage=zh-CN\n");
        CHECK(languageOnly && *languageOnly == "zh-CN");
    }

    void TestDictionaryIntegrity()
    {
        CHECK(!entries.empty());
        std::set<std::string_view> keys;
        for (const auto &entry : entries)
        {
            CHECK(entry.source && *entry.source);
            CHECK(entry.english && *entry.english);
            if (!keys.emplace(entry.source).second)
            {
                std::cerr << "Duplicate localization key: " << entry.source << '\n';
                ++test_state::failures;
            }
            if (PrintfSignature(entry.source) != PrintfSignature(entry.english))
            {
                std::cerr << "printf placeholder mismatch: " << entry.source << " -> " << entry.english << '\n';
                ++test_state::failures;
            }
            CHECK(BraceSignature(entry.source) == BraceSignature(entry.english));
        }
        CHECK(!patterns.empty());
        std::set<std::string_view> patternKeys;
        for (const auto &pattern : patterns)
        {
            CHECK(pattern.source && *pattern.source);
            CHECK(pattern.english && *pattern.english);
            if (!patternKeys.emplace(pattern.source).second)
            {
                std::cerr << "Duplicate localization pattern: " << pattern.source << '\n';
                ++test_state::failures;
            }
            CHECK(gloss::localization::model::ValidTranslation(pattern.source, pattern.english));
        }
    }

    void TestTemplates()
    {
        using namespace gloss::localization::model;
        CHECK(ValidTranslation("技能 {0}：{1}", "Skill {1}: {0}"));
        CHECK(!ValidTranslation("技能 {0}：{1}", "Skill {1}"));
        const auto captures = MatchTemplate("{0}：{1}（{0}）", "斩棍式：第三段（斩棍式）");
        CHECK(captures && captures->size() == 2 && (*captures)[0] == "斩棍式" && (*captures)[1] == "第三段");
        const std::vector<std::string> values{"Cut stance", "Third move"};
        CHECK(FormatTemplate("{1} · {0}", values) == "Third move · Cut stance");
        CHECK(!MatchTemplate("{0}{1}", "ab")); // Adjacent captures have no unambiguous split.
        CHECK(!MatchTemplate("{0}", std::string(16385, 'x')));
    }

    void TestRuntimePreferenceAndPersistence(const std::filesystem::path &root)
    {
        using namespace gloss::localization;
        test_state::dataDirectory = root / "automatic";
        ResetForTest();
        CHECK(GetPreference() == Preference::FollowGame); // Missing or malformed settings default to FollowGame.
        CHECK(SetDetectedCulture("zh-TW", DetectionSource::GameConfig));
        CHECK(GetLanguage() == Language::Chinese);
        CHECK(GetGameCulture() == "zh-TW");

        auto result = SetPreference(Preference::English);
        CHECK(result.success && GetLanguage() == Language::English); // UI language changes immediately.
        CHECK(SetDetectedCulture("zh-Hant", DetectionSource::GameRuntime));
        CHECK(GetLanguage() == Language::English); // Explicit user selection beats later game probes.
        CHECK(!SetDetectedCulture("en--US", DetectionSource::GameRuntime));
        CHECK(!SetDetectedCulture("en-US", DetectionSource::OperatingSystem)); // Runtime evidence outranks fallback probes.
        CHECK(GetDetection().source == DetectionSource::GameRuntime);

        CHECK(SetPreference(Preference::Chinese).success && GetLanguage() == Language::Chinese);
        CHECK(SetPreference(Preference::FollowGame).success && GetLanguage() == Language::Chinese);
        CHECK(SetPreference(Preference::English).success && GetLanguage() == Language::English);

        ResetForTest();
        CHECK(GetPreference() == Preference::English); // A new initialization reads the persisted choice.
        CHECK(GetLanguage() == Language::English);

        test_state::dataDirectory = root / "malformed";
        WriteFile(test_state::dataDirectory / "language.ini", "[Interface]\r\nLanguage=unsupported\r\n");
        ResetForTest();
        CHECK(GetPreference() == Preference::FollowGame);

        test_state::dataDirectory = root / "blocked" / "child";
        WriteFile(root / "blocked", "parent is a file");
        ResetForTest();
        CHECK(GetPreference() == Preference::FollowGame);
        const auto languageBeforeFailure = GetLanguage();
        result = SetPreference(Preference::English);
        CHECK(!result.success);
        CHECK(GetPreference() == Preference::FollowGame);
        CHECK(GetLanguage() == languageBeforeFailure); // The failed write cannot change the active language.
    }

    void TestTranslationFallbackAndCustomText(const std::filesystem::path &root)
    {
        using namespace gloss::localization;
        test_state::dataDirectory = root / "translation";
        ResetForTest();
        CHECK(SetPreference(Preference::English).success);
        CHECK(std::string(Text("检查更新")) == "Check for updates");
        CHECK(Translate("检查更新") == "Check for updates");
        CHECK(Translate("七十二变（实验性）") == "72 Transformations (Experimental)");
        CHECK(Translate("七十二变\n（实验性）") == "72 Changes\n(Experimental)");
        const std::string custom = "我为这个技能设置的名称";
        CHECK(Translate(custom) == custom); // Unknown user-provided text stays verbatim.
        CHECK(Translate("未收录的角色名称") == "未收录的角色名称");
        CHECK(Translate("鼠标左键") == "Mouse Left");
        CHECK(Translate("Ctrl+鼠标右键") == "Ctrl+Mouse Right");
        CHECK(Translate("Ctrl+Shift+Alt+鼠标侧键 2") == "Ctrl+Shift+Alt+Mouse X2");
        CHECK(Translate("Ctrl+自定义鼠标名") == "Ctrl+自定义鼠标名");
        CHECK(Text(nullptr) == nullptr);
        for (const auto &pattern : patterns)
        {
            std::array<std::string, 8> sample{"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta"};
            auto source = gloss::localization::model::FormatTemplate(pattern.source, sample);
            auto expected = gloss::localization::model::FormatTemplate(pattern.english, sample);
            if (source && expected)
                CHECK(Translate(*source) == *expected);
        }
    }

    std::string NumberFormatFields(std::string_view text)
    {
        std::string result;
        unsigned index{};
        for (std::size_t position = 0; position < text.size();)
        {
            if (text.substr(position).starts_with("{}"))
            {
                result += '{' + std::to_string(index++) + '}';
                position += 2;
            }
            else result.push_back(text[position++]);
        }
        return result;
    }

    void TestFormattedRuntimeTranslations(const std::filesystem::path &root)
    {
        using namespace gloss::localization;
        test_state::dataDirectory = root / "formatted-runtime";
        ResetForTest();
        CHECK(SetPreference(Preference::English).success);
        const std::array<std::string, 8> values{"12345", "; detail", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta"};
        for (const auto &entry : entries)
        {
            if (std::string_view(entry.source).find("{}") == std::string_view::npos) continue;
            // A source format key is insufficient: the completed runtime value
            // must also match an anchored translation template.
            const auto source = model::FormatTemplate(NumberFormatFields(entry.source), values);
            const auto expected = model::FormatTemplate(NumberFormatFields(entry.english), values);
            CHECK(source && expected);
            if (source && expected && Translate(*source) != *expected)
            {
                std::cerr << "Missing formatted runtime translation: " << *source << '\n';
                ++test_state::failures;
            }
        }
        CHECK(Translate("角色依赖字段 检查更新 无法校验") == "Could not validate character dependency field 检查更新");
        CHECK(Translate("形态依赖 BattleSC；需要专用阶段或场景驱动") ==
              "This form depends on BattleSC; a dedicated phase or scene adapter is required");
        CHECK(Translate("形态包含 OtherFsmTemplates 依赖或未知序列格式；需要专用适配") ==
              "This form has a dependency on OtherFsmTemplates or an unknown sequence format; a dedicated adapter is required");
        const std::string prefixed = "我的角色依赖字段 BattleSC 无法校验";
        const std::string suffixed = "角色依赖字段 BattleSC 无法校验（我的备注）";
        CHECK(Translate(prefixed) == prefixed);
        CHECK(Translate(suffixed) == suffixed);
    }

    void TestTransformationResultsAndCustomNames(const std::filesystem::path &root)
    {
        using namespace gloss::localization;
        namespace adapter = gloss::gameplay::transformation::adapter;
        namespace bindings = gloss::gameplay::transformation::bindings;
        test_state::dataDirectory = root / "transformation-results";
        ResetForTest();
        CHECK(SetPreference(Preference::English).success);

        adapter::Movement movement{1, 0, 1, 4, 600.f, 400.f, 300.f, 1.f};
        constexpr std::array<const char *, 3> expected{
            "Ground movement adapter; experimental standalone proxy with manual skills",
            "Swimming movement adapter; experimental standalone proxy with manual skills",
            "Flying movement adapter; experimental standalone proxy with manual skills"};
        constexpr std::array<std::uint8_t, 3> modes{1, 4, 5};
        for (std::size_t i = 0; i < modes.size(); ++i)
        {
            movement.mode = modes[i];
            const auto decision = adapter::Evaluate(movement, true);
            CHECK(decision.allowed);
            CHECK(Translate(decision.reason + "；独立代理/手动技能实验模式") == expected[i]);
        }
        movement.custom = 1;
        const auto rejected = adapter::Evaluate(movement);
        CHECK(!rejected.allowed);
        CHECK(Translate(rejected.reason) == "This form uses a custom movement mode; a dedicated adapter is unavailable");

        bindings::Config loaded;
        const auto incomplete = bindings::Parse("GLOSS_TRANSFORMATION_BINDINGS_1\n\"role\" 1\n102801", loaded);
        CHECK(!incomplete.success);
        CHECK(Translate(incomplete.message) == "A skill configuration record is incomplete");
        const auto duplicate = bindings::Parse("GLOSS_TRANSFORMATION_BINDINGS_1\n\"role\" 0\n\"role\" 0\n", loaded);
        CHECK(!duplicate.success);
        CHECK(Translate(duplicate.message) == "Character skill configuration records are corrupt or duplicated");

        const bindings::Config custom{{"role", {{102801, "检查更新", 'K', gloss::features::Ctrl}}}};
        const auto saved = bindings::Serialize(custom);
        CHECK(bindings::Parse(saved, loaded).success);
        CHECK(loaded.at("role").front().customName == "检查更新");
        CHECK(SetPreference(Preference::Chinese).success);
        CHECK(bindings::Serialize(loaded) == saved);
        CHECK(SetPreference(Preference::English).success);
        CHECK(bindings::Serialize(loaded) == saved); // Language selection never rewrites player labels or chords.
        CHECK(Translate("快捷键已被「检查更新」使用") == "This hotkey is already used by \"检查更新\"");
        CHECK(Translate("快捷键已被「已恢复悟空：玩家或场景已变化」使用") ==
              "This hotkey is already used by \"已恢复悟空：玩家或场景已变化\"");
        CHECK(Translate("已恢复悟空：角色移动组件在运行中失效") ==
              "Wukong restored: The character's movement component became invalid during play");
        CHECK(Translate("已恢复悟空：形态依赖 BattleSC；需要专用阶段或场景驱动") ==
              "Wukong restored: This form depends on BattleSC; a dedicated phase or scene adapter is required");
        CHECK(SetPreference(Preference::Chinese).success);
        CHECK(Translate("已恢复悟空：角色移动组件在运行中失效") == "已恢复悟空：角色移动组件在运行中失效");
    }

    void TestBadCultureAndExplicitNameBehavior(const std::filesystem::path &root)
    {
        using namespace gloss::localization;
        test_state::dataDirectory = root / "culture";
        ResetForTest();
        CHECK(GetPreference() == Preference::FollowGame);
        CHECK(SetDetectedCulture("fr-FR", DetectionSource::GameRuntime));
        CHECK(GetLanguage() == Language::English); // Unsupported but valid game locales select the English fallback.
        CHECK(GetGameCulture() == "fr-FR");
        CHECK(SetPreference(Preference::Chinese).success && GetLanguage() == Language::Chinese);
        CHECK(SetDetectedCulture("en-US", DetectionSource::GameRuntime));
        CHECK(GetLanguage() == Language::Chinese);
    }
}

int main()
{
    TemporaryDirectory temporary;
    if (!temporary.owned)
    {
        std::cerr << "FAIL could not create an isolated localization test directory\n";
        return 1;
    }
    TestCultureAndPreferenceModel();
    TestDictionaryIntegrity();
    TestApplicationTranslationKeyCoverage();
    TestTemplates();
    TestRuntimePreferenceAndPersistence(temporary.path);
    TestTranslationFallbackAndCustomText(temporary.path);
    TestFormattedRuntimeTranslations(temporary.path);
    TestTransformationResultsAndCustomNames(temporary.path);
    TestBadCultureAndExplicitNameBehavior(temporary.path);
    if (test_state::failures)
    {
        std::cerr << test_state::failures << " localization checks failed\n";
        return 1;
    }
    std::cout << "PASS localization model, dictionary, culture detection, persistence and translation checks\n";
    return 0;
}
