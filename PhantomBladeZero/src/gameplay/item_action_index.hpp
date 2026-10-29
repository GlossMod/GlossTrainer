#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
namespace gloss::gameplay {
    // Snapshot header: player, playerInput, decorator table, device.
    // Each mapping: action address, asset name, package name, key name, validity.
    inline auto BuildItemActionIndex(const std::vector<std::uint64_t>& snapshot, int count) {
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> result;
        if (count < 0 || snapshot.size() < 4 + static_cast<std::size_t>(count) * 5) return result;
        for (int j = 0; j < count; ++j) {
            const auto offset = 4 + static_cast<std::size_t>(j) * 5;
            if (snapshot[offset + 4])
                result[(snapshot[offset + 2] << 32) | snapshot[offset + 1]].push_back(
                    static_cast<std::uint32_t>(snapshot[offset + 3]));
        }
        return result;
    }
}
