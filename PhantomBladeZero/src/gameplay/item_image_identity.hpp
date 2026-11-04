#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace gloss::gameplay {
    struct ItemImageObjectIdentity {
        std::uintptr_t address{}, type{}, outer{};
        std::int32_t index{-1}, serial{};
        std::uint64_t name{};
        bool Valid() const {
            return address >= 0x10000 && type >= 0x10000 && outer >= 0x10000 && index >= 0 && serial > 0;
        }
        bool operator==(const ItemImageObjectIdentity&) const = default;
    };

    // This is a weak observation, never an owning reference to a UObject. A
    // serial-less object must be loaded/revalidated again instead of retained.
    struct ItemImageObservation {
        std::uint64_t epoch{}, revision{};
        std::string source;
        ItemImageObjectIdentity texture;

        void Clear() { *this = {}; }
        bool Matches(std::uint64_t currentEpoch, std::uint64_t currentRevision,
            std::string_view currentSource, const ItemImageObjectIdentity& currentTexture) const {
            return texture.Valid() && currentTexture.Valid() && epoch == currentEpoch && revision == currentRevision &&
                source == currentSource && texture == currentTexture;
        }
    };
}
