#pragma once
#include <chrono>
#include <cstddef>

namespace gloss::gameplay {
    // Spreads one status pass across frames, evaluating a single slice per Tick, so
    // reflection checks and engine read-backs cannot gather into one long frame.
    class StatusSchedule {
        using Clock = std::chrono::steady_clock;
        Clock::time_point nextPass{};
        std::size_t cursor{};
        bool running{};
    public:
        static constexpr auto IdleInterval = std::chrono::seconds(2);
        // Returns the slice to evaluate this frame, or sliceCount for none. Anything
        // reading status now (menu, restoring toggles) passes immediate.
        std::size_t Next(Clock::time_point now, bool immediate, std::size_t sliceCount) {
            if (!running) {
                if (!immediate && now < nextPass) return sliceCount;
                running = true;
                cursor = 0;
            }
            return cursor < sliceCount ? cursor++ : sliceCount;
        }
        bool Finished(std::size_t sliceCount) const { return running && cursor >= sliceCount; }
        void Complete(Clock::time_point now) { running = false; cursor = 0; nextPass = now + IdleInterval; }
        void Restart() { running = false; cursor = 0; nextPass = {}; }
    };
}
