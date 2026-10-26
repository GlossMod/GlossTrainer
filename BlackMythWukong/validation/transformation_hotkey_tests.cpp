#include "../src/features.hpp"
#include "../src/gameplay/transformation_hotkeys.hpp"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace trans = gloss::gameplay::transformation;
namespace hotkeys = gloss::gameplay::transformation::hotkeys;

namespace fixture
{
    std::filesystem::path dataDirectory;
    bool overlayVisible{};
    trans::Status status;
    std::vector<trans::SkillBinding> bindings;
    int dispatched{};
    std::string dispatchedCharacter;
    int dispatchedSkill{};
    int failures{};
}

#define CHECK(...)                                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        if (!(__VA_ARGS__))                                                                                \
        {                                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";              \
            ++fixture::failures;                                                                          \
        }                                                                                                  \
    } while (false)

namespace gloss
{
    const std::filesystem::path &DataDirectory() { return fixture::dataDirectory; }
    void Log(std::string_view) {}
    namespace overlay
    {
        bool Visible() { return fixture::overlayVisible; }
    }
    namespace gameplay::transformation
    {
        Status GetStatus() { return fixture::status; }
        std::vector<SkillBinding> Bindings(std::string_view id)
        {
            return id == fixture::status.activeId ? fixture::bindings : std::vector<SkillBinding>{};
        }
        features::Result TriggerSkillHotkey(std::string_view id, int skillId)
        {
            ++fixture::dispatched;
            fixture::dispatchedCharacter = id;
            fixture::dispatchedSkill = skillId;
            return {true, {}};
        }
    }
}

namespace
{
    constexpr unsigned Ctrl = gloss::features::Ctrl;
    constexpr unsigned Shift = gloss::features::Shift;
    constexpr unsigned Alt = gloss::features::Alt;

    void TestMenuFeatureAndReservedChordsAreRejected()
    {
        CHECK(!hotkeys::Conflicts(VK_INSERT, 0).success); // Default menu binding.
        CHECK(gloss::features::SetMenuHotkey('M', Ctrl).success);
        CHECK(!hotkeys::Conflicts('M', Ctrl).success);

        CHECK(gloss::features::SetHotkey(gloss::features::Id::GodMode, 'G', Ctrl).success);
        CHECK(!hotkeys::Conflicts('G', Ctrl).success);
        CHECK(!gloss::features::SetHotkey(gloss::features::Id::InfiniteHealth, 'G', Ctrl).success);
        CHECK(!hotkeys::Conflicts(VK_HOME, Ctrl | Shift).success);
        CHECK(hotkeys::Conflicts('K', Alt).success);
    }

    void TestGlobalHotkeysCannotBeMovedOntoSavedSkillChords()
    {
        gloss::features::RegisterHotkeyConflictProbe([](unsigned key, unsigned modifiers)
        {
            return key == 'K' && modifiers == Alt ? std::string("变身技能 41001") : std::string{};
        });
        CHECK(!gloss::features::SetHotkey(gloss::features::Id::InfiniteMana, 'K', Alt).success);
        CHECK(!gloss::features::SetMenuHotkey('K', Alt).success);
        CHECK(gloss::features::SetHotkey(gloss::features::Id::InfiniteMana, 'L', Alt).success);
        gloss::features::RegisterHotkeyConflictProbe({});
    }

    void TestCaptureHandlesChordCancelAndClear()
    {
        hotkeys::BeginCapture("role-a", 41001);
        CHECK(hotkeys::AnyCapture());
        CHECK(hotkeys::HandleHotkey(VK_CONTROL, Ctrl)); // Modifier-only presses keep recording.
        CHECK(hotkeys::AnyCapture());
        CHECK(hotkeys::HandleHotkey('Q', Ctrl | Shift));
        const auto captured = hotkeys::TakeCaptured();
        CHECK(captured.has_value());
        if (captured)
        {
            CHECK(captured->characterId == "role-a");
            CHECK(captured->skillId == 41001);
            CHECK(captured->key == 'Q');
            CHECK(captured->modifiers == (Ctrl | Shift));
        }
        CHECK(!hotkeys::AnyCapture());

        hotkeys::BeginCapture("role-a", 41002);
        CHECK(hotkeys::HandleHotkey(VK_ESCAPE, 0));
        CHECK(!hotkeys::AnyCapture());
        CHECK(!hotkeys::TakeCaptured().has_value());

        hotkeys::BeginCapture("role-a", 41003);
        CHECK(hotkeys::HandleHotkey(VK_DELETE, 0));
        const auto cleared = hotkeys::TakeCaptured();
        CHECK(cleared.has_value());
        if (cleared)
        {
            CHECK(cleared->key == 0);
            CHECK(cleared->modifiers == 0);
        }

        hotkeys::BeginCapture("role-a", 41004);
        CHECK(hotkeys::HandleHotkey('G', Ctrl)); // Existing global feature chord.
        CHECK(hotkeys::AnyCapture());
        CHECK(!hotkeys::TakeCaptured().has_value());
        CHECK(hotkeys::HandleHotkey(VK_ESCAPE, 0));
        hotkeys::CancelCapture();
    }

    void TestBoundKeyDispatchUsesActiveCharacterAndHonorsRuntimeGates()
    {
        fixture::status = {};
        fixture::status.phase = trans::Phase::Active;
        fixture::status.activeId = "role-a";
        fixture::bindings = {{41001, "自定义名", 'H', Alt}};
        fixture::overlayVisible = false;
        gloss::features::SetHotkeysEnabled(true);
        gloss::features::SetUpdateRequired(false);
        fixture::dispatched = 0;

        CHECK(hotkeys::IsHotkey('H', Alt));
        CHECK(hotkeys::HandleHotkey('H', Alt));
        CHECK(fixture::dispatched == 1);
        CHECK(fixture::dispatchedCharacter == "role-a");
        CHECK(fixture::dispatchedSkill == 41001);

        fixture::overlayVisible = true;
        CHECK(!hotkeys::IsHotkey('H', Alt));
        fixture::overlayVisible = false;
        gloss::features::SetHotkeysEnabled(false);
        CHECK(!hotkeys::IsHotkey('H', Alt));
        gloss::features::SetHotkeysEnabled(true);
        fixture::status.phase = trans::Phase::Idle;
        CHECK(!hotkeys::IsHotkey('H', Alt));
        fixture::status.phase = trans::Phase::Active;
        gloss::features::SetUpdateRequired(true);
        CHECK(!hotkeys::IsHotkey('H', Alt));
        gloss::features::SetUpdateRequired(false);
        CHECK(hotkeys::IsHotkey('H', Alt));
    }

    void TestMouseCaptureLabelsConflictsAndDispatch()
    {
        constexpr unsigned keys[]{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        constexpr const char *labels[]{"鼠标左键", "鼠标右键", "鼠标中键", "鼠标侧键 1", "鼠标侧键 2"};
        for (std::size_t i = 0; i < std::size(keys); ++i)
        {
            CHECK(hotkeys::Conflicts(keys[i], Ctrl | Alt).success);
            CHECK(hotkeys::Label(keys[i], 0) == labels[i]);
            CHECK(hotkeys::Label(keys[i], Ctrl | Alt) == std::string("Ctrl+Alt+") + labels[i]);
            hotkeys::BeginCapture("role-a", 41001);
            CHECK(hotkeys::HandleHotkey(keys[i], Ctrl | Alt));
            const auto captured = hotkeys::TakeCaptured();
            CHECK(captured && captured->key == keys[i] && captured->modifiers == (Ctrl | Alt));
        }
        CHECK(gloss::features::SetHotkey(gloss::features::Id::InfiniteFocus, VK_RBUTTON, Shift).success);
        CHECK(!hotkeys::Conflicts(VK_RBUTTON, Shift).success);
        CHECK(hotkeys::Conflicts(VK_RBUTTON, 0).success);
        CHECK(gloss::features::SetMenuHotkey(VK_XBUTTON1, Ctrl).success);
        CHECK(!hotkeys::Conflicts(VK_XBUTTON1, Ctrl).success);
        CHECK(gloss::features::GetMenuHotkey().label == "Ctrl+鼠标侧键 1");
        CHECK(gloss::features::SetMenuHotkey('M', Ctrl).success);

        fixture::status.phase = trans::Phase::Active;
        fixture::status.activeId = "role-a";
        fixture::bindings = {{41002, "鼠标招式", VK_LBUTTON, Ctrl}};
        fixture::overlayVisible = false;
        fixture::dispatched = 0;
        CHECK(!hotkeys::HandleHotkey(VK_LBUTTON, 0));
        CHECK(!hotkeys::HandleHotkey(VK_LBUTTON, Ctrl | Shift));
        CHECK(hotkeys::HandleHotkey(VK_LBUTTON, Ctrl));
        CHECK(fixture::dispatched == 1 && fixture::dispatchedSkill == 41002);
        fixture::overlayVisible = true;
        CHECK(!hotkeys::HandleHotkey(VK_LBUTTON, Ctrl)); // Normal menu clicks still belong to ImGui.
        fixture::overlayVisible = false;
    }
}

int main()
{
    fixture::dataDirectory = std::filesystem::temp_directory_path() / L"GlossTrainerTransformationHotkeyTests";
    std::error_code error;
    std::filesystem::remove_all(fixture::dataDirectory, error);
    std::filesystem::create_directories(fixture::dataDirectory);

    TestMenuFeatureAndReservedChordsAreRejected();
    TestGlobalHotkeysCannotBeMovedOntoSavedSkillChords();
    TestCaptureHandlesChordCancelAndClear();
    TestBoundKeyDispatchUsesActiveCharacterAndHonorsRuntimeGates();
    TestMouseCaptureLabelsConflictsAndDispatch();
    hotkeys::CancelCapture();
    gloss::features::RegisterHotkeyConflictProbe({});
    std::filesystem::remove_all(fixture::dataDirectory, error);
    if (fixture::failures)
    {
        std::cerr << fixture::failures << " transformation hotkey assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation hotkey tests passed (0 failures)\n";
    return 0;
}
