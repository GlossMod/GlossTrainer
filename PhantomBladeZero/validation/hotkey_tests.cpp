#include "../src/hotkeys.hpp"
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
    using namespace gloss::hotkeys;
    using namespace std::string_literals;
    int failures{}, checks{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    std::vector<Action> Actions() { return {{"toggle:health", {}, "无限生命"}, {"action:speed", {"1"}, "游戏速度"}, {"action:heal", {}, "恢复生命"}}; }
    void TestDefaultsAndValidation() {
        Registry r; r.Configure(Actions());
        std::string error;
        CHECK(!r.Get("toggle:health"));
        CHECK(!r.Matches({'H', Control}, true, false));
        CHECK(!r.Set("missing", {'H', Control}, {}, error));
        CHECK(!error.empty());
        CHECK(r.Set("toggle:health", {'H', Control}, {}, error));
        CHECK(r.Get("toggle:health")->chord == gloss::hotkeys::Chord{'H', Control});
        CHECK(!r.Set("action:heal", {'H', Control}, {}, error));
        CHECK(error.find("无限生命") != std::string::npos);
        CHECK(error.find("toggle:") == std::string::npos);
        CHECK(r.Set("action:heal", {'H', Shift}, {}, error));
        CHECK(!r.Set("action:speed", {'F', Alt}, {}, error));
        CHECK(r.Set("action:speed", {'F', Alt}, {"1.75"}, error));
        CHECK(r.Set("action:heal", {}, {}, error));
        CHECK(!r.Get("action:heal"));
        for (const auto key : {VK_INSERT, VK_ESCAPE, VK_DELETE, VK_TAB, VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, VK_PROCESSKEY, VK_PACKET, VK_LBUTTON, 255, 400}) {
            CHECK(!r.Set("action:heal", {unsigned(key), 0}, {}, error));
        }
        CHECK(!ChordError({VK_F4, Alt}).empty());
        CHECK(!ChordError({VK_SPACE, Alt}).empty());
        CHECK(!ChordError({'F', Windows}).empty());
        CHECK(!ChordError({'F', 16}).empty());
        CHECK(ChordError({VK_F4, Control}).empty());
        CHECK(ChordLabel({'P', Control | Shift | Alt}) == "Ctrl+Shift+Alt+P");
        CHECK(ChordLabel({VK_F24, 0}) == "F24");
        CHECK(ChordLabel({VK_INSERT, Control | Shift}) == "Ctrl+Shift+Insert");
        CHECK(ChordLabel({}) == "未绑定");
        CHECK(!ChordLabel({VK_HOME, Alt}).empty());
        CHECK(!r.Set("action:speed", {'S', 0}, {std::string(1025, 'x')}, error));
    }
    void TestEligibilityQueueAndPending() {
        Registry r; r.Configure(Actions()); std::string error;
        CHECK(r.Set("action:speed", {'T', Control}, {"2.5"}, error));
        CHECK(!r.QueueKey({'T', Control}, false, false));
        CHECK(!r.QueueKey({'T', Control}, true, true));
        CHECK(!r.QueueKey({'T', 0}, true, false));
        CHECK(!r.Matches({'T', Control}, false, false));
        CHECK(!r.Matches({'T', Control}, true, true));
        CHECK(r.Matches({'T', Control}, true, false));
        for (int i = 0; i < 100; ++i) CHECK(r.QueueKey({'T', Control}, true, false));
        auto queue = r.Drain();
        CHECK(queue.size() == 1);
        CHECK(queue[0].action == "action:speed");
        CHECK(queue[0].parameters == std::vector<std::string>{"2.5"});
        CHECK(r.Drain().empty());
        CHECK(r.QueueKey({'T', Control}, true, false));
        CHECK(r.Drain().empty()); // Draining does not release pending protection.
        r.Complete("action:speed");
        CHECK(r.QueueKey({'T', Control}, true, false));
        CHECK(r.Drain().size() == 1);
        r.OnFocusLost();
        CHECK(r.QueueKey({'T', Control}, true, false));
        r.OnFocusLost();
        CHECK(r.Drain().empty());
        CHECK(r.QueueKey({'T', Control}, true, false));
        r.Configure({{"action:other", {}}});
        CHECK(!r.Get("action:speed"));
        CHECK(r.Drain().empty());
        CHECK(!r.QueueKey({'T', Control}, true, false));
    }
    void TestCapturePriorityAndErrors() {
        Registry r; r.Configure(Actions()); std::string error;
        CHECK(r.Set("action:heal", {'Q', Control}, {}, error));
        r.BeginCapture("action:speed", {"1.75"});
        CHECK(r.Capture().action == "action:speed");
        CHECK(r.Matches({VK_INSERT, 0}, true, true));
        CHECK(r.QueueKey({VK_SHIFT, Shift}, true, true));
        r.PumpCapture(); CHECK(!r.Capture().action.empty());
        CHECK(r.QueueKey({VK_INSERT, 0}, true, true));
        r.PumpCapture(); CHECK(r.Capture().message.find("界面开关") != std::string::npos);
        CHECK(r.QueueKey({'Q', Control}, true, true));
        r.PumpCapture(); CHECK(r.Capture().message.find("已绑定") != std::string::npos);
        CHECK(r.Drain().empty());
        CHECK(!r.QueueKey({'R', Control}, false, true));
        CHECK(!r.QueueKey({VK_LBUTTON, 0}, true, true));
        CHECK(r.QueueKey({'R', Control | Shift}, true, true));
        CHECK(!r.Get("action:speed")); // Input thread has only queued capture.
        r.PumpCapture();
        CHECK(r.Capture().action.empty());
        CHECK(r.Get("action:speed")->parameters == std::vector<std::string>{"1.75"});
        CHECK(r.Get("action:speed")->chord == gloss::hotkeys::Chord{'R', Control | Shift});
        r.BeginCapture("action:speed", {"3"});
        CHECK(r.QueueKey({VK_ESCAPE, 0}, true, true));
        r.PumpCapture();
        CHECK(r.Capture().action.empty());
        CHECK(r.Get("action:speed")->parameters[0] == "1.75");
        r.BeginCapture("action:speed", {"3"});
        CHECK(r.QueueKey({VK_DELETE, 0}, true, true));
        r.PumpCapture();
        CHECK(!r.Get("action:speed"));
        r.BeginCapture("action:heal", {});
        CHECK(r.QueueKey({'K', 0}, true, true));
        r.OnFocusLost(); r.PumpCapture();
        CHECK(r.Capture().action.empty());
        CHECK(r.Get("action:heal")->chord.key == 'Q');
        r.BeginCapture("missing", {}); CHECK(r.Capture().action.empty());
        r.BeginCapture("action:speed", {}); CHECK(r.Capture().action.empty());
    }
    void TestPersistenceAndCatalogChange(const std::filesystem::path& root) {
        Registry r; r.Configure(Actions()); std::string error;
        const auto path = root / "hotkeys-roundtrip.ini";
        CHECK(r.Set("action:speed", {'S', Control | Alt}, {"1.25"}, error));
        CHECK(r.Set("toggle:health", {VK_F3, Shift}, {}, error));
        r.SetParameters("action:speed", {"2.75"});
        CHECK(r.Dirty());
        CHECK(r.Save(path, error)); CHECK(!r.Dirty());
        Registry loaded; loaded.Configure(Actions()); CHECK(loaded.Load(path, error));
        CHECK(loaded.Get("action:speed")->parameters[0] == "2.75");
        CHECK(loaded.Get("toggle:health")->chord == gloss::hotkeys::Chord{VK_F3, Shift});
        loaded.Configure({{"action:speed", {"1"}}});
        CHECK(!loaded.Get("toggle:health"));
        CHECK(loaded.Get("action:speed").has_value());
        CHECK(loaded.Save(root / "hotkeys-filtered.ini", error));
        Registry changedShape; changedShape.Configure({{"action:speed", {"1", "2"}}});
        CHECK(changedShape.Load(path, error)); CHECK(!changedShape.Get("action:speed"));
        Registry unicode; unicode.Configure({{"action:text", {""}}});
        CHECK(unicode.Set("action:text", {'U', Alt}, {"测试\n=[参数]\0尾"s}, error));
        CHECK(unicode.Save(root / "hotkeys-unicode.ini", error));
        Registry restored; restored.Configure({{"action:text", {""}}});
        CHECK(restored.Load(root / "hotkeys-unicode.ini", error));
        CHECK(restored.Get("action:text")->parameters == unicode.Get("action:text")->parameters);
        std::ofstream bad(root / "hotkeys-invalid.ini", std::ios::binary);
        bad << "[action:speed]\nkey=83\nmodifiers=1\ncount=1\np0=zz\n[removed:action]\nkey=70\nmodifiers=0\ncount=0\n[action:heal]\nkey=45\nmodifiers=0\ncount=0\n"; bad.close();
        CHECK(loaded.Load(root / "hotkeys-invalid.ini", error)); CHECK(!loaded.Get("action:speed")); CHECK(!loaded.Get("removed:action")); CHECK(!loaded.Get("action:heal"));
        std::ofstream oversize(root / "hotkeys-oversize.ini", std::ios::binary); oversize << std::string(1024 * 1024 + 1, 'x'); oversize.close();
        CHECK(!loaded.Load(root / "hotkeys-oversize.ini", error)); CHECK(!error.empty());
        CHECK(loaded.Load(root / "does-not-exist.ini", error));
        CHECK(!r.Save(root / "hotkeys-roundtrip.ini" / "impossible.ini", error));
    }
    void TestConcurrentInputQueue() {
        Registry r; r.Configure(Actions()); std::string error;
        CHECK(r.Set("action:heal", {'H', 0}, {}, error));
        std::atomic_bool go{};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) threads.emplace_back([&] { while (!go.load()) {} for (int i = 0; i < 1000; ++i) r.QueueKey({'H', 0}, true, false); });
        go = true;
        for (auto& thread : threads) thread.join();
        CHECK(r.Drain().size() == 1);
        r.OnFocusLost(); CHECK(r.Drain().empty());
    }
    void TestMenuBindingAndConflicts(const std::filesystem::path& root) {
        Registry r; r.Configure(Actions()); std::string error;
        const auto path = root / "menu-hotkeys.ini";
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0});
        CHECK(r.MatchesMenu({VK_INSERT, 0}, true));
        CHECK(!r.MatchesMenu({VK_INSERT, 0}, false));
        CHECK(!r.MatchesMenu({VK_INSERT, Control}, true));
        CHECK(!r.Set("action:heal", {VK_INSERT, 0}, {}, error));
        CHECK(error.find("界面开关") != std::string::npos);
        CHECK(r.SetMenu({'M', Control | Shift}, path, error));
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control | Shift});
        CHECK(!r.MatchesMenu({VK_INSERT, 0}, true));
        CHECK(r.MatchesMenu({'M', Control | Shift}, true));
        CHECK(!r.MatchesMenu({'M', Control}, true));
        // Menu input is routed independently of gameplay eligibility or actions.
        CHECK(!r.QueueKey({'M', Control | Shift}, true, false));
        CHECK(!r.QueueKey({'M', Control | Shift}, true, true));
        CHECK(!r.Set("action:heal", {'M', Control | Shift}, {}, error));
        CHECK(r.Set("action:heal", {VK_INSERT, 0}, {}, error));
        CHECK(!r.SetMenu({VK_INSERT, 0}, path, error));
        CHECK(error.find("恢复生命") != std::string::npos);
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control | Shift});
        CHECK(r.Set("action:heal", {}, {}, error));
        CHECK(r.SetMenu({VK_INSERT, 0}, path, error));
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0});
        for (const auto chord : {gloss::hotkeys::Chord{}, gloss::hotkeys::Chord{VK_ESCAPE, 0}, gloss::hotkeys::Chord{VK_DELETE, Control}, gloss::hotkeys::Chord{VK_TAB, 0},
            gloss::hotkeys::Chord{VK_SHIFT, 0}, gloss::hotkeys::Chord{VK_F4, Alt}, gloss::hotkeys::Chord{VK_SPACE, Alt}, gloss::hotkeys::Chord{'K', Windows},
            gloss::hotkeys::Chord{VK_LBUTTON, 0}, gloss::hotkeys::Chord{255, 0}}) {
            CHECK(!r.SetMenu(chord, path, error));
            CHECK(!error.empty());
            CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0});
        }
        for (unsigned modifiers = 0; modifiers < 8; ++modifiers) {
            CHECK(r.SetMenu({VK_HOME, modifiers}, path, error));
            CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_HOME, modifiers});
        }
        r.Configure({});
        CHECK(r.MatchesMenu({VK_HOME, 7}, true));
        CHECK(!r.Set(MenuAction, {'A', 0}, {}, error));
    }
    void TestMenuCaptureAndSaveFailures(const std::filesystem::path& root) {
        Registry r; r.Configure(Actions()); std::string error;
        const auto path = root / "menu-capture.ini";
        CHECK(r.Set("action:heal", {'H', Control}, {}, error));
        CHECK(r.QueueKey({'H', Control}, true, false));
        r.BeginMenuCapture();
        CHECK(r.Capture().action == MenuAction);
        CHECK(r.Drain().empty());
        CHECK(!r.MatchesMenu({VK_INSERT, 0}, true));
        CHECK(!r.QueueKey({'M', Control}, false, true));
        CHECK(r.QueueKey({VK_SHIFT, Shift}, true, true));
        r.PumpCapture(path); CHECK(r.Capture().action == MenuAction);
        CHECK(r.QueueKey({VK_DELETE, 0}, true, true));
        r.PumpCapture(path);
        CHECK(r.Capture().action == MenuAction);
        CHECK(r.Capture().message.find("不能为空") != std::string::npos);
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0});
        CHECK(r.QueueKey({'H', Control}, true, true));
        r.PumpCapture(path);
        CHECK(r.Capture().message.find("恢复生命") != std::string::npos);
        CHECK(r.Drain().empty());
        CHECK(r.QueueKey({'M', Control}, true, true));
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0}); // Input thread never edits the model or disk.
        r.PumpCapture(path);
        CHECK(r.Capture().action.empty());
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        CHECK(!r.Dirty());
        Registry loaded; loaded.Configure(Actions()); CHECK(loaded.Load(path, error));
        CHECK(loaded.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        CHECK(loaded.Get("action:heal")->chord == gloss::hotkeys::Chord{'H', Control});

        r.BeginMenuCapture();
        CHECK(r.QueueKey({VK_ESCAPE, 0}, true, true)); r.PumpCapture(path);
        CHECK(r.Capture().action.empty()); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        r.BeginMenuCapture();
        CHECK(r.QueueKey({'Q', 0}, true, true)); r.OnFocusLost(); r.PumpCapture(path);
        CHECK(r.Capture().action.empty()); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        r.BeginMenuCapture(); r.CancelCapture(); CHECK(r.Capture().action.empty());

        CHECK(r.Set("action:heal", {'J', Control}, {}, error));
        CHECK(r.Dirty());
        // Disallow replacement at the OS level: the original file and active
        // menu stay usable, and unrelated unsaved feature edits remain dirty.
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(file != INVALID_HANDLE_VALUE);
        CHECK(!r.SetMenu({'N', Alt}, path, error));
        CHECK(!error.empty()); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control}); CHECK(r.Dirty());
        r.BeginMenuCapture(); CHECK(r.QueueKey({'N', Alt}, true, true)); r.PumpCapture(path);
        CHECK(r.Capture().action == MenuAction); CHECK(!r.Capture().message.empty());
        CHECK(r.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        CHECK(CloseHandle(file));
        Registry unchanged; unchanged.Configure(Actions()); CHECK(unchanged.Load(path, error));
        CHECK(unchanged.MenuChord() == gloss::hotkeys::Chord{'M', Control});
        CHECK(unchanged.Get("action:heal")->chord == gloss::hotkeys::Chord{'H', Control});
        CHECK(r.QueueKey({'N', Alt}, true, true)); r.PumpCapture(path);
        CHECK(r.Capture().action.empty()); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'N', Alt}); CHECK(!r.Dirty());
        CHECK(unchanged.Load(path, error)); CHECK(unchanged.MenuChord() == gloss::hotkeys::Chord{'N', Alt});
        CHECK(unchanged.Get("action:heal")->chord == gloss::hotkeys::Chord{'J', Control});
        CHECK(!r.SetMenu({'P', 0}, {}, error)); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'N', Alt});
        r.BeginMenuCapture(); CHECK(r.QueueKey({'P', 0}, true, true)); r.PumpCapture();
        CHECK(r.Capture().action == MenuAction); CHECK(r.MenuChord() == gloss::hotkeys::Chord{'N', Alt});
    }
    void TestMenuPersistenceCompatibility(const std::filesystem::path& root) {
        const auto path = root / "menu-compatibility.ini";
        std::string error;
        auto write = [&](const std::string& content) { std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream << content; };
        write("[action:heal]\nkey=72\nmodifiers=1\ncount=0\n");
        Registry legacy; legacy.Configure(Actions()); CHECK(legacy.Load(path, error));
        CHECK(legacy.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0}); CHECK(legacy.Get("action:heal")->chord == gloss::hotkeys::Chord{'H', Control});
        write("[action:heal]\nkey=77\nmodifiers=1\ncount=0\n[menu:toggle]\nkey=77\nmodifiers=1\ncount=0\n");
        CHECK(legacy.Load(path, error));
        CHECK(legacy.MenuChord() == gloss::hotkeys::Chord{'M', Control}); CHECK(!legacy.Get("action:heal"));
        // A malformed menu always falls back to Insert, with feature conflicts
        // rejected after that fallback. None may leave an empty menu binding.
        for (const auto& menu : {"key=0\nmodifiers=0\ncount=0\n", "key=72\nmodifiers=8\ncount=0\n",
            "key=27\nmodifiers=0\ncount=0\n", "key=72\nmodifiers=0\ncount=1\np0=31\n",
            "key=72oops\nmodifiers=0\ncount=0\n", "key=72\ncount=0\n"}) {
            write(std::string("[action:heal]\nkey=45\nmodifiers=0\ncount=0\n[menu:toggle]\n") + menu);
            CHECK(legacy.Load(path, error)); CHECK(legacy.MenuChord() == gloss::hotkeys::Chord{VK_INSERT, 0}); CHECK(!legacy.Get("action:heal"));
        }
        CHECK(legacy.SetMenu({'K', Control | Alt}, path, error));
        CHECK(legacy.Set("action:heal", {VK_INSERT, 0}, {}, error));
        CHECK(legacy.Save(path, error));
        Registry restored; restored.Configure(Actions()); CHECK(restored.Load(path, error));
        CHECK(restored.MenuChord() == gloss::hotkeys::Chord{'K', Control | Alt});
        CHECK(restored.Get("action:heal")->chord == gloss::hotkeys::Chord{VK_INSERT, 0});
        CHECK(restored.Set("action:speed", {VK_F7, 0}, {"1.25"}, error));
        CHECK(restored.Save(path, error));
        Registry again; again.Configure(Actions()); CHECK(again.Load(path, error));
        CHECK(again.MenuChord() == restored.MenuChord());
        CHECK(again.Get("action:speed")->parameters[0] == "1.25");
    }
    void TestAllModifiersAndQueueCapacity() {
        Registry r; std::vector<Action> actions;
        for (int i = 0; i < 64; ++i) actions.push_back({"action:" + std::to_string(i), {"1"}, "测试功能"});
        r.Configure(std::move(actions)); std::string error;
        for (unsigned modifiers = 0; modifiers < 8; ++modifiers) {
            CHECK(r.Set("action:" + std::to_string(modifiers), {VK_F12, modifiers}, {std::to_string(modifiers)}, error));
            CHECK(r.Matches({VK_F12, modifiers}, true, false));
            CHECK(r.QueueKey({VK_F12, modifiers}, true, false));
        }
        auto queue = r.Drain(); CHECK(queue.size() == 8);
        for (unsigned i = 0; i < 8; ++i) { CHECK(queue[i].parameters[0] == std::to_string(i)); r.Complete(queue[i].action); }
        r.OnFocusLost();
        for (unsigned i = 0; i < 64; ++i) {
            CHECK(r.Set("action:" + std::to_string(i), {'A' + (i % 26), i / 26}, {"1"}, error));
            CHECK(r.QueueKey({'A' + (i % 26), i / 26}, true, false));
        }
        queue = r.Drain(); CHECK(queue.size() == 32);
        for (const auto& call : queue) r.Complete(call.action);
        // The over-capacity input was consumed but must not be marked pending.
        CHECK(r.QueueKey({'A' + (63 % 26), 63 / 26}, true, false));
        queue = r.Drain(); CHECK(queue.size() == 1); CHECK(queue[0].action == "action:63");
    }
}

int main(int argc, char** argv) {
    const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("output/pbz-combat-20261029/hotkeys");
    std::filesystem::create_directories(root);
    TestDefaultsAndValidation(); TestEligibilityQueueAndPending(); TestCapturePriorityAndErrors();
    TestPersistenceAndCatalogChange(root); TestConcurrentInputQueue(); TestAllModifiersAndQueueCapacity();
    TestMenuBindingAndConflicts(root); TestMenuCaptureAndSaveFailures(root); TestMenuPersistenceCompatibility(root);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
