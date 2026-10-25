#include "../src/localization.hpp"
#include "../src/gameplay/engine_bridge.hpp"
#include <Windows.h>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game_language_fixture
{
    struct Reply
    {
        bool succeeds{};
        std::wstring text;
    };
    ULONGLONG now{};
    bool onGameThread{};
    int failures{};
    std::map<std::wstring, Reply> replies;
    std::vector<std::wstring> calls;
    std::vector<std::pair<std::string, gloss::localization::DetectionSource>> cultures;
}

static ULONGLONG GameLanguageNow() { return game_language_fixture::now; }
#define GetTickCount64 GameLanguageNow
#include "../src/gameplay/game_language.cpp"
#undef GetTickCount64

namespace gloss::gameplay::engine
{
    bool IsGameThread() noexcept { return game_language_fixture::onGameThread; }
    CallResult Call(std::wstring_view function, std::span<Arg> arguments, std::uintptr_t)
    {
        game_language_fixture::calls.emplace_back(function);
        const auto found = game_language_fixture::replies.find(std::wstring(function));
        if (found == game_language_fixture::replies.end() || !found->second.succeeds)
            return {false, "mock reflection call failed", CallState::NotCalled};
        if (!arguments.empty()) arguments.front().text = found->second.text;
        return {true, {}, CallState::Completed};
    }
}

namespace gloss::localization
{
    bool SetDetectedCulture(std::string_view culture, DetectionSource source)
    {
        game_language_fixture::cultures.emplace_back(culture, source);
        return true;
    }
}

namespace
{
    using gloss::localization::DetectionSource;
    constexpr std::wstring_view LanguagePath = L"/Script/Engine.KismetInternationalizationLibrary:GetCurrentLanguage";
    constexpr std::wstring_view CulturePath = L"/Script/Engine.KismetInternationalizationLibrary:GetCurrentCulture";

#define CHECK(expression)                                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(expression))                                                                \
        {                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
            ++game_language_fixture::failures;                                            \
        }                                                                                 \
    } while (false)

    void SetReply(std::wstring_view path, bool succeeds, std::wstring value = {})
    {
        game_language_fixture::replies[std::wstring(path)] = {succeeds, std::move(value)};
    }

    void TestProbeRequiresGameThread()
    {
        game_language_fixture::now = 1000;
        game_language_fixture::onGameThread = false;
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.empty());
        CHECK(game_language_fixture::cultures.empty());
    }

    void TestLanguagePreferenceAndInterval()
    {
        game_language_fixture::now = 2000;
        game_language_fixture::onGameThread = true;
        SetReply(LanguagePath, true, L"zh-CN");
        SetReply(CulturePath, true, L"en-US");
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 1);
        CHECK(game_language_fixture::calls.back() == LanguagePath);
        CHECK(game_language_fixture::cultures.size() == 1);
        CHECK(game_language_fixture::cultures.back().first == "zh-CN");
        CHECK(game_language_fixture::cultures.back().second == DetectionSource::GameRuntime);

        game_language_fixture::now = 6999;
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 1); // Probes are throttled to five seconds.
    }

    void TestCultureFallbackAndInvalidUtf16()
    {
        game_language_fixture::now = 7000;
        SetReply(LanguagePath, false);
        SetReply(CulturePath, true, L"fr-FR");
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 3);
        CHECK(game_language_fixture::calls[1] == LanguagePath);
        CHECK(game_language_fixture::calls[2] == CulturePath);
        CHECK(game_language_fixture::cultures.back().first == "fr-FR");

        game_language_fixture::now = 12000;
        SetReply(LanguagePath, true, L"中文"); // Unreal should return a locale tag; reject non-ASCII data.
        SetReply(CulturePath, true, L"zh-Hant");
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 5);
        CHECK(game_language_fixture::calls[3] == LanguagePath);
        CHECK(game_language_fixture::calls[4] == CulturePath);
        CHECK(game_language_fixture::cultures.back().first == "zh-Hant");
    }

    void TestFailedProbeRetriesAfterInterval()
    {
        const auto priorCultures = game_language_fixture::cultures.size();
        game_language_fixture::now = 17000;
        SetReply(LanguagePath, false);
        SetReply(CulturePath, false);
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 7);
        CHECK(game_language_fixture::cultures.size() == priorCultures);

        game_language_fixture::now = 21999;
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 7);
        game_language_fixture::now = 22000;
        SetReply(LanguagePath, true, L"en-US");
        gloss::gameplay::game_language::Tick();
        CHECK(game_language_fixture::calls.size() == 8);
        CHECK(game_language_fixture::cultures.size() == priorCultures + 1);
        CHECK(game_language_fixture::cultures.back().first == "en-US");
    }
}

int main()
{
    TestProbeRequiresGameThread();
    TestLanguagePreferenceAndInterval();
    TestCultureFallbackAndInvalidUtf16();
    TestFailedProbeRetriesAfterInterval();
    if (game_language_fixture::failures)
    {
        std::cerr << game_language_fixture::failures << " game-language checks failed\n";
        return 1;
    }
    std::cout << "PASS game-language thread gate, engine probes, fallback and retry checks\n";
    return 0;
}
