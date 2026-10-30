#pragma once
#include <chrono>
#include <cstddef>

namespace gloss::gameplay {
    // A costly engine call may exceed the deadline by itself. Never begin a
    // second call after the deadline, and bound cheap/cache-only work as well.
    struct ItemLoadingBudget {
        using Clock = std::chrono::steady_clock;
        Clock::time_point deadline;
        std::size_t limit{}, consumed{};

        ItemLoadingBudget(Clock::time_point start, Clock::duration duration, std::size_t maximum)
            : deadline(start + duration), limit(maximum) {}

        bool Continue(Clock::time_point now) const {
            return consumed < limit && (consumed == 0 || now < deadline);
        }
        void Consume() { ++consumed; }
    };
}
