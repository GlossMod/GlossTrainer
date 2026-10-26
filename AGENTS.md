# Repository Guidelines

## Project Structure & Module Organization

GlossTrainer hosts Windows x64 C++20 trainers in per-game directories, using Dear ImGui and MinHook. `BlackMythWukong/src/` contains runtime, input, overlay, and DX11/DX12 renderers; its `gameplay/` subdirectory contains hooks, engine bridges, and embedded catalog data. `BlackMythWukong/validation/` holds regression tests, and `BlackMythWukong/docs/` holds this game's architecture, implementation, installation and verification documents. `Scripts/` provides installation tooling; root `docs/` holds shared release and maintenance documents; `licenses/` holds dependency notices.

`Tests/` contains the local graphics host but is Git-ignored, as are `build/`, `output/`, and `data/`. The root build includes `Tests/`; check its availability in fresh checkouts.

## Generated Validation Artifacts

Store generated validation artifacts in the repository-root `output/` directory, preferably under `output/<task>-<YYYYMMDD>/`. This includes build/configure/test logs, screenshots, UI prototypes and prompts, runtime captures, dumps, JSON results, artifact hashes, and package receipts. Do not create these files or validation-output directories under `docs/`. Configure task scripts and commands to write these artifacts to `output/`.

Keep game-specific architecture, implementation, audit, release templates and verification-summary documents in `<Game>/docs/`; keep the module README as the entry point. Shared cross-game documentation stays in root `docs/`. For Black Myth: Wukong, record conclusions in `BlackMythWukong/docs/verification.md` and link to root raw evidence with paths such as `../../output/<task>-<YYYYMMDD>/results.json`. When moving documents or existing artifact directories, update links and script input/output paths; preserve original captured logs and results.

## Build, Test, and Development Commands

Install Visual Studio 2022 Desktop Development with C++, Windows SDK, and xmake. Run from the repository root:

```powershell
xmake f -p windows -a x64 -m release  # Configure; use debug for debugging
xmake build BlackMythWukong         # Produce build\release\BlackMythWukong.asi
xmake build GlossTrainerGameplayTests
.\build\release\GlossTrainerGameplayTests.exe
xmake build GlossTrainerImeTests
.\build\release\GlossTrainerImeTests.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-install.ps1
```

For local graphics regression, build `GlossTrainerHost` and run `Tests/run-automated.ps1` with `-Renderer dx11` and `dx12`; see `README.md` for arguments. Preview installation with `xmake bi --exedir="<game Win64 directory>" --name=BlackMythWukong --dry-run`. Exit the game before installation.

## Coding Style & Naming Conventions

Use four-space indentation, same-line opening braces, C++20, and UTF-8 source. Follow existing `gloss` namespaces, PascalCase types/functions, camelCase variables, and lowercase underscore-separated `.cpp`/`.hpp` filenames. Keep UI, rendering, and gameplay responsibilities separate. No formatter or linter configuration is committed; match surrounding code.

## Testing Guidelines

Tests use standalone C++ executables with custom `CHECK` assertions, plus PowerShell installer checks. Name test files `*_tests.cpp` and cases `Test...`. Run affected `GlossTrainer*Tests` targets in Debug and Release; use matching output directories. Cover failure paths and state restoration. No numeric coverage threshold is configured. Store raw validation evidence in root `output/` and record summaries and evidence links in the game's `docs/verification.md` (`BlackMythWukong/docs/verification.md` for Black Myth: Wukong); graphics-host success does not establish in-game feature correctness.

## Commit & Pull Request Guidelines

History uses short descriptive subjects, primarily Chinese, without a mandatory prefix. Prefer action-oriented messages such as `修复输入法焦点恢复`. PRs should explain behavior changes, link relevant issues, list validation results, and include light/dark screenshots for UI changes.

## Agent Workflow

Before implementing UI changes, generate and inspect a prototype with `custom-imagegen`; use modern minimal styling supporting light/dark themes. Delegate independent work to `data_tester` (gpt-6-luna), `code_executor` (gpt-6.1-sol), or `planner` (gpt-6-astra). Specify ownership and expected outputs; preserve concurrent edits. The primary agent integrates and verifies results.
