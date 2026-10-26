# Black Myth: Wukong In-Game Trainer

GlossTrainer v{{VERSION}} · Windows x64

[简体中文 (default)](README.md) · [English](readme_en.md)

## Installation

1. Exit the game normally and extract the release ZIP.
2. Copy `version.dll`, `version.ini` and the `scripts` folder together into the game's `b1\Binaries\Win64` directory, alongside `b1-Win64-Shipping.exe`.
3. Start the game and press **Insert** to show or hide the menu. Customize the menu key under **Settings → Hotkeys**. All feature hotkeys are unbound by default; assign only the ones you need.

Installed layout:

```text
BlackMythWukong/
└─ b1/Binaries/Win64/
   ├─ b1-Win64-Shipping.exe
   ├─ version.dll
   ├─ version.ini
   └─ scripts/
      └─ GlossTrainer.asi
```

The ZIP includes Ultimate ASI Loader v10.0.0 x64, so installation does not require downloading the Loader. If `version.dll` or `version.ini` already belongs to another plugin, check its ownership before replacing it. Preserve other plugins and their files.

To update, exit the game before copying the new files. The trainer creates configuration and logs under `scripts\GlossTrainer` at runtime. The ZIP does not include default configuration, so updates preserve your theme, scale and hotkeys.

To uninstall, exit the game and remove `scripts\GlossTrainer.asi`. Remove `version.dll` and `version.ini` only after confirming that they are used solely by this trainer. You may retain configuration and logs in `scripts\GlossTrainer`; do not delete the entire `scripts` folder.

Download updates from the [Black Myth: Wukong releases](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true) and verify the ZIP against its matching `.SHA256SUMS.txt` file. Release tags start with `BlackMythWukong/v`; update checks compare only this game's stable releases. A successfully confirmed newer version requires a manual update before using trainer features. A network failure only displays “Could not check for updates” and does not restrict use. The trainer does not download or install updates automatically.

## Features

- **Survival and spells:** health, mana, stamina, focus, gourd uses, and options for spells, transformations, spirits and vessels.
- **Combat and attributes:** damage, defense, critical hits, tenacity, status effects, attribute limits and four resistances, across 64 searchable, categorized trainer features.
- **Resources and movement:** Will, experience, Sparks, relic points, drops, movement speed, jump height and game speed.
- **Item tools:** a catalog of 1,160 entries with search, quantity selection, and category-specific add/remove operations. Individual removal of spirits, spells and journal entries is not implemented.
- **Teleport tools:** 32 scenes and 284 landmarks, grouped by scene in collapsible sections, with search, coordinate movement, location bookmarks and scene-object operations.
- **Seventy-Two Transformations (experimental):** a searchable character catalog, per-character skill lists, optional custom skill names, keyboard/mouse shortcut combinations, saved camera distance per character, and an option to restore Wukong. Unverified characters may be tried. Some characters lack movement animations and slide while moving; special characters may have incomplete abilities.
- **Interface and settings:** light/dark themes, UI scaling, Chinese text input, custom menu and feature hotkeys, saved configuration, and Simplified Chinese / English interfaces.

Feature compatibility depends on the game version; refer to the availability shown in the current menu. Item, story and cross-scene operations may change save progress. Back up your saves before using them.

## Credits

**By: 小莫**

## About

- **Current version:** v{{VERSION}}.
- **Latest version:** see the update result on the trainer's **About** page, or visit the [game's GitHub releases](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true). This document records the bundled version, not a live latest-version value.
- **Author:** 小莫.
- **Main LICENSE:** [GNU GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE).
- **Source repository:** [GlossMod / GlossTrainer](https://github.com/GlossMod/GlossTrainer).
- **Sponsor the author:** [sponsor.aoe.top](https://sponsor.aoe.top/).
- **Request a game trainer:** [GlossMod commissions](https://www.glossmod.com/commissions).

## licenses

GlossTrainer is licensed under GNU GPL v3; [source for this version](https://github.com/GlossMod/GlossTrainer/tree/BlackMythWukong/v{{VERSION}}). Complete license texts and copyright notices for the project and bundled components follow.

{{LICENSES}}
