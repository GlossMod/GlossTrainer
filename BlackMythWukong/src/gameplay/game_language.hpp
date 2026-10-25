#pragma once

namespace gloss::gameplay::game_language
{
    // Called by the engine frame callback only; never calls engine from rendering.
    void Tick();
}
