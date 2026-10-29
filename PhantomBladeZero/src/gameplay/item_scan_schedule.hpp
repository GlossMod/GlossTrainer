#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
namespace gloss::gameplay {
    struct ItemScanSchedule {
        using Clock = std::chrono::steady_clock;
        Clock::time_point next{};
        std::uint32_t cursor{};
        bool Begin(Clock::time_point now, std::uint32_t count) {
            if (now < next) return false;
            if (cursor >= count) return false;
            next = now + std::chrono::milliseconds(50);
            return true;
        }
        std::uint32_t End(std::uint32_t count) const { return (std::min)(count, cursor + 512); }
        void Finish(Clock::time_point now, std::uint32_t count) {
            // Completed prefixes are never rescanned; appended objects remain discoverable.
            (void)now; (void)count;
        }
    };
}
