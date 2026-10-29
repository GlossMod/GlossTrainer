# Phantom Blade Zero In-Game Trainer

GlossTrainer v{{VERSION}} · Windows x64

[简体中文](README.md) · [English](readme_en.md)

## Installation

1. Save and exit the game normally, then extract the release ZIP.
2. Copy `version.dll`, `version.ini` and the `scripts` folder into `platform/steam`, alongside that directory's `PBZ.exe`.
3. Start the game and press **Insert** to show or hide the menu. Feature hotkeys are initially unbound; assign them on the Features page.

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

- Infinite Sha Chi, Infinite Dark Source, automatic perfect deflect, automatic perfect dodge and invulnerability.
- Read health, set maximum health and heal, restore health, refill medicine and ultimate resources. Some actions may change save data.
- Live item catalog and icons, category search, quantity confirmation and verified inventory increments. Unique items are limited to one per request; ordinary items to 999.
- Remember verified toggles and restore them when the game and feature are ready. One-time actions are never replayed.
- Custom keyboard hotkeys, Chinese / English interface, light / dark themes, scaling and saved layout.
- Game-specific `PhantomBladeZero/v` update channel and manual download links in Settings and About.

Supported executable: `CL-301524_JKS-17219`, Windows x64, SHA-256 `7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4`. Native calls are disabled on other builds. New combat behavior and scene transitions still require individual in-game checks; see the [verification record](https://github.com/GlossMod/GlossTrainer/blob/master/PhantomBladeZero/docs/verification.md). Item names and descriptions retain the game's language.

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
