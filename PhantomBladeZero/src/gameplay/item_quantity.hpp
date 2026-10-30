#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gloss::gameplay {
    // AddDustNum and the game's inventory quantities use signed 32-bit integers.
    inline constexpr int MaxItemQuantity = std::numeric_limits<std::int32_t>::max();

    inline bool ParseItemQuantity(std::string_view text, int& quantity) {
        if (text.empty()) return false;
        int parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            parsed < 1 || parsed > MaxItemQuantity) return false;
        quantity = parsed;
        return true;
    }

    inline bool ItemQuantityFitsInventory(std::int64_t owned, int quantity) {
        return quantity >= 1 && quantity <= MaxItemQuantity && owned >= 0 &&
            owned <= static_cast<std::int64_t>(MaxItemQuantity) - quantity;
    }
}
