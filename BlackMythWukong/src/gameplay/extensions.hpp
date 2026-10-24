#pragma once
#include "../features.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gloss::gameplay::extensions {
struct Vec3 { double x{}, y{}, z{}; };
struct ItemEntry { int id; const char* name; const char* category; const char* description; int list; };
struct SceneEntry { int id; const char* name; };
struct LandmarkEntry { int scene; const char* name; Vec3 position; };
struct Bookmark { std::string name; Vec3 position; };
struct ObjectLocation { std::uintptr_t address{}; std::string name; Vec3 position; };
struct Status {
    bool ready{}, pending{}, positionValid{};
    Vec3 position;
    std::string message;
    std::vector<ObjectLocation> objects;
};
std::span<const ItemEntry> Items();
std::span<const SceneEntry> Scenes();
std::span<const LandmarkEntry> Landmarks();
Status GetStatus();
features::Result ChangeItem(std::size_t index, int count, bool remove);
features::Result SetCard(std::size_t index,int storyStage,int portraitStage);
features::Result RefreshPosition();
features::Result Teleport(Vec3 position);
features::Result Travel(int sceneId, Vec3 position);
features::Result TravelDefault(int sceneId);
features::Result RefreshObjects();
features::Result TeleportToObject(std::uintptr_t address);
std::vector<Bookmark> Bookmarks();
features::Result SaveBookmark(std::string name, Vec3 position);
features::Result RemoveBookmark(std::size_t index);
// Called by the installer and the verified native-player callback respectively.
void Initialize();
void CapturePlayer(std::uintptr_t player) noexcept;
bool Owns(features::Id id);
features::BackendState Query(features::Id id);
features::Result SetEnabled(features::Id id, bool enabled, double value);
features::Result Apply(features::Id id, double value);
}
