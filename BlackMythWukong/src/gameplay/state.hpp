#pragma once
#include "../features.hpp"
#include <cstdint>
namespace gloss::gameplay {
bool Enabled(features::Id id) noexcept;
float Value(features::Id id) noexcept;
// All game-memory changes execute inside a captured game-thread hook.
void Initialize();
}
