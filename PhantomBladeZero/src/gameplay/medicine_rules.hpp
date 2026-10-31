#pragma once
#include <cstdint>

namespace gloss::gameplay {
    // GM_InfiniteMedicine writes this into MaxTime, the permanent base use count.
    // Earlier trainer versions called it, so saves can still hold the value.
    inline constexpr std::int32_t InflatedMedicineMaximum = 2147083647;
    inline constexpr std::int32_t MaxPlausibleMedicineUses = 1000;

    constexpr bool MedicineMaximumInflated(std::int32_t maximum) { return maximum == InflatedMedicineMaximum; }
    constexpr bool ValidMedicineDefault(std::int32_t uses) { return uses >= 1 && uses <= 99; }
    // The game's total is MaxTime + MedicineExtraUsage. Anything past this limit
    // means the base value is corrupt, and it must not be filled to.
    constexpr bool ValidMedicineTotal(std::int32_t total) { return total >= 1 && total <= MaxPlausibleMedicineUses; }
}
