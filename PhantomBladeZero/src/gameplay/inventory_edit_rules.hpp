#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace gloss::gameplay {
    struct InventoryRowPolicy {
        std::uint8_t itemType{}, detailType{}, materialType{};
        bool attributeEffect{}, integration{}, flow{}, dialogue{}, ability{};
    };

    // Read-only game-script evidence: Map.confirmAndConsumeCharm removes one
    // Item_Traversing. A broad material/narrative-category exemption is unsafe.
    inline bool InventoryConsumableAllowed(std::string_view id, const InventoryRowPolicy& row) noexcept {
        return id == "Item_Traversing" && row.itemType == 0 && row.detailType == 1 && row.materialType == 0 &&
            !row.attributeEffect && !row.integration && !row.flow && !row.dialogue && !row.ability;
    }
    inline bool ParseInventoryTarget(std::string_view text, std::int32_t& target) noexcept {
        if (text.empty() || text.front() < '0' || text.front() > '9') return false;
        std::int32_t value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0) return false;
        target = value;
        return true;
    }
    inline std::optional<std::int32_t> InventoryRemovalAmount(std::int64_t current, std::int32_t target) noexcept {
        if (current < 0 || current > std::numeric_limits<std::int32_t>::max() || target < 0 || target > current) return {};
        return static_cast<std::int32_t>(current - target);
    }
    inline bool InventoryOrdinaryReduction(const InventoryRowPolicy& row) noexcept {
        return row.itemType == 0 && (row.detailType == 1 || row.detailType == 3 || row.detailType == 4 || row.detailType == 6) && row.materialType <= 7 &&
            !row.attributeEffect && !row.integration && !row.flow && !row.dialogue && !row.ability;
    }
}
