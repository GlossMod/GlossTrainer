#pragma once
#include "../features.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gloss::gameplay::transformation
{
    enum class Support { Verified, Experimental, Unverified, Unavailable };
    enum class Phase { Idle, Loading, Initializing, Active, Restoring, RecoveryRequired };
    enum class Action { LightAttack, HeavyAttack, Dodge, Skill1, Skill2, Skill3, Skill4, Special };

    struct CharacterEntry
    {
        std::string id, name, category, path, form;
        Support support{Support::Unverified};
        std::string reason;
        bool discovered{};
        int resourceId{}, overrideId{};
    };
    struct SkillEntry
    {
        int id{};
        std::string name;
        bool available{};
        std::string reason;
    };
    struct SkillSnapshot
    {
        bool inspected{};
        std::vector<SkillEntry> skills;
    };
    struct SkillSlot
    {
        std::string name;
        Action action{Action::LightAttack};
        std::vector<int> skills;
    };
    struct SkillBinding
    {
        int skillId{};
        std::string customName;
        unsigned key{}, modifiers{};
    };
    struct Capabilities
    {
        bool catalog{}, spawn{}, skills{}, input{}, movement{}, recovery{}, isolation{};
    };
    struct Status
    {
        bool ready{}, pending{}, catalogComplete{};
        Phase phase{Phase::Idle};
        std::string activeId, inspectedId, message, catalogMessage;
        Capabilities capabilities;
        std::uint64_t generation{};
    };

    // Public calls only queue work. Engine objects are read and changed by Tick
    // on the shared game-thread callback; returned collections are snapshots.
    void Initialize();
    void Tick(std::uintptr_t player, std::uintptr_t controller, std::uintptr_t world, std::uint64_t generation, bool controlsAvailable = true);
    void ContextLost();
    Status GetStatus();
    std::vector<CharacterEntry> Catalog();
    features::Result RequestCatalogRefresh();
    features::Result Inspect(std::string_view id);
    features::Result Begin(std::string_view id);
    features::Result Restore();
    std::vector<SkillEntry> Skills();
    SkillSnapshot SkillsFor(std::string_view id);
    std::vector<SkillSlot> Slots(std::string_view id);
    features::Result SetSlots(std::string_view id, std::vector<SkillSlot> slots);
    features::Result TriggerSlot(std::size_t index);
    // Per-character records use actual skill IDs. Empty names preserve the game
    // name, and key == 0 leaves a skill unbound. No fixed action-slot limit.
    std::vector<SkillBinding> Bindings(std::string_view id);
    features::Result SetBindings(std::string_view id, std::vector<SkillBinding> bindings);
    features::Result TriggerSkill(int skillId);
    bool IsActive();
    bool NeedsExclusiveControl();
    const char *SupportName(Support support);
    const char *PhaseName(Phase phase);
    const char *ActionName(Action action);
}
