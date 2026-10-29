#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gloss::gameplay {
    enum class ItemImageFormat { Rgba8, Bgra8, Bc1, Bc2, Bc3, Bc7 };
    struct ItemImageRequest { std::uint64_t epoch{}; std::string key; };
    struct ItemImageResult {
        std::uint64_t epoch{};
        std::string key;
        std::uint32_t width{}, height{};
        ItemImageFormat format{};
        // Tightly packed rows/BC blocks, top mip only. Empty data means failure.
        std::vector<std::uint8_t> data;
        std::string error;
    };
    void ResetItemImages(std::uint64_t epoch);
    std::uint64_t ItemImageEpoch();
    bool RequestItemImage(std::string_view key);
    std::optional<ItemImageRequest> TakeItemImageRequest();
    void PublishItemImage(ItemImageResult result);
    std::vector<ItemImageResult> DrainItemImageResults();
}
