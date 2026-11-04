#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace gloss::gameplay {
    struct LootMarker {
        float x{}, y{};
        std::string label;
        float distance{}; // Metres; script converts Unreal centimetres before publishing.
    };
    // Producer is the verified game-thread script tick. Consumer copies a short
    // snapshot; stale markers expire without reading or retaining any UObject.
    bool PublishLootMarkersJson(std::string_view json);
    void ClearLootMarkers();
    std::vector<LootMarker> GetLootMarkers();
}
