#include "../src/gameplay/cooldown_rules.hpp"
#include <iostream>
#include <limits>

namespace {
    using namespace gloss::gameplay;
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)
    void TestCombinedCooldownAndDisable() {
        // Generic cooldown can exceed the reduced weapon timer. Both have
        // already been combined by the original getter at this boundary.
        const auto enabled = ApplySwitchCooldown(2.5f, true, true, true);
        CHECK(enabled.remaining == 0); CHECK(enabled.bypassed);
        const auto disabled = ApplySwitchCooldown(2.25f, false, true, true);
        CHECK(disabled.remaining == 2.25f); CHECK(!disabled.bypassed);
        // Re-enabling does not reuse or restore an old timer snapshot.
        CHECK(ApplySwitchCooldown(.125f, true, true, true).remaining == 0);
        CHECK(ApplySwitchCooldown(.1f, false, true, true).remaining == .1f);
    }
    void TestPlayerAndSlotScope() {
        for (const bool enabled : {false, true}) for (const bool local : {false, true})
            for (const bool knownSlot : {false, true}) {
                const auto result = ApplySwitchCooldown(4, enabled, local, knownSlot);
                const bool expected = enabled && local && knownSlot;
                CHECK(result.remaining == (expected ? 0.f : 4.f));
                CHECK(result.bypassed == expected);
            }
        // Stale pawn/component, another thread and NPC components all fail
        // currentLocalPlayer before this pure decision runs.
        CHECK(ApplySwitchCooldown(10, true, false, true).remaining == 10);
        CHECK(ApplySwitchCooldown(10, true, true, false).remaining == 10);
    }
    void TestSentinelsAndBoundaries() {
        for (const auto value : {0.f, -0.f, -1.f, -100.f}) {
            const auto result = ApplySwitchCooldown(value, true, true, true);
            CHECK(result.remaining == value); CHECK(!result.bypassed);
            CHECK(std::signbit(result.remaining) == std::signbit(value));
        }
        for (const auto value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            const auto result = ApplySwitchCooldown(value, true, true, true);
            CHECK(result.remaining == value); CHECK(!result.bypassed);
        }
        const auto nan = ApplySwitchCooldown(std::numeric_limits<float>::quiet_NaN(), true, true, true);
        CHECK(std::isnan(nan.remaining)); CHECK(!nan.bypassed);
        for (const auto value : {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()}) {
            const auto result = ApplySwitchCooldown(value, true, true, true);
            CHECK(result.remaining == 0); CHECK(result.bypassed);
        }
    }
}
int main() {
    TestCombinedCooldownAndDisable(); TestPlayerAndSlotScope(); TestSentinelsAndBoundaries();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
