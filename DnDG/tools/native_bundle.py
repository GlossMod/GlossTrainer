"""Build and launch the DnDG native ASI in a separate, local-only bundle.

The EXE, Steam DLL, saves and installed PCK are never patched. The derived PCK
only bootstraps the C++ GDExtension (and selects D3D12 at launch). Do not share it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from build import DEFAULT_GAME, GAME_NAME, GAME_VERSION, inside, publish_bundle
from launch import require_game_stopped
from pck import Pack, bool_variant, settings_decode, settings_encode, sha256, string_variant, write_pack

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL_EXE = "2b1283b94097c32c8e87c3b3e1bd6e0c07fdb80b2685d5235cc7ba32bcc78d09"
LOADER_SHA = "3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7"
LOADER_INI = b"[GlobalSets]\r\nDontLoadFromDllMain=1\r\nLoadPlugins=1\r\nLoadFromScriptsOnly=1\r\nLoadRecursively=0\r\nDebugLog=0\r\n"
BRIDGE_PATH = "gloss_trainer_native/bridge.gd"
EXTENSION_PATH = "gloss_trainer_native/bridge.gdextension"
EXTENSION_LIST = ".godot/extension_list.cfg"
BRIDGE_SCRIPT = b'''extends Node
func _ready() -> void:
    if not ClassDB.can_instantiate("GlossTrainerBridge"):
        push_error("Native GlossTrainerBridge class unavailable")
        return
    var bridge: Node = ClassDB.instantiate("GlossTrainerBridge")
    bridge.name = "Bridge"
    add_child(bridge)
'''
SMOKE_SCENE = b'''[gd_scene load_steps=2 format=3]
[ext_resource type="Script" path="res://gloss_trainer_native/smoke.gd" id="1"]
[node name="NativeSmoke" type="Node"]
script = ExtResource("1")
'''
SMOKE_SCRIPT = b'''extends Node
func _ready() -> void:
    process_mode = Node.PROCESS_MODE_ALWAYS
    var bridge := get_node_or_null("/root/GlossTrainerNative/Bridge")
    if bridge == null or not bridge.is_class("GlossTrainerBridge") or not bridge.is_processing():
        push_error("Native C++ bridge autoload was not initialized")
        get_tree().quit(2)
        return
    print("GLOSS_NATIVE_BRIDGE_READY=" + str(bridge.get_class()))
    print("GLOSS_NATIVE_USER_DIR=" + OS.get_user_data_dir())
    await get_tree().create_timer(3.0).timeout
    print("GLOSS_NATIVE_SMOKE_COMPLETE")
    get_tree().quit(0)
'''
BASE_FILES = {"DnDG_64.exe", "DnDG_64.pck", "steam_api64.dll", "dinput8.dll", "dinput8.ini", "scripts/GlossTrainer.asi"}


def descriptor(module_path: Path) -> bytes:
    # Absolute, JSON-escaped string is also valid in Godot's ConfigFile syntax.
    # The loader and engine must refer to the exact same module on disk.
    library = json.dumps(module_path.as_posix(), ensure_ascii=False)
    return ("[configuration]\nentry_symbol = \"GlossTrainerGodotInit\"\n"
            "compatibility_minimum = \"4.6\"\nreloadable = false\n\n"
            f"[libraries]\nwindows.x86_64 = {library}\n").encode()


def read_loader(path: Path) -> bytes:
    if sha256(path) != LOADER_SHA:
        raise ValueError("Expected the repository-pinned Ultimate ASI Loader v10.0.0 x64 ZIP")
    with zipfile.ZipFile(path) as archive:
        if archive.namelist() != ["dinput8.dll"]:
            raise ValueError("Unexpected layout of Ultimate ASI Loader archive")
        return archive.read("dinput8.dll")


def build(args):
    game = args.game_dir.resolve(strict=True)
    output = args.output.resolve()
    if inside(output, game) or inside(game, output):
        raise ValueError("Output must not be in or contain the Steam installation")
    bundle = output / "bundle"
    if bundle.exists():
        raise ValueError("Output bundle already exists; never overwriting a native bundle")
    source_exe = game / "DnDG_64.exe"
    source_hashes = {name: sha256(game / name) for name in ("DnDG_64.exe", "DnDG_64.pck", "steam_api64.dll")}
    if source_hashes["DnDG_64.exe"] != ORIGINAL_EXE:
        raise ValueError("Original EXE differs from the known DnDG 2.0.8.1 Windows x64 build")
    pack = Pack(game / "DnDG_64.pck")
    settings = settings_decode(pack.read("project.binary"))
    if settings.get("application/config/name") != string_variant(GAME_NAME) or settings.get("application/config/version") != string_variant(GAME_VERSION):
        raise ValueError("Original PCK does not match the supported game version")
    critical = ("application/config/use_custom_user_dir", "application/config/custom_user_dir_name",
                "application/config/disable_project_settings_override", "autoload/GlossTrainerNative")
    if any(key.startswith(prefix + ".") for key in settings for prefix in critical):
        raise ValueError("Feature-specific settings could override native bundle isolation")
    if any(name in pack.entries for name in ("project.godot", EXTENSION_LIST, BRIDGE_PATH, EXTENSION_PATH)):
        raise ValueError("Original PCK has a conflicting project or extension manifest")
    asi = args.asi.resolve(strict=True)
    if asi.name != "GlossTrainer.asi":
        raise ValueError("Expected a build named GlossTrainer.asi")
    module = asi.read_bytes()
    # Validate symbol presence at the real launch as well; these probes catch
    # common mistakes like selecting the standalone graphics-only build early.
    if len(module) < 0x40 or module[:2] != b"MZ" or any(symbol not in module for symbol in
            (b"InitializeASI\0", b"GlossTrainerGodotInit\0")):
        raise ValueError("ASI must include both the Loader and GDExtension exports")
    proxy = read_loader(args.loader)
    identity = hashlib.sha256(str(output).encode()).hexdigest()[:16]
    user_dir = f"GlossTrainer/DnDG/native/{identity}"
    pack.verify()
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": 1, "layout": "native-asi-d3d12", "local_only": True, "game_dir": str(game),
                "user_dir": user_dir, "original": source_hashes, "packs": {}}
    with tempfile.TemporaryDirectory(prefix=".native-", dir=output) as temporary:
        staging = Path(temporary)
        for mode in ("normal", "smoke"):
            directory = staging / mode
            (directory / "scripts").mkdir(parents=True)
            shutil.copyfile(source_exe, directory / "DnDG_64.exe")
            shutil.copyfile(game / "steam_api64.dll", directory / "steam_api64.dll")
            (directory / "dinput8.dll").write_bytes(proxy)
            (directory / "dinput8.ini").write_bytes(LOADER_INI)
            (directory / "scripts/GlossTrainer.asi").write_bytes(module)
            base = dict(settings) if mode == "normal" else {
                "application/config/name": string_variant("GlossTrainer Native Smoke"),
                "application/config/version": string_variant(GAME_VERSION),
                "application/run/main_scene": string_variant("res://gloss_trainer_native/smoke.tscn"),
                "rendering/renderer/rendering_method": string_variant("gl_compatibility"),
            }
            base["autoload/GlossTrainerNative"] = string_variant("*res://" + BRIDGE_PATH)
            base["application/config/use_custom_user_dir"] = bool_variant(True)
            base["application/config/custom_user_dir_name"] = string_variant(user_dir + ("/smoke" if mode == "smoke" else ""))
            base["application/config/disable_project_settings_override"] = bool_variant(True)
            files = {
                "project.binary": settings_encode(base),
                BRIDGE_PATH: BRIDGE_SCRIPT,
                EXTENSION_PATH: descriptor(bundle / mode / "scripts/GlossTrainer.asi"),
                EXTENSION_LIST: ("res://" + EXTENSION_PATH + "\n").encode(),
            }
            if mode == "smoke":
                files.update({"gloss_trainer_native/smoke.tscn": SMOKE_SCENE,
                              "gloss_trainer_native/smoke.gd": SMOKE_SCRIPT,
                              ".godot/global_script_class_cache.cfg": b"list=[]\n"})
            write_pack(directory / "DnDG_64.pck", files, pack if mode == "normal" else None)
            Pack(directory / "DnDG_64.pck").verify()
            manifest["packs"][mode] = {name: sha256(directory / name) for name in sorted(BASE_FILES)}
        if any(sha256(game / name) != value for name, value in source_hashes.items()):
            raise ValueError("Original game changed during build")
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        publish_bundle(staging, bundle)
    print(bundle)


def launch(args):
    if args.headless and not args.smoke:
        raise ValueError("--headless is only available for the isolated --smoke pack")
    bundle = args.bundle.resolve(strict=True)
    manifest = json.loads((bundle / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("local_only") is not True or manifest.get("layout") != "native-asi-d3d12":
        raise ValueError("Not a native DnDG bundle")
    mode = "smoke" if args.smoke else "normal"
    game = Path(manifest["game_dir"]).resolve(strict=True)
    if inside(bundle, game) or inside(game, bundle):
        raise ValueError("Native bundle is not isolated from Steam installation")
    for name in ("DnDG_64.exe", "DnDG_64.pck", "steam_api64.dll"):
        if sha256(game / name) != manifest["original"][name]:
            raise ValueError("Original file has changed: " + name)
    directory = (bundle / mode).resolve(strict=True)
    if directory != bundle / mode or set(manifest["packs"][mode]) != BASE_FILES:
        raise ValueError("Invalid bundle topology")
    if {file.name for file in directory.iterdir()} != {"DnDG_64.exe", "DnDG_64.pck", "steam_api64.dll", "dinput8.dll", "dinput8.ini", "scripts"}:
        raise ValueError("Unexpected sidecar files")
    if {file.name for file in (directory / "scripts").iterdir()} != {"GlossTrainer.asi"}:
        raise ValueError("Unexpected sidecar scripts")
    for name, value in manifest["packs"][mode].items():
        copied = (directory / name).resolve(strict=True)
        if copied != directory / name or sha256(copied) != value:
            raise ValueError("Unexpected file path or hash: " + name)
    pack = Pack(directory / "DnDG_64.pck")
    settings = settings_decode(pack.read("project.binary"))
    expected_user = "GlossTrainer/DnDG/native/" + hashlib.sha256(str(bundle.parent).encode()).hexdigest()[:16]
    if args.smoke:
        expected_user += "/smoke"
        if settings.get("application/run/main_scene") != string_variant("res://gloss_trainer_native/smoke.tscn") or \
                [key for key in settings if key.startswith("autoload/")] != ["autoload/GlossTrainerNative"]:
            raise ValueError("Smoke scene is not isolated from original autoloads")
    if settings.get("application/config/custom_user_dir_name") != string_variant(expected_user) or \
            settings.get("application/config/use_custom_user_dir") != bool_variant(True) or \
            settings.get("application/config/disable_project_settings_override") != bool_variant(True) or \
            pack.read(EXTENSION_PATH) != descriptor(directory / "scripts/GlossTrainer.asi"):
        raise ValueError("Launch isolation or ASI path does not match; rebuild instead of moving the bundle")
    # Force only the rendering backend; the normal pack retains the original
    # stretch/resolution settings and viewport nearest sampling.
    command = [str(directory / "DnDG_64.exe")]
    if args.headless:
        command += ["--headless"]
    else:
        command += ["--rendering-driver", "d3d12", "--rendering-method", "mobile" if args.smoke else "forward_plus"]
        if args.smoke:
            command += ["--windowed", "--resolution", "1280x720"]
    print(json.dumps({"command": command, "cwd": str(directory), "user_dir": expected_user}, indent=2), flush=True)
    if args.dry_run:
        return 0
    require_game_stopped()
    if args.smoke:
        proc = subprocess.run(command, cwd=directory, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=20,
                              creationflags=subprocess.CREATE_NO_WINDOW if args.headless else 0)
        log = proc.stdout.decode("utf-8", errors="replace")
        print(log)
        lines = [line.strip() for line in log.splitlines()]
        user_dirs = [line.removeprefix("GLOSS_NATIVE_USER_DIR=") for line in lines
                     if line.startswith("GLOSS_NATIVE_USER_DIR=")]
        if (proc.returncode != 0 or (not args.headless and not any(line.startswith("D3D12 ") for line in lines)) or
                lines.count("GLOSS_NATIVE_BRIDGE_READY=GlossTrainerBridge") != 1 or
                lines.count("GLOSS_NATIVE_SMOKE_COMPLETE") != 1 or
                len(user_dirs) != 1 or not user_dirs[0].replace("\\", "/").endswith("/" + expected_user) or
                "ERROR:" in log):
            renderer = "headless launch" if args.headless else "D3D12"
            raise ValueError(f"Native smoke did not confirm {renderer}, bridge readiness, isolated user dir and clean exit")
        return 0
    return subprocess.call(command, cwd=directory, stdin=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    builder = sub.add_parser("build")
    builder.add_argument("--game-dir", type=Path, default=DEFAULT_GAME)
    builder.add_argument("--output", type=Path, required=True)
    builder.add_argument("--asi", type=Path, default=ROOT / "build/DnDG/release/GlossTrainer.asi")
    builder.add_argument("--loader", type=Path, required=True, help="Verified Ultimate-ASI-Loader_x64.zip (v10.0.0)")
    runner = sub.add_parser("launch")
    runner.add_argument("--bundle", type=Path, required=True)
    runner.add_argument("--smoke", action="store_true")
    runner.add_argument("--headless", action="store_true", help="Run the isolated smoke pack without a window")
    runner.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "build":
            build(args)
            return 0
        return launch(args)
    except (KeyError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Native bundle refused: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
