# Phantom Blade Zero In-Game Trainer

GlossTrainer v{{VERSION}} · Windows x64

[简体中文](README.md) · [English](readme_en.md)

## Installation

1. Save and exit the game normally, then extract the release ZIP.
2. Copy `version.dll`, `version.ini` and the `scripts` folder into `platform/steam`, alongside that directory's `PBZ.exe`.
3. Start the game and press **Insert** (the default) to show or hide the menu. Click the key box under Settings > Menu and layout to record a new combination or restore the default. Ctrl / Shift / Alt are supported; Esc cancels. Changes are saved automatically. Flight up/down defaults to **Page Up / Page Down**, moving 100 units per press. Other new feature hotkeys start unbound. Configure them on the Features page; existing bindings and explicitly cleared bindings are preserved.

```text
PBZ/platform/steam/
  PBZ.exe
  version.dll
  version.ini
  scripts/
    GlossTrainer.asi
```

The ZIP includes Ultimate ASI Loader v10.0.0 x64. Check ownership before replacing a Loader used by other plugins. Exit the game and back up old files before updating. Remove the old `scripts/PBZ.asi` from the loading directory to prevent duplicate loading with `GlossTrainer.asi`. Preserve other plugins.

Configuration and logs are stored in `scripts/GlossTrainer/PBZ/`. No default preferences are shipped; retain your `ui.ini`, `hotkeys.ini`, `features.ini` and `language.ini`. To uninstall, exit the game and remove `scripts/GlossTrainer.asi`. Remove `version.dll` and `version.ini` only if no other plugin uses them.

## Features

- Free map travel without consuming Traversing Charms. A separate Remove teleport restrictions toggle permits travel from restricted locations, including near bells; destinations must still be activated bells.
- Automatically collect supported nearby drops, notes and chests. The default range is 10 m, adjustable from 1–50 m. Collection waits while the character is busy or in combat.
- Native green loot highlights with item icons and distance labels, visible through walls. The default range is 50 m, adjustable from 1–100 m. A known single reward type uses its item icon; unknown or mixed rewards retain a generic collection marker. Opened chests are excluded.
- Adjust camera FOV, distance and vertical offset. Camera distance remains subject to the active camera mode; disabling restores the original values.
- Apply the effects of unequipped accessories in your backpack. Disabling removes only effects added by the trainer.
- Infinite health, Sha Chi, pure Sha Chi segments and Dark Source; automatic perfect deflect, automatic perfect dodge and invulnerability.
- Damage, defense, poise damage and pure Sha Chi gain multipliers; one-hit kill, instant break, stealth and weapon-switch cooldown.
- Sha Chi cost multiplier for heavy attacks and Shachi ability Blueprint costs. Damage-induced Sha Chi loss is unchanged; native FengLei conversion is not covered.
- No medicine or shop-stock consumption. Infinite consumables currently protects only verified Traversing Charms; story items, materials and permanent stat items are consumed normally.
- Movement, player, AI and global game speed, jump height, gravity and FOV. Noclip flight uses configurable Page Up / Page Down keys, default step 100. Flight stays enabled when the exit capsule is obstructed.
- Edit remaining days and unlock the 66-day difficulty; progress is saved by the game.
- Read health, set maximum health and heal, restore health and refill medicine. Some one-time actions may change save data. Old GM actions removed from this game build are shown as unavailable.
- Live item catalog and icons, category search, quantity confirmation and verified inventory increments. Unique items are limited to one per request; ordinary items to 999.
- Consciousness Dust no longer has the trainer's 999-per-request limit. Enter the desired amount directly; the resulting total must fit the game's signed 32-bit integer range (up to 2,147,483,647).
- Cached entries and resolved names appear first, with descriptions filled in batches. Granting remains disabled until the current scene's catalog has been validated.
- Remember verified toggles and multiplier values and restore them when the game and feature are ready. Failed parameter edits keep the last successful value. One-time actions are never replayed.
- Custom keyboard hotkeys, Chinese / English interface, light / dark themes, scaling and saved layout.
- Game-specific `PhantomBladeZero/v` update channel and manual download links in Settings and About.

Find the two distance sliders under Features > Resources. Drag a slider and click its Apply button; successful values are saved with the toggles. Other parameters with a defined range also use sliders. Ctrl+click allows precise numeric entry.

This version processes nearby targets, the item catalog and images in batches, reduces repeated queries and periodic disk writes, and loads item icons asynchronously. Highlights show at most the nearest 32 supported targets already loaded and registered by the game. They do not include the V interface's off-screen edge arrows or alter manual V use and quest tracking.

Supported executable: `CL-302256_JKS-17280`, Windows x64, SHA-256 `4415933ce33476f14c837abb61a1b60af8b78cd81783896501913a7aac69e423`. Native calls are disabled on other builds. Native green markers have been observed in-game. Per-item icon selection and opened-chest filtering have passed builds and offline regression tests; their final visual behavior and performance in dense scenes still await in-game acceptance. See the [verification record](https://github.com/GlossMod/GlossTrainer/blob/master/PhantomBladeZero/docs/verification.md) for the scope of other combat and scene-transition checks. Item names and descriptions retain the game's language.

## About

Current version: v{{VERSION}}. Latest version: check the About page. Missing releases and network errors are reported without restricting features. Updates are downloaded and installed manually.

## Credits

Author: 小莫 (Xiaomo)

- [Main LICENSE · GNU GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)
- [Source code](https://github.com/GlossMod/GlossTrainer)
- [Phantom Blade Zero downloads](https://github.com/GlossMod/GlossTrainer/releases?q=PhantomBladeZero%2Fv&expanded=true)
- [Sponsor the author](https://sponsor.aoe.top/)
- [Request a game trainer](https://www.glossmod.com/commissions)

## licenses

{{LICENSES}}
