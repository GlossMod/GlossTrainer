#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gloss::dndg {

inline constexpr int IconMaxSide = 62;
struct IconPixels {
    std::string key;
    int width{};
    int height{};
    std::vector<std::uint8_t> rgba;
};

// Work is requested on the renderer thread and resolved by the scene thread.
std::string TakeIconRequest();
void PublishIconPixels(IconPixels value);
void PumpIconAtlas();
bool DrawItemIcon(const std::string& key, float left, float top, float right, float bottom);

} // namespace gloss::dndg
