#pragma once

#include "model.hpp"

namespace godot { class SceneTree; }

namespace gloss::dndg {

// Must be called on the game thread. Commands contain snapshot identities, not
// indexes; no engine object is kept across commands or sent to the render thread.
Feedback ApplyGameCommand(godot::SceneTree* tree, const Command& command);

} // namespace gloss::dndg
