# DnDG packaging tools

For the public Windows ZIP and Steam installer, see the [release guide](../docs/release-readme.md) and [build guide](../docs/native.md). The following sections describe the local-only prototype packaging workflow.

Python 3.11+ standard library only. Reads and validates the installed unencrypted
Godot 4.6.3 PCK v3 from DnDG 2.0.8.1 and publishes a **local-only Godot sidecar export layout**.
The stock installation is read-only. No executable patching, injection,
ownership-check changes, Steam environment changes, steam_appid.txt, or path
override is used.

## Integration

```powershell
py -3 DnDG/tools/build.py --game-dir '<Steam 游戏安装目录>'
py -3 output/dndg-local/bundle/launch.py --dry-run
py -3 output/dndg-local/bundle/launch.py --smoke
```

Builder options:

- `--src-dir`: defaults to repository `DnDG/src`. Recursively includes `.gd`
  files as `res://gloss_trainer/<relative-path>`. Requires `trainer_overlay.gd`.
  Explicit source preload/load paths must use this prefix. Resource collisions,
  escaping symlinks and root `__*` script names are rejected.
- `--output`: defaults to repository `output/dndg-local`. An existing
  `bundle/` is never overwritten; choose a new output directory when rebuilding.
- `--use-game-saves`: explicitly opts the normal pack into original save
  settings. Launch additionally requires `--allow-game-saves`. Smoke remains
  isolated. No saves are copied.

Manifest schema is **2**, layout `godot-sidecar`. Schema 1 bundles require
rebuilding. Each of `bundle/normal/` and `bundle/smoke/` contains exactly:

```text
DnDG_64.exe       # Byte-identical stock executable copy
DnDG_64.pck       # Generated normal or independent smoke pack
steam_api64.dll   # Byte-identical stock DLL copy
```

The bundle root also contains `manifest.json`, `launch.py`, `pck.py` and
`LOCAL-ONLY.txt`. Logs and receipts go to `bundle/logs/`.

Each mode runs its same-named executable from that mode's directory. The default
command has **no arguments**. Godot discovers the adjacent same-named PCK
normally, and the copied Steam DLL is present for relative loading. The installed
exe disables --main-pack; ordinary sidecar loading was independently proven
with the unmodified original executable on November 18, 2026.

**Do not redistribute bundles.** They contain proprietary executables, DLLs and
original assets. Distribute tools and trainer sources only; each owner builds
locally. No ASI or new DLL is compiled.

## Isolation and launch checks

Normal settings preserve existing autoload order and append
`autoload/GlossTrainer` as `*res://gloss_trainer/trainer_overlay.gd`. Other Variant
payloads are preserved byte-for-byte except explicit isolation and override
changes. External settings overrides are disabled.

Default Godot user:// is `%APPDATA%/GlossTrainer/DnDG/<bundle-id>`, with a separate
`/smoke` directory. The identity depends on output path and source hashes;
rebuilding elsewhere creates another save location. This isolates Godot saves;
it does not establish how original scripts use Steam Cloud or absolute paths.
Those scripts never execute in the smoke pack.

Every launch validates original exe/PCK/DLL hashes, copied exe/DLL hashes,
selected PCK hash, and every packaged trainer source hash. Exact sidecar paths
and binary isolation settings are checked. Extra files in a mode directory are
rejected to avoid accidental external project settings or libraries. Save
additional evidence outside the mode folders.

**Normal launch always requires every DnDG_64.exe process to be stopped.** The
launcher enumerates Windows processes using Toolhelp through stdlib ctypes,
checks twice before spawning, and refuses on any matching PID or enumeration
error. Stock and sidecar instances both count. Existing processes are never
stopped. External processes can independently start the game after these
checks; this does not lock Steam or the stock executable.

`--dry-run` validates and prints without spawning or checking game availability.
`launch.py` without `--smoke` is the normal interface. The first integrated
bundle initialized Steam and created isolated progress; in-match actions were
not exercised. If launch fails or reroutes to Steam, there is no automatic
fallback, retry, ownership bypass or Steam manipulation.

## Safe smoke

Smoke contains supplied trainer sources, minimal settings, an empty main scene
and a one-second exit script. It contains no original autoloads, original main
scene or game assets. An empty `.godot/global_script_class_cache.cfg` avoids
an exported-runtime missing-cache error.

Smoke sources must work without original game globals/resources; sources using
game-specific preloads may fail. Use a temporary trainer_overlay.gd stub to
validate packaging independently of backend/UI. Default smoke can run while the
stock game is open because no game/Steam autoload runs. This was verified with
a minimal stub, not arbitrary supplied scripts.

`--smoke` starts without arguments. Optional `--headless` is smoke-only and adds
only that rendering option. The parent enforces `--timeout` (default 30 seconds,
range 1..120), killing only its own spawned PID on expiry. Success requires
autoload/completion markers, the expected isolated user directory, exit code 0
and no runtime/script error markers. Missing markers, including a Steam reroute,
fail without retries. Stdout and JSON receipts go to bundle/logs; runtime-owned
logs remain under the isolated Godot user folder.

## Verification

```powershell
py -3 -B -m unittest discover -s DnDG/tools -p '*_tests.py' -v
```

Sidecar evidence is under repository output/dndg-trainer-20261118/sidecar-verification,
with aggregate receipt output/dndg-trainer-20261118/sidecar-results.json. Stock
Godot 4.6.3 smoke success proves same-name PCK discovery, autoload, isolated user
directory and clean exit. The original game PID remained running during the
initial packaging smoke. For integrated startup, save integrity, and adapter/UI
coverage, see the [verification record](../docs/verification.md).

Earlier --main-pack failure and Godot 4.7.2 reference evidence remain under
packaging-verification and packaging-final; they describe a different launch
mechanism, not the current sidecar result.

Format references saved under root evidence format-sources are from
Godot tag 4.6: core/io/file_access_pack.cpp, core/io/pck_packer.cpp,
core/io/marshalls.cpp, core/config/project_settings.cpp, main/main.cpp and
platform/windows/os_windows.cpp. Unknown header versions/flags, encryption,
sparse/delta/removal entries, duplicate/traversing paths, overlapping or
truncated payloads and settings fail closed.
