#include "../src/runtime.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Exercise the real model and file I/O with a deterministic retry clock.
static ULONGLONG SettingsTickCount();
#define GetTickCount64 SettingsTickCount
#include "../src/features.cpp"
#undef GetTickCount64

namespace
{
    using namespace gloss::features;
    int failures{};
    std::filesystem::path root, directory;
    ULONGLONG now = 10000;
    std::array<bool, Count> active{};
    std::array<double, Count> applied{};
    std::array<Availability, Count> available{};
    std::array<int, Count> writes{};
    bool busy{}, fail{}, asynchronous{}, inlineCompletion{};
    int actions{};
    struct Command { Id id; bool enabled; double value; };
    std::optional<Command> command;

#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    constexpr std::size_t At(Id id) { return static_cast<std::size_t>(id); }
    void ResetBackend()
    {
        active.fill(false);
        applied.fill(0);
        available.fill(Availability::Ready);
        writes.fill(0);
        busy = fail = asynchronous = inlineCompletion = false;
        actions = 0;
        command.reset();
        RegisterBackend({
            [](Id id) { return BackendState{available[At(id)], active[At(id)], {}}; },
            [](Id id, bool enabled, double value) -> Result {
                ++writes[At(id)];
                // Read-only calls must remain safe from a backend callback.
                (void)HotkeysEnabled();
                if (busy || command) return {false, "busy", false, true};
                if (fail) return {false, "failure"};
                if (inlineCompletion) {
                    active[At(id)] = enabled;
                    applied[At(id)] = value;
                    CompleteAction(id, {true, "completed inline"});
                    return {true, "queued", true};
                }
                if (asynchronous) {
                    command = Command{id, enabled, value};
                    return {true, "queued", true};
                }
                active[At(id)] = enabled;
                applied[At(id)] = value;
                return {true, {}};
            },
            [](Id, double) { ++actions; return Result{true, {}}; }
        });
    }
    void Begin(const char *name)
    {
        SetUpdateRequired(false);
        directory = root / name;
        std::filesystem::create_directories(directory);
        ResetBackend();
        InitializeSettings();
    }
    void Restart()
    {
        ResetBackend();
        InitializeSettings();
    }
    void Tick()
    {
        now += 5000;
        RestoreSettings();
    }
    void Finish(bool success)
    {
        CHECK(command.has_value());
        if (!command) return;
        const auto value = *command;
        command.reset();
        if (success) {
            active[At(value.id)] = value.enabled;
            applied[At(value.id)] = value.value;
        }
        CompleteAction(value.id, {success, success ? "done" : "execution failed"});
    }
    void WriteFile(const std::string &text)
    {
        std::ofstream stream(directory / "features.ini", std::ios::binary | std::ios::trunc);
        stream << text;
        CHECK(stream.good());
    }
    std::string ReadFile()
    {
        std::ifstream stream(directory / "features.ini", std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), {}};
    }
    void TestDefaultsAndRoundTrip()
    {
        Begin("round-trip");
        CHECK(HotkeysEnabled());
        CHECK(!std::filesystem::exists(directory / "features.ini"));
        Tick();
        for (const auto &definition : Definitions()) {
            CHECK(GetSnapshot(definition.id).value == definition.initialValue);
            CHECK(writes[At(definition.id)] == 0);
        }
        CHECK(SetValue(Id::DamageMultiplier, 3.5));
        CHECK(SetEnabled(Id::DamageMultiplier, true).success);
        CHECK(SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(SetValue(Id::DamageMultiplier, 7.25)); // Staged, never applied.
        CHECK(SetValue(Id::AddExperience, 12345));
        SetHotkeysEnabled(false);
        CHECK(ReadFile().find("[DamageMultiplier]") != std::string::npos);
        Restart();
        CHECK(!HotkeysEnabled());
        CHECK(GetSnapshot(Id::DamageMultiplier).value == 7.25);
        CHECK(GetSnapshot(Id::AddExperience).value == 12345);
        CHECK(!active[At(Id::DamageMultiplier)]);
        CHECK(GetSnapshot(Id::DamageMultiplier).restorePending);
        Tick();
        CHECK(active[At(Id::DamageMultiplier)]);
        CHECK(applied[At(Id::DamageMultiplier)] == 3.5);
        CHECK(active[At(Id::InfiniteHealth)]);
        CHECK(!GetSnapshot(Id::DamageMultiplier).restorePending);
        CHECK(actions == 0);
        Tick();
        CHECK(writes[At(Id::DamageMultiplier)] == 1);
        CHECK(Apply(Id::DamageMultiplier).success);
        Restart();
        Tick();
        CHECK(applied[At(Id::DamageMultiplier)] == 7.25);
        CHECK(SetEnabled(Id::InfiniteHealth, false).success);
        Restart();
        Tick();
        CHECK(!active[At(Id::InfiniteHealth)]);
        CHECK(active[At(Id::DamageMultiplier)]);
    }
    void TestWaitingAndCancellation()
    {
        Begin("waiting");
        CHECK(SetEnabled(Id::InfiniteMana, true).success);
        Restart();
        available[At(Id::InfiniteMana)] = Availability::Unavailable;
        Tick();
        available[At(Id::InfiniteMana)] = Availability::Waiting;
        Tick();
        CHECK(writes[At(Id::InfiniteMana)] == 0);
        CHECK(GetSnapshot(Id::InfiniteMana).restorePending);
        CHECK(SetEnabled(Id::InfiniteMana, false).success);
        CHECK(!GetSnapshot(Id::InfiniteMana).restorePending);
        available[At(Id::InfiniteMana)] = Availability::Ready;
        Tick();
        CHECK(!active[At(Id::InfiniteMana)]);
        Restart();
        Tick();
        CHECK(!active[At(Id::InfiniteMana)]);
        CHECK(SetEnabled(Id::InfiniteMana, true).success);
        Restart();
        available[At(Id::InfiniteMana)] = Availability::Waiting;
        Tick();
        available[At(Id::InfiniteMana)] = Availability::Ready;
        Tick();
        CHECK(active[At(Id::InfiniteMana)]);
        active[At(Id::InfiniteMana)] = false; // Later scene loss must not re-enable.
        Tick();
        CHECK(!active[At(Id::InfiniteMana)]);
    }
    void TestBusyAndAsynchronousRestore()
    {
        Begin("async");
        CHECK(SetEnabled(Id::NoGravity, true).success);
        Restart();
        busy = true;
        Tick();
        CHECK(GetSnapshot(Id::NoGravity).restorePending);
        busy = false;
        asynchronous = true;
        Tick();
        CHECK(command.has_value());
        const auto submitted = writes[At(Id::NoGravity)];
        Tick();
        CHECK(writes[At(Id::NoGravity)] == submitted);
        CHECK(GetSnapshot(Id::NoGravity).restorePending);
        Finish(true);
        Tick();
        CHECK(active[At(Id::NoGravity)]);
        CHECK(!GetSnapshot(Id::NoGravity).restorePending);
        Restart();
        asynchronous = true;
        Tick();
        Finish(false);
        Tick();
        CHECK(!GetSnapshot(Id::NoGravity).restorePending);
        CHECK(writes[At(Id::NoGravity)] == 1);
        CHECK(GetSnapshot(Id::NoGravity).actionFailed);
        Restart(); // Execution failure must not erase the saved preference.
        Tick();
        CHECK(active[At(Id::NoGravity)]);
    }
    void TestCancelSubmittedRestore()
    {
        Begin("cancel-submitted");
        CHECK(SetEnabled(Id::HideHud, true).success);
        Restart();
        asynchronous = true;
        Tick();
        CHECK(SetEnabled(Id::HideHud, false).success);
        Finish(true); // Accepted enable may already be executing.
        busy = true;
        for (int attempt = 0; attempt < 8; ++attempt) Tick();
        CHECK(active[At(Id::HideHud)]);
        busy = false;
        Tick();
        CHECK(command.has_value());
        if (command) CHECK(!command->enabled);
        Finish(true);
        Tick();
        CHECK(!active[At(Id::HideHud)]);
        CHECK(!GetSnapshot(Id::HideHud).restorePending);
        Restart();
        Tick();
        CHECK(!active[At(Id::HideHud)]);
    }
    void TestUserAsyncAndImmediateCompletion()
    {
        Begin("user-async");
        asynchronous = true;
        CHECK(SetValue(Id::FixedCameraDistance, 800));
        const auto queued = SetEnabled(Id::FixedCameraDistance, true);
        CHECK(queued.success && queued.pending);
        CHECK(!active[At(Id::FixedCameraDistance)]);
        // A second write for the same ID must not replace the accepted request.
        CHECK(SetValue(Id::FixedCameraDistance, 950));
        CHECK(!SetEnabled(Id::FixedCameraDistance, true).success);
        Finish(true);
        Restart();
        Tick();
        CHECK(active[At(Id::FixedCameraDistance)]);
        CHECK(applied[At(Id::FixedCameraDistance)] == 800);
        CHECK(GetSnapshot(Id::FixedCameraDistance).value == 950);

        Begin("inline-completion");
        inlineCompletion = true;
        CHECK(SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(!GetSnapshot(Id::InfiniteHealth).restorePending);
        Restart();
        inlineCompletion = true;
        Tick();
        CHECK(active[At(Id::InfiniteHealth)]);
        CHECK(!GetSnapshot(Id::InfiniteHealth).restorePending);
        CHECK(GetSnapshot(Id::InfiniteHealth).message == "completed inline");
        Tick();
        CHECK(writes[At(Id::InfiniteHealth)] == 1);
    }
    void TestNewEnableSupersedesCancelledRestore()
    {
        Begin("reenable-after-cancel");
        CHECK(SetEnabled(Id::NoGravity, true).success);
        Restart();
        asynchronous = true;
        Tick();
        CHECK(SetEnabled(Id::NoGravity, false).success);
        Finish(true);
        CHECK(SetEnabled(Id::NoGravity, true).success);
        Finish(true);
        Tick();
        CHECK(active[At(Id::NoGravity)]);

        CHECK(!command);
        Restart();
        Tick();
        CHECK(active[At(Id::NoGravity)]);
        Begin("apply-after-cancel");
        CHECK(SetValue(Id::FixedCameraDistance, 800));
        CHECK(SetEnabled(Id::FixedCameraDistance, true).success);
        Restart();
        asynchronous = true;
        Tick();
        CHECK(SetEnabled(Id::FixedCameraDistance, false).success);
        Finish(true);
        CHECK(SetValue(Id::FixedCameraDistance, 950));
        CHECK(Apply(Id::FixedCameraDistance).success);
        Finish(true);
        Tick();
        CHECK(active[At(Id::FixedCameraDistance)]);
        CHECK(applied[At(Id::FixedCameraDistance)] == 950);
        CHECK(!command);
        Restart();
        Tick();
        CHECK(active[At(Id::FixedCameraDistance)]);
        CHECK(applied[At(Id::FixedCameraDistance)] == 950);
    }
    void TestUpdateBarrierAndFailures()
    {
        Begin("update");
        CHECK(SetEnabled(Id::InfiniteStamina, true).success);
        Restart();
        SetUpdateRequired(true);
        Tick();
        SetUpdateRequired(false);
        Tick();
        CHECK(!active[At(Id::InfiniteStamina)]);
        CHECK(writes[At(Id::InfiniteStamina)] == 0);
        Restart();
        Tick();
        CHECK(active[At(Id::InfiniteStamina)]);
        fail = true;
        CHECK(!SetEnabled(Id::InfiniteHealth, true).success);
        Restart();
        Tick();
        CHECK(!active[At(Id::InfiniteHealth)]);
        CHECK(active[At(Id::InfiniteStamina)]);
        SetUpdateRequired(true);
        CHECK(!active[At(Id::InfiniteStamina)]);
        SetUpdateRequired(false);
        Tick();
        CHECK(!active[At(Id::InfiniteStamina)]);
        Restart();
        Tick();
        CHECK(active[At(Id::InfiniteStamina)]);
    }
    void TestInvalidConfiguration()
    {
        Begin("invalid");
        WriteFile("[meta]\nversion=1\n[global]\nhotkeys_enabled=bogus\n"
                  "[InfiniteHealth]\nenabled=1\n[InfiniteMana]\nenabled=banana\n"
                  "[DamageMultiplier]\nvalue=nan\napplied_value=inf\n"
                  "[GameSpeed]\nvalue=999\n[EditWill]\nvalue=17.7\n"
                  "[AddExperience]\nenabled=1\nvalue=45\n[UnknownFutureFeature]\nenabled=1\n");
        Restart();
        Tick();
        CHECK(HotkeysEnabled());
        CHECK(active[At(Id::InfiniteHealth)]);
        CHECK(!active[At(Id::InfiniteMana)]);
        CHECK(GetSnapshot(Id::DamageMultiplier).value == GetDefinition(Id::DamageMultiplier).initialValue);
        CHECK(GetSnapshot(Id::GameSpeed).value == GetDefinition(Id::GameSpeed).maximum);
        CHECK(GetSnapshot(Id::EditWill).value == 18);
        CHECK(GetSnapshot(Id::AddExperience).value == 45);
        CHECK(actions == 0);
        WriteFile("[meta]\nversion=999\n[InfiniteHealth]\nenabled=1\n");
        Restart();
        Tick();
        CHECK(!active[At(Id::InfiniteHealth)]);
        WriteFile("truncated nonsense");
        Restart();
        Tick();
        CHECK(!active[At(Id::InfiniteHealth)]);
    }
    void TestSaveFailurePreservesFile()
    {
        Begin("write-failure");
        CHECK(SetValue(Id::DamageMultiplier, 4));
        const auto old = ReadFile();
        const auto path = directory / "features.ini";
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(locked != INVALID_HANDLE_VALUE);
        CHECK(SetValue(Id::DamageMultiplier, 9));
        CHECK(ReadFile() == old);
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        Tick(); // Retry a failed save even without another user edit.
        Restart();
        CHECK(GetSnapshot(Id::DamageMultiplier).value == 9);
        CHECK(SetValue(Id::DamageMultiplier, 10));
        Restart();
        CHECK(GetSnapshot(Id::DamageMultiplier).value == 10);
    }
}
static ULONGLONG SettingsTickCount() { return now; }
namespace gloss
{
    const std::filesystem::path &DataDirectory() { return directory; }
    void Log(std::string_view) {}
}
int main(int argc, char **argv)
{
    if (argc > 2) {
        std::cerr << "Usage: GlossTrainerFeatureSettingsTests [output-directory]\n";
        return 2;
    }
    root = std::filesystem::absolute(argc == 2 ? argv[1] : "output/feature-settings-validation") /
        ("cases-" + std::to_string(GetCurrentProcessId()));
    TestDefaultsAndRoundTrip();
    TestWaitingAndCancellation();
    TestBusyAndAsynchronousRestore();
    TestCancelSubmittedRestore();
    TestUserAsyncAndImmediateCompletion();
    TestNewEnableSupersedesCancelledRestore();
    TestUpdateBarrierAndFailures();
    TestInvalidConfiguration();
    TestSaveFailurePreservesFile();
    std::cout << "Feature settings tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
