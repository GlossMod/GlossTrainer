#include "game_icon_pixels.hpp"

#include "game_read.hpp"

#include <godot_cpp/classes/atlas_texture.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace gloss::dndg {

IconPixels EncodeIconPixels(std::string key, godot::Ref<godot::Image> image, std::optional<godot::Rect2i> region) {
    IconPixels result{std::move(key)};
    if (image.is_null() || image->is_empty() || (image->is_compressed() && image->decompress() != godot::OK))
        return result;
    if (region) {
        const auto clipped = region->intersection(godot::Rect2i(0, 0, image->get_width(), image->get_height()));
        if (!clipped.has_area())
            return result;
        // Decompress before cropping. get_region cannot blit compressed data.
        image = image->get_region(clipped);
        if (image.is_null() || image->is_empty())
            return result;
    }

    const auto width = image->get_width(), height = image->get_height();
    if (width <= 0 || height <= 0)
        return result;
    const auto edge = std::max(width, height);
    if (edge > IconMaxSide) {
        const auto fit = [edge](std::int64_t size) { return std::max<std::int64_t>(1, size * IconMaxSide / edge); };
        image->resize(fit(width), fit(height), godot::Image::INTERPOLATE_NEAREST);
    }
    image->clear_mipmaps();
    image->convert(godot::Image::FORMAT_RGBA8);
    const auto outWidth = image->get_width(), outHeight = image->get_height();
    if (outWidth <= 0 || outHeight <= 0 || outWidth > IconMaxSide || outHeight > IconMaxSide)
        return result;
    const auto rgba = image->get_data();
    if (rgba.size() != outWidth * outHeight * 4)
        return result;
    result.width = static_cast<int>(outWidth);
    result.height = static_cast<int>(outHeight);
    result.rgba.assign(rgba.ptr(), rgba.ptr() + rgba.size());
    return result;
}

IconPixels ReadGameIconPixels(std::string key) {
    const auto texture = LoadGameIcon(key);
    if (texture.is_null())
        return IconPixels{std::move(key)};
    if (const godot::Ref<godot::AtlasTexture> atlas = texture; atlas.is_valid()) {
        const auto backing = atlas->get_atlas();
        if (backing.is_null())
            return IconPixels{std::move(key)};
        auto region = godot::Rect2i(atlas->get_region());
        if (region.size.x <= 0)
            region.size.x = backing->get_width();
        if (region.size.y <= 0)
            region.size.y = backing->get_height();
        return EncodeIconPixels(std::move(key), backing->get_image(), region);
    }
    return EncodeIconPixels(std::move(key), texture->get_image());
}

} // namespace gloss::dndg
