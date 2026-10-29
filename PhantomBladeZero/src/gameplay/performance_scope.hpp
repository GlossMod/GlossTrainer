#pragma once
#include <array>
#include <chrono>
#include <format>
#include "../runtime.hpp"

namespace gloss::gameplay {
    // Used only by the verified game thread. Aggregate instead of logging each frame.
    class PerformanceScope {
        using Clock = std::chrono::steady_clock;
        struct Sample { double total{}, maximum{}; unsigned count{}, slow{}; };
        static inline std::array<Sample, 6> samples{};
        static inline Clock::time_point nextReport{};
        unsigned index;
        Clock::time_point started{Clock::now()};
    public:
        explicit PerformanceScope(unsigned section) : index(section) {}
        ~PerformanceScope() {
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration<double, std::milli>(now - started).count();
            auto& sample = samples[index];
            sample.total += elapsed;
            if (elapsed > sample.maximum) sample.maximum = elapsed;
            ++sample.count;
            if (elapsed >= 2) ++sample.slow;
            if (index != 0) return;
            if (nextReport == Clock::time_point{}) nextReport = now + std::chrono::seconds(10);
            if (now < nextReport) return;
            constexpr std::array names{"tick", "labels", "inventory", "images", "combat", "items"};
            for (unsigned i = 0; i < samples.size(); ++i) {
                const auto& value = samples[i];
                if (value.count) gloss::Log(std::format("PBZ perf {}: calls={} avg={:.3f}ms max={:.3f}ms over2ms={}",
                    names[i], value.count, value.total / value.count, value.maximum, value.slow));
            }
            samples = {};
            nextReport = now + std::chrono::seconds(10);
        }
    };
}
