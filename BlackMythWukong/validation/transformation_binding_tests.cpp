#include "../src/gameplay/transformation_bindings.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace trans = gloss::gameplay::transformation;
namespace binding = gloss::gameplay::transformation::bindings;

namespace
{
    int failures{};

#define CHECK(...)                                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        if (!(__VA_ARGS__))                                                                                \
        {                                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";              \
            ++failures;                                                                                   \
        }                                                                                                  \
    } while (false)

    constexpr unsigned Ctrl = 1;
    constexpr unsigned Shift = 2;
    constexpr unsigned Alt = 4;

    std::vector<trans::SkillEntry> Skills()
    {
        std::vector<trans::SkillEntry> skills;
        for (int i = 0; i < 16; ++i)
            skills.push_back({70000 + i, "Native skill", true, {}});
        skills.push_back({80000, "Not initialized", false, "not initialized"});
        return skills;
    }

    void TestMoreThanEightBindingsAndStableSkillIds()
    {
        const auto skills = Skills();
        std::vector<trans::SkillBinding> bindings;
        for (int i = 0; i < 12; ++i)
            bindings.push_back({70000 + i, {}, static_cast<unsigned>('A' + i), 0});
        CHECK(bindings.size() > 8);
        CHECK(binding::ValidBindings(bindings, skills).success);

        auto unknown = bindings;
        unknown.back().skillId = 999999;
        CHECK(!binding::ValidBindings(unknown, skills).success);

        // A saved binding may remain visible while the game's current skill
        // initialization says it is unavailable; execution handles that state.
        auto unavailable = bindings;
        unavailable[0].skillId = 80000;
        CHECK(binding::ValidBindings(unavailable, skills).success);
    }

    void TestCharacterScopedNamesAndBindingsRoundTripAsUtf8()
    {
        binding::Config source;
        source["/game/role/yangjian#"] = {
            {70001, "旋风斩", 'Q', Ctrl},
            {70002, "", 0, 0},
            {70003, "天眼·破阵", 'R', Ctrl | Shift},
        };
        source["/game/role/wolf#"] = {
            {70001, "狼袭", 'Q', Alt},
        };

        const auto encoded = binding::Serialize(source);
        binding::Config parsed;
        CHECK(binding::Parse(encoded, parsed).success);
        CHECK(parsed.size() == 2);
        CHECK(parsed.at("/game/role/yangjian#").size() == 3);
        CHECK(parsed.at("/game/role/yangjian#")[0].skillId == 70001);
        CHECK(parsed.at("/game/role/yangjian#")[0].customName == "旋风斩");
        CHECK(parsed.at("/game/role/yangjian#")[1].customName.empty());
        CHECK(parsed.at("/game/role/yangjian#")[2].modifiers == (Ctrl | Shift));
        CHECK(parsed.at("/game/role/wolf#")[0].skillId == 70001);
        CHECK(parsed.at("/game/role/wolf#")[0].customName == "狼袭");
        CHECK(parsed.at("/game/role/wolf#")[0].modifiers == Alt);
    }

    void TestModifierMatchingIsExactAndUnboundSkillsDoNotMatch()
    {
        const trans::SkillBinding keyBinding{70001, "", 'K', Ctrl};
        CHECK(binding::Matches(keyBinding, 'K', Ctrl));
        CHECK(!binding::Matches(keyBinding, 'K', 0));
        CHECK(!binding::Matches(keyBinding, 'K', Ctrl | Shift));
        CHECK(!binding::Matches(keyBinding, 'J', Ctrl));
        CHECK(!binding::Matches(trans::SkillBinding{70002, "", 0, 0}, 'K', Ctrl));

        CHECK(binding::Bindable('K', Ctrl));
        CHECK(binding::Bindable('K', Ctrl | Shift | Alt));
        CHECK(!binding::Bindable(0x11, 0)); // VK_CONTROL cannot be used as the main key.
        CHECK(!binding::Bindable(0x1b, 0)); // Escape is reserved for cancelling capture.
        CHECK(!binding::Bindable(0x24, Ctrl | Shift)); // Ctrl+Shift+Home toggles all features.
    }

    void TestDuplicateSkillIdsAndChordsAreRejectedPerCharacter()
    {
        const auto skills = Skills();
        const std::vector<trans::SkillBinding> duplicateId{
            {70001, "first", 'A', 0},
            {70001, "second", 'B', 0},
        };
        const std::vector<trans::SkillBinding> duplicateChord{
            {70001, "first", 'A', Ctrl},
            {70002, "second", 'A', Ctrl},
        };
        CHECK(!binding::ValidBindings(duplicateId, skills).success);
        CHECK(!binding::ValidBindings(duplicateChord, skills).success);

        binding::Config characters;
        characters["character-a"] = {{70001, "one", 'A', Ctrl}};
        characters["character-b"] = {{70002, "two", 'A', Ctrl}};
        const auto encoded = binding::Serialize(characters);
        binding::Config parsed;
        CHECK(binding::Parse(encoded, parsed).success);
        CHECK(parsed.size() == 2); // The same chord can be reused by another character.
    }

    void TestInvalidNamesAndMalformedFilesDoNotPolluteOutput()
    {
        const auto skills = Skills();
        CHECK(binding::ValidBindings({{70001, "", 0, 0}}, skills).success);
        CHECK(binding::ValidBindings({{70001, std::string(256, 'n'), 'A', 0}}, skills).success);
        CHECK(!binding::ValidBindings({{70001, std::string(257, 'n'), 'A', 0}}, skills).success);
        CHECK(!binding::ValidBindings({{70001, "bad\nname", 'A', 0}}, skills).success);
        CHECK(!binding::ValidBindings({{70001, std::string("\xC3\x28", 2), 'A', 0}}, skills).success);

        binding::Config output;
        output["keep"] = {{70003, "保留", 'Z', Shift}};
        const auto before = binding::Serialize(output);
        auto malformed = binding::Serialize(binding::Config{{"bad", {{70001, "名称", 'A', Ctrl}}}});
        if (!malformed.empty())
        {
            const auto closingQuote = malformed.rfind('"');
            if (closingQuote != std::string::npos) malformed.erase(closingQuote, 1);
        }
        const auto result = binding::Parse(malformed, output);
        CHECK(!result.success);
        CHECK(binding::Serialize(output) == before);
    }

    void TestMouseButtonsAndExactMouseChordsRoundTrip()
    {
        binding::Config source{{"large-boss", {
            {70001, "近战", 1, 0}, {70002, "蓄力", 2, Ctrl | Shift},
            {70003, "", 4, Alt}, {70004, "", 5, 0}, {70005, "", 6, Ctrl}}}};
        CHECK(binding::ValidBindings(source.begin()->second, Skills()).success);
        binding::Config loaded;
        CHECK(binding::Parse(binding::Serialize(source), loaded).success);
        CHECK(binding::Serialize(source) == binding::Serialize(loaded));
        CHECK(binding::Matches(loaded.begin()->second[1], 2, Ctrl | Shift));
        CHECK(!binding::Matches(loaded.begin()->second[1], 2, Ctrl));
        CHECK(!binding::Matches(loaded.begin()->second[1], 2, Ctrl | Shift | Alt));
        CHECK(!binding::Bindable(3, 0)); // VK_CANCEL is not a mouse button.
        CHECK(!binding::Bindable(7, 0)); // Undefined virtual key remains rejected.
    }
}

int main()
{
    TestMoreThanEightBindingsAndStableSkillIds();
    TestCharacterScopedNamesAndBindingsRoundTripAsUtf8();
    TestModifierMatchingIsExactAndUnboundSkillsDoNotMatch();
    TestDuplicateSkillIdsAndChordsAreRejectedPerCharacter();
    TestInvalidNamesAndMalformedFilesDoNotPolluteOutput();
    TestMouseButtonsAndExactMouseChordsRoundTrip();
    if (failures)
    {
        std::cerr << failures << " transformation binding assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation binding tests passed (0 failures)\n";
    return 0;
}
