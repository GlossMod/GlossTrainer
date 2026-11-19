#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/math_defs.hpp>

#include <cmath>

namespace gloss::dndg {

inline bool KnownFalse(const godot::Variant& value) {
    return value.get_type() == godot::Variant::BOOL && !static_cast<bool>(value);
}

inline bool CardsRevealed(godot::Node* player, godot::Node* opponent) {
    return player && opponent && !player->is_queued_for_deletion() && !opponent->is_queued_for_deletion() &&
        KnownFalse(player->get("moon_shroud_effect_active")) &&
        KnownFalse(opponent->get("moon_shroud_effect_active"));
}

inline bool CardFaceRevealed(godot::Node* card) {
    if (!card || !KnownFalse(card->get("card_facedown")) || !KnownFalse(card->get("card_shrouded")))
        return false;
    // The sprite can appear before the flags during flip-in. The engine also
    // clamps zero scale to epsilon; a collapsed sprite does not hide its face.
    const auto* back = godot::Object::cast_to<godot::Node2D>(
        card->get_node_or_null("ShroudLiftOnHover/ShroudSprite"));
    return !back || !back->is_visible_in_tree() || std::abs(back->get_scale().x) <= godot::CMP_EPSILON;
}

} // namespace gloss::dndg
