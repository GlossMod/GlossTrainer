#pragma once

#include "model.hpp"

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <string_view>

namespace godot {
class SceneTree;
}

namespace gloss::dndg {

// Game thread only. A supported normal encounter returns run/game/player/ready;
// callers must not retain the Dictionary or its objects across frames/threads.
godot::Dictionary EncounterContext(godot::SceneTree* tree);
Snapshot ReadGameSnapshot(godot::SceneTree* tree, UiRequest request);
Catalog ReadGameCatalog(godot::SceneTree* tree, Locale locale);

// Entry::iconKey is a UTF-8 res:// path: Texture2D for cards/charms, SpriteFrames
// (first frame of "default") for unique chips. Resolve on the game thread only
// and cache outside the read path; neither snapshots nor catalogs load textures.
godot::Ref<godot::Texture2D> LoadGameIcon(std::string_view iconKey);

} // namespace gloss::dndg
