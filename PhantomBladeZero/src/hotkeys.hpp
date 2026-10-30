#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gloss::hotkeys {
    inline constexpr std::string_view MenuAction = "menu:toggle";
    enum Modifier : unsigned { Control = 1, Shift = 2, Alt = 4, Windows = 8 };
    struct Chord {
        unsigned key{}, modifiers{};
        bool operator==(const Chord&) const = default;
    };
    struct Action { std::string id; std::vector<std::string> defaults; std::string title; Chord defaultChord{}; };
    struct Binding { Chord chord; std::vector<std::string> parameters; };
    struct Invocation { std::string action; std::vector<std::string> parameters; };
    struct CaptureState { std::string action, message; };

    std::string ChordLabel(Chord chord);
    std::string ChordError(Chord chord);

    // Input threads only enqueue edges. Binding edits and disk writes are pumped
    // by the rendering thread; callbacks into the game are never made here.
    class Registry {
    public:
        void Configure(std::vector<Action> actions);
        Chord MenuChord() const;
        bool SetMenu(Chord chord, const std::filesystem::path& path, std::string& error);
        bool MatchesMenu(Chord chord, bool foreground) const;
        void BeginMenuCapture();
        std::optional<Binding> Get(std::string_view action) const;
        bool Set(std::string_view action, Chord chord, std::vector<std::string> parameters, std::string& error);
        void SetParameters(std::string_view action, std::vector<std::string> parameters);
        void BeginCapture(std::string_view action, std::vector<std::string> parameters);
        void CancelCapture();
        CaptureState Capture() const;
        bool Matches(Chord chord, bool foreground, bool menuVisible) const;
        bool QueueKey(Chord chord, bool foreground, bool menuVisible);
        void PumpCapture(const std::filesystem::path& path = {});
        std::vector<Invocation> Drain();
        void Complete(std::string_view action);
        void OnFocusLost();
        bool Load(const std::filesystem::path& path, std::string& error);
        bool Save(const std::filesystem::path& path, std::string& error);
        bool Dirty() const;
    private:
        bool SetLocked(std::string_view action, Chord chord, std::vector<std::string> parameters, std::string& error);
        bool SetMenuLocked(Chord chord, const std::filesystem::path& path, std::string& error);
        bool SaveLocked(const std::filesystem::path& path, Chord menu, std::string& error);
        void InitializeDefaultsLocked();
        mutable std::mutex mutex_;
        std::map<std::string, Action, std::less<>> actions_;
        std::map<std::string, Binding, std::less<>> bindings_;
        // Includes explicit clears and defaults skipped because another action owns the key.
        std::set<std::string, std::less<>> initializedBindings_;
        Chord menuChord_{0x2d, 0}; // VK_INSERT; keep the public header independent of Windows.h.
        CaptureState capture_;
        std::vector<std::string> captureParameters_;
        std::deque<Chord> captureKeys_;
        std::deque<Invocation> queue_;
        std::set<std::string, std::less<>> pending_;
        bool dirty_{};
    };
    Registry& GetRegistry();
}
