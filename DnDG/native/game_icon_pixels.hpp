#pragma once

#include "icons.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/rect2i.hpp>

#include <optional>

namespace gloss::dndg {

// Game-thread helpers. EncodeIconPixels uses a scratch Image (which it may
// decompress/resize). It never modifies the game's Texture2D resource itself.
IconPixels EncodeIconPixels(std::string key, godot::Ref<godot::Image> image,
    std::optional<godot::Rect2i> region = std::nullopt);
IconPixels ReadGameIconPixels(std::string key);

} // namespace gloss::dndg
