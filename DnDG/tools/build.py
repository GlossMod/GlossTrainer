"""Build a LOCAL-ONLY side-by-side pack. Requires Python 3.11+; no Godot SDK."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

from pck import (Pack, bool_variant, path_overrides_disabled, settings_decode,
                 settings_encode, sha256, string_variant, write_pack)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "output/dndg-local"
GAME_VERSION = "2.0.8.1"
TRAINER_VERSION = "0.1.4"
GAME_NAME = "Dungeons & Degenerate Gamblers"
DEFAULT_GAME = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common" / GAME_NAME

SMOKE_SCRIPT = '''extends Node
func _ready() -> void:
    print("GLOSS_SMOKE_USER_DIR=" + OS.get_user_data_dir())
    await get_tree().process_frame
    if get_node_or_null("/root/GlossTrainer") == null:
        push_error("GlossTrainer autoload missing")
        get_tree().quit(2)
        return
    print("GLOSS_SMOKE_AUTOLOAD_OK")
    await get_tree().create_timer(1.0).timeout
    print("GLOSS_SMOKE_COMPLETE")
    get_tree().quit(0)
'''
SMOKE_SCENE = '''[gd_scene load_steps=2 format=3]
[ext_resource type="Script" path="res://gloss_trainer/__smoke.gd" id="1"]
[node name="GlossSmoke" type="Node"]
script = ExtResource("1")
'''


def inside(path, root):
    return path == root or root in path.parents


def publish_bundle(stage, bundle):
    # Windows scanners can briefly hold a just-copied executable open. Retry
    # only publication, never execution, and never replace an existing bundle.
    for delay in (0.05, 0.1, 0.2, 0.4, 0.8, None):
        if bundle.exists():
            raise ValueError("Bundle appeared during build; refusing overwrite")
        try:
            stage.rename(bundle)
            return
        except PermissionError:
            if delay is None:
                raise
            time.sleep(delay)


def build(args):
    game = args.game_dir.resolve(strict=True)
    source = args.src_dir.resolve(strict=True)
    output = args.output.resolve()
    if inside(output, game) or inside(game, output):
        raise ValueError("Output must be separate from the game directory")
    if inside(output, source) or inside(source, output):
        raise ValueError("Output must be separate from trainer sources")
    original_path = game / "DnDG_64.pck"
    executable = game / "DnDG_64.exe"
    steam = game / "steam_api64.dll"
    for required in (original_path, executable, steam):
        if not required.is_file():
            raise ValueError("Missing original game file: " + str(required))
    pack = Pack(original_path)
    if "project.godot" in pack.entries:
        raise ValueError("Text project settings would take precedence; unsupported original pack")
    print("Validating original PCK payloads...", flush=True)
    original_hash = sha256(original_path)
    pack.verify()
    settings = settings_decode(pack.read("project.binary"))
    if settings.get("application/config/name") != string_variant(GAME_NAME):
        raise ValueError("PCK is not the Dungeons & Degenerate Gamblers project")
    if settings.get("application/config/version") != string_variant(GAME_VERSION):
        raise ValueError(f"Only DnDG {GAME_VERSION} is supported; refusing to build for another version")
    sensitive = ("application/config/use_custom_user_dir", "application/config/custom_user_dir_name",
                 "application/config/disable_project_settings_override")
    if any(key.startswith(tuple(prefix + "." for prefix in sensitive)) for key in settings):
        raise ValueError("Feature-specific isolation settings are unsupported")
    if "gloss_trainer/version" in settings or "autoload/GlossTrainer" in settings:
        raise ValueError("Original already defines GlossTrainer")
    scripts = {}
    script_hashes = {}
    for path in sorted(source.rglob("*.gd")):
        if not inside(path.resolve(), source):
            raise ValueError("Source symlink escapes source directory")
        name = "gloss_trainer/" + path.relative_to(source).as_posix()
        if name.startswith("gloss_trainer/__") or name in pack.entries:
            raise ValueError("Reserved or conflicting resource: " + name)
        data = path.read_bytes()
        data.decode("utf-8-sig")
        scripts[name] = data
        script_hashes[name] = hashlib.sha256(data).hexdigest()
    if "gloss_trainer/trainer_overlay.gd" not in scripts:
        raise ValueError("src-dir must contain trainer_overlay.gd")
    identity = hashlib.sha256((str(output) + json.dumps(script_hashes, sort_keys=True)).encode()).hexdigest()[:16]
    user_dir = "GlossTrainer/DnDG/" + identity
    settings["gloss_trainer/version"] = string_variant(TRAINER_VERSION)
    settings["autoload/GlossTrainer"] = string_variant("*res://gloss_trainer/trainer_overlay.gd")
    # Prevent external override.cfg from undoing the isolation or autoload.
    settings["application/config/disable_project_settings_override"] = bool_variant(True)
    if not args.use_game_saves:
        settings["application/config/use_custom_user_dir"] = bool_variant(True)
        settings["application/config/custom_user_dir_name"] = string_variant(user_dir)
    smoke_settings = {
        "application/config/name": string_variant("GlossTrainer Smoke"),
        "application/run/main_scene": string_variant("res://gloss_trainer/__smoke.tscn"),
        "application/config/use_custom_user_dir": bool_variant(True),
        "application/config/custom_user_dir_name": string_variant(user_dir + "/smoke"),
        "application/config/disable_project_settings_override": bool_variant(True),
        "gloss_trainer/version": settings["gloss_trainer/version"],
        "autoload/GlossTrainer": settings["autoload/GlossTrainer"],
        "rendering/renderer/rendering_method": string_variant("gl_compatibility"),
    }
    output.mkdir(parents=True, exist_ok=True)
    bundle = output / "bundle"
    if bundle.exists():
        raise ValueError("Bundle exists; choose a new output directory (no overwrites)")
    with tempfile.TemporaryDirectory(prefix=".pack-", dir=output) as staging:
        stage = Path(staging)
        for mode in ("normal", "smoke"):
            directory = stage / mode
            directory.mkdir()
            shutil.copyfile(executable, directory / "DnDG_64.exe")
            shutil.copyfile(steam, directory / "steam_api64.dll")
        normal = stage / "normal/DnDG_64.pck"
        smoke = stage / "smoke/DnDG_64.pck"
        write_pack(normal, {**scripts, "project.binary": settings_encode(settings)}, pack)
        write_pack(smoke, {**scripts, "project.binary": settings_encode(smoke_settings),
                          "gloss_trainer/__smoke.gd": SMOKE_SCRIPT.encode(),
                          "gloss_trainer/__smoke.tscn": SMOKE_SCENE.encode(),
                          ".godot/global_script_class_cache.cfg": b"list=[]\n"})
        print("Verifying generated packs...", flush=True)
        Pack(normal).verify()
        Pack(smoke).verify()
        if sha256(original_path) != original_hash:
            raise ValueError("Original PCK changed during build")
        manifest = {
            "schema": 2, "layout": "godot-sidecar", "local_only": True,
            "warning": "Contains original game assets. Do not redistribute these PCKs.",
            "game_dir": str(game), "executable": str(executable),
            "executable_sha256": sha256(executable),
            "original_pck": str(original_path), "original_pck_sha256": original_hash,
            "steam_dll_sha256": sha256(steam),
            "path_overrides_disabled": path_overrides_disabled(executable),
            "game_version": GAME_VERSION, "trainer_version": TRAINER_VERSION, "original_entries": len(pack.entries), "scripts": script_hashes,
            "autoload": "*res://gloss_trainer/trainer_overlay.gd",
            "isolated": not args.use_game_saves, "user_dir_name": user_dir,
            "packs": {"normal": {"file": "normal/DnDG_64.pck", "sha256": sha256(normal)},
                      "smoke": {"file": "smoke/DnDG_64.pck", "sha256": sha256(smoke)}},
        }
        for mode in ("normal", "smoke"):
            if (sha256(stage / mode / "DnDG_64.exe") != manifest["executable_sha256"]
                    or sha256(stage / mode / "steam_api64.dll") != manifest["steam_dll_sha256"]):
                raise ValueError("Copied executable or Steam DLL hash mismatch")
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        for name in ("launch.py", "pck.py"):
            shutil.copyfile(Path(__file__).with_name(name), stage / name)
        (stage / "LOCAL-ONLY.txt").write_text(manifest["warning"] + "\n", encoding="utf-8")
        publish_bundle(stage, bundle)
    print(json.dumps({"bundle": str(bundle), "manifest": str(bundle / "manifest.json"),
                      "isolated": manifest["isolated"], "user_dir_name": user_dir,
                      "path_overrides_disabled": manifest["path_overrides_disabled"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--src-dir", type=Path, default=ROOT / "DnDG/src")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--use-game-saves", action="store_true", help="Explicitly opt normal launches into original saves; smoke always isolated")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, "Build refused: " + str(error) + "\n")


if __name__ == "__main__":
    main()
