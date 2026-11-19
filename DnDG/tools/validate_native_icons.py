"""Run the DnDG icon GDExtension fixture without starting or driving the game."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from build import DEFAULT_GAME, GAME_NAME, GAME_VERSION, inside
from pck import Pack, settings_decode, sha256, string_variant

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "DnDG/validation/native_icons"


def run(args: argparse.Namespace) -> None:
    engine = args.engine.resolve(strict=True)
    game = args.game_dir.resolve(strict=True)
    output = args.output.resolve()
    if inside(output, game) or inside(game, output) or inside(engine, game):
        raise ValueError("Use an isolated output directory and a separate Godot 4.6.x executable")
    original = game / "DnDG_64.pck"
    original_hash = sha256(original)
    settings = settings_decode(Pack(original).read("project.binary"))
    if (settings.get("application/config/name") != string_variant(GAME_NAME) or
            settings.get("application/config/version") != string_variant(GAME_VERSION)):
        raise ValueError(f"Expected the original DnDG {GAME_VERSION} resource pack")
    binary = (ROOT / f"build/DnDG/{args.mode}/dndg_icon_probe.dll").resolve(strict=True)
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / "fixture"
    fixture.mkdir()
    for filename in ("project.godot", "test_icons.gd", "icon_probe.gdextension"):
        shutil.copyfile(FIXTURE / filename, fixture / filename)
    shutil.copyfile(binary, fixture / binary.name)
    (fixture / ".godot").mkdir()
    (fixture / ".godot/extension_list.cfg").write_text("res://icon_probe.gdextension\n", encoding="ascii")
    isolated = output / "userdata"
    isolated.mkdir()
    command = [str(engine), "--headless", "--path", str(fixture), "--script", "res://test_icons.gd", "--", str(original)]
    proc = subprocess.run(command, cwd=fixture, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, env={**os.environ, "APPDATA": str(isolated)}, timeout=30)
    log = proc.stdout.decode("utf-8", errors="replace").replace("\r", "")
    (output / "result.log").write_text(log, encoding="utf-8")
    after = sha256(original)
    (output / "receipt.json").write_text(json.dumps({"mode": args.mode, "command": command,
        "exit_code": proc.returncode, "original_pck_sha256_before": original_hash,
        "original_pck_sha256_after": after, "probe_sha256": sha256(fixture / binary.name)}, indent=2) + "\n",
        encoding="utf-8")
    print(log, end="")
    if proc.returncode != 0 or "ERROR:" in log or "DnDG native icon fixture: 13/13 passed" not in log or after != original_hash:
        raise ValueError("Native icon fixture failed; see result.log and receipt.json")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--mode", choices=("debug", "release"), default="release")
    parser.add_argument("--output", type=Path, required=True)
    try:
        run(parser.parse_args())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Native icon fixture refused: {error}\n")


if __name__ == "__main__":
    main()
