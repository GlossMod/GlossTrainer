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

    void TestDefaultBindingsCoverEmptyShortExactAndLongCatalogs()
    {
        const std::vector<unsigned> expected{1, 2, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30};
        for (const auto count : {0u, 1u, 5u, 11u, 12u, 17u})
        {
            auto skills = Skills();
            skills.resize(count);
            const auto values = binding::DefaultBindings(skills);
            CHECK(values.size() == skills.size());
            CHECK(binding::ValidBindings(values, skills).success);
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                CHECK(values[i].skillId == skills[i].id);
                CHECK(values[i].customName.empty() && values[i].modifiers == 0);
                CHECK(values[i].key == (i < expected.size() ? expected[i] : 0));
                if (i >= 2 && i < 12)
                {
                    CHECK(values[i].key >= 0x30 && values[i].key <= 0x39); // Main keyboard VK_0..VK_9.
                    CHECK(!(values[i].key >= 0x60 && values[i].key <= 0x69)); // Never VK_NUMPAD0..9.
                }
            }
        }
    }

    void TestDefaultBindingsPreserveCatalogOrderIncludingUnavailableSkills()
    {
        const std::vector<trans::SkillEntry> skills{
            {90001, "First unavailable", false, "not initialized"},
            {17, "Second", true, {}},
            {74000, "Third unavailable", false, "not initialized"},
            {3, "Fourth", true, {}},
        };
        const auto values = binding::DefaultBindings(skills);
        CHECK(values.size() == 4);
        CHECK(values[0].skillId == 90001 && values[0].key == 1);
        CHECK(values[1].skillId == 17 && values[1].key == 2);
        CHECK(values[2].skillId == 74000 && values[2].key == '1');
        CHECK(values[3].skillId == 3 && values[3].key == '2');
        auto available = skills;
        for (auto &skill : available) skill.available = true;
        CHECK(binding::Serialize({{"role", values}}) == binding::Serialize({{"role", binding::DefaultBindings(available)}}));
    }

    void TestDefaultBindingsRoundTripKeepsUnboundTailAndEmptyNames()
    {
        const auto skills = Skills();
        const binding::Config source{{"default-role", binding::DefaultBindings(skills)}};
        binding::Config parsed;
        const auto encoded = binding::Serialize(source);
        CHECK(binding::Parse(encoded, parsed).success);
        CHECK(binding::Serialize(parsed) == encoded);
        const auto &values = parsed.at("default-role");
        CHECK(values.size() == skills.size() && binding::ValidBindings(values, skills).success);
        CHECK(values[0].key == 1 && values[1].key == 2 && values[11].key == '0');
        for (std::size_t i = 12; i < values.size(); ++i)
            CHECK(values[i].skillId == skills[i].id && values[i].key == 0 && values[i].modifiers == 0 && values[i].customName.empty());
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
    TestDefaultBindingsCoverEmptyShortExactAndLongCatalogs();
    TestDefaultBindingsPreserveCatalogOrderIncludingUnavailableSkills();
    TestDefaultBindingsRoundTripKeepsUnboundTailAndEmptyNames();
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
