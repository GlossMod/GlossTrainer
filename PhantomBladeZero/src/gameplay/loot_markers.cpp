#include "loot_markers.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <mutex>

namespace gloss::gameplay {
    namespace {
        std::mutex markerMutex;
        std::vector<LootMarker> markers;
        std::chrono::steady_clock::time_point published;
    }
    bool PublishLootMarkersJson(std::string_view text) {
        std::vector<LootMarker> next;
        try {
            if (text.size() > 16384) { ClearLootMarkers(); return false; }
            const auto data = nlohmann::json::parse(text);
            if (!data.is_array() || data.size() > 32) { ClearLootMarkers(); return false; }
            for (const auto& item : data) {
                if (!item.is_object() || !item.contains("x") || !item.contains("y") ||
                    !item.contains("distance") || !item.contains("label") ||
                    !item["x"].is_number() || !item["y"].is_number() || !item["distance"].is_number() ||
                    !item["label"].is_string()) { ClearLootMarkers(); return false; }
                LootMarker marker{item["x"].get<float>(), item["y"].get<float>(),
                    item["label"].get<std::string>(), item["distance"].get<float>()};
                if (!std::isfinite(marker.x) || !std::isfinite(marker.y) || !std::isfinite(marker.distance) ||
                    marker.x < 0 || marker.x > 100000 || marker.y < 0 || marker.y > 100000 ||
                    marker.distance < 0 || marker.distance > 1000 ||
                    (marker.label != "Loot" && marker.label != "Chest" && marker.label != "Note")) {
                    ClearLootMarkers(); return false;
                }
                next.push_back(std::move(marker));
            }
        } catch (...) { ClearLootMarkers(); return false; }
        std::lock_guard lock(markerMutex);
        markers = std::move(next);
        published = std::chrono::steady_clock::now();
        return true;
    }
    void ClearLootMarkers() {
        std::lock_guard lock(markerMutex);
        markers.clear();
        published = {};
    }
    std::vector<LootMarker> GetLootMarkers() {
        std::lock_guard lock(markerMutex);
        if (std::chrono::steady_clock::now() - published > std::chrono::milliseconds(750)) return {};
        return markers;
    }
}
