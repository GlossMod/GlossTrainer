#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace gloss::features
{
    enum class Id : std::uint8_t
    {
        GodMode,
        InfiniteHealth,
        InfiniteMana,
        InfiniteStamina,
        InfiniteFocus,
        InfiniteGourdUses,
        SpellCooldown,
        TransformationCooldown,
        TransformationDuration,
        InfiniteSpiritEnergy,
        InfiniteVesselEnergy,
        MaximumCriticalChance,
        MaximumPoise,
        PreventAbnormalStatus,
        WillMultiplier,
        ExperienceMultiplier,
        RelicPoints,
        GuaranteedDrop,
        OneHitKill,
        DamageMultiplier,
        DefenseMultiplier,
        StaminaConsumption,
        GameSpeed,
        EditWill,
        EditSelectedItemCount,
        AddExperience,
        EditSparks,
        PlayerSpeed,
        MovementSpeed,
        JumpHeight,
        MaximumHealth,
        MaximumMana,
        MaximumStamina,
        Attack,
        Defense,
        StaminaRecovery,
        CriticalChance,
        CriticalDamage,
        DamageBonus,
        DamageReduction,
        ChillResistance,
        BurnResistance,
        PoisonResistance,
        ShockResistance,
        InfiniteItems,
        Stealth,
        BossStealth,
        NoHitCollision,
        InfiniteBuffDuration,
        InfiniteVesselDuration,
        BulletTime,
        HideHud,
        HidePlayer,
        PauseBackground,
        FreezeWorld,
        ContinuousJump,
        NoGravity,
        FreeCamera,
        FixedCameraDistance,
        SummonCloud,
        UnlockCloud,
        UnlockShrines,
        UnlockBossRush,
        NewGameCycle,
        Count
    };
    inline constexpr std::size_t Count = static_cast<std::size_t>(Id::Count);
    enum class Group : std::uint8_t
    {
        Survival,
        Abilities,
        Combat,
        Resources,
        Movement,
        Attributes,
        Resistance,
        World,
        Camera,
        Unlocks,
        Count
    };
    enum class Kind : std::uint8_t
    {
        Toggle,
        ToggleValue,
        Action
    };
    enum class Availability : std::uint8_t
    {
        Unavailable,
        Waiting,
        Ready,
        Error
    };
    enum Modifier : unsigned
    {
        NoModifier = 0,
        Ctrl = 1,
        Shift = 2,
        Alt = 4
    };
    struct Definition
    {
        Id id;
        Group group;
        Kind kind;
        const char *name;
        const char *description;
        const char *hotkey;
        unsigned virtualKey;
        unsigned modifiers;
        double initialValue;
        double minimum;
        double maximum;
        double step;
        bool integer;
    };
    struct BackendState
    {
        Availability availability = Availability::Unavailable;
        bool enabled{};
        std::string message;
    };
    struct Result
    {
        bool success{};
        std::string message;
    };
    struct Snapshot
    {
        Availability availability = Availability::Unavailable;
        bool enabled{};
        double value{}; // Staged editor value; editing alone never writes to the game.
        std::string message;
        bool actionFailed{};
    };
    // Callbacks are copied under the model mutex and invoked without that mutex.
    // Query must be read-only. SetEnabled/Apply run only after an explicit UI/hotkey action.
    // Backend owns actual enabled state and must report support honestly per feature.
    struct Backend
    {
        std::function<BackendState(Id)> query;
        std::function<Result(Id, bool, double)> setEnabled;
        std::function<Result(Id, double)> apply;
    };
    void RegisterBackend(Backend backend);
    // Serializes with writes, disables active switches once on entry, and never
    // re-enables them on exit. Read-only model calls remain safe in callbacks.
    void SetUpdateRequired(bool required);
    bool UpdateRequired();
    // Monotonic barrier for queued backends; a failed retry does not revive old work.
    std::uint64_t UpdateGeneration();
    std::span<const Definition> Definitions();
    const Definition &GetDefinition(Id id);
    const char *GroupName(Group group);
    Snapshot GetSnapshot(Id id);
    bool SetValue(Id id, double value);
    Result SetEnabled(Id id, bool enabled);
    // Action -> backend.apply. An already enabled ToggleValue -> backend.setEnabled
    // with the staged value. Apply never enables a disabled ToggleValue.
    Result Apply(Id id);
    // Call only on initial key-down (never auto-repeat), with the exact modifier mask.
    // Returns true when the chord belongs to the catalog, even when unsupported.
    bool HandleHotkey(unsigned virtualKey, unsigned modifiers);
    bool HotkeysEnabled();
    void SetHotkeysEnabled(bool enabled);
    struct Hotkey
    {
        unsigned key{}, modifiers{};
        std::string label;
    };
    void InitializeHotkeys();
    Hotkey GetHotkey(Id id);
    Result SetHotkey(Id id, unsigned key, unsigned modifiers);
    void BeginHotkeyCapture(Id id);
    void CancelHotkeyCapture();
    bool CapturingHotkey(Id id);
    bool AnyHotkeyCapture();
    // The menu chord remains available independently of feature hotkey enablement.
    // Input routing owns the visibility change; this model only stores the chord.
    Hotkey GetMenuHotkey();
    Result SetMenuHotkey(unsigned key, unsigned modifiers);
    void BeginMenuHotkeyCapture();
    bool CapturingMenuHotkey();
    Result GetMenuHotkeyResult();
    bool IsMenuHotkey(unsigned key, unsigned modifiers);
    // Complete a previously accepted asynchronous action without invoking backend callbacks.
    void CompleteAction(Id id, Result result);
}
