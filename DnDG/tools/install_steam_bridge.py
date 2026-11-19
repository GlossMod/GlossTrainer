"""Install the DnDG ASI, loader and Godot bridge into a supported Steam installation."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

from launch import require_game_stopped
from native_bundle import (BRIDGE_PATH, BRIDGE_SCRIPT, DEFAULT_GAME,
                           EXTENSION_LIST, EXTENSION_PATH, LOADER_INI,
                           ORIGINAL_EXE, descriptor, sha256)

ORIGINAL_PACK = "c315e288845d9b682a4f9ebc7c71549ade659cdf6672fffb39f01844bdff0ef8"
LOADER_DLL = "900fa114af8317bfe22015ca77becc1afa9853831213ff17a5e03e0f45d58937"
USER_DIR = "GlossTrainer/DnDG/native/steam"


def bridge_files(asi):
    return {
        BRIDGE_PATH: BRIDGE_SCRIPT,
        EXTENSION_PATH: descriptor(asi),
        EXTENSION_LIST: f"res://{EXTENSION_PATH}\n".encode(),
        # Publish last so autoload can only be enabled after its resources exist.
        "override.cfg": (
            "[application]\nconfig/use_custom_user_dir=true\n"
            f'config/custom_user_dir_name="{USER_DIR}"\n\n'
            f'[autoload]\nGlossTrainerNative="*res://{BRIDGE_PATH}"\n\n'
            '[rendering]\nrenderer/rendering_method="forward_plus"\n'
            'rendering_device/driver.windows="d3d12"\n'
        ).encode(),
    }


def local_path(game, name):
    path = game / name
    if path.resolve() != path:
        raise ValueError(f"Unexpected redirected path: {path}")
    return path


def install(game, dry_run, package=None):
    game = game.resolve(strict=True)
    for name, expected in (("DnDG_64.exe", ORIGINAL_EXE), ("DnDG_64.pck", ORIGINAL_PACK)):
        if sha256(local_path(game, name)) != expected:
            raise ValueError(f"Unexpected file version: {name}")

    source = package.resolve(strict=True) if package is not None else game
    loader = local_path(source, "dinput8.dll").read_bytes()
    if hashlib.sha256(loader).hexdigest() != LOADER_DLL:
        raise ValueError("Unexpected file version: dinput8.dll")
    loader_ini = local_path(source, "dinput8.ini").read_bytes()
    if loader_ini != LOADER_INI:
        raise ValueError("Expected the DnDG Loader configuration with scripts-only loading")
    asi_name = "scripts/GlossTrainer.asi"
    module = local_path(source, asi_name).read_bytes()
    if not module.startswith(b"MZ") or any(symbol not in module for symbol in
            (b"InitializeASI\0", b"GlossTrainerGodotInit\0")):
        raise ValueError("Expected a native GlossTrainer.asi with both entry points")

    desired = {"dinput8.dll": loader, "dinput8.ini": loader_ini, asi_name: module}
    desired.update(bridge_files(local_path(game, asi_name)))
    pending = {}
    for name, data in desired.items():
        path = local_path(game, name)
        if path.exists():
            if not path.is_file() or path.read_bytes() != data:
                raise ValueError(f"Conflicting file, left intact: {path}")
        else:
            pending[path] = data
    if not dry_run and pending:
        require_game_stopped()
        created = []
        try:
            for path, data in pending.items():
                path.parent.mkdir(exist_ok=True)
                with path.open("xb") as stream:
                    created.append(path)
                    stream.write(data)
        except OSError:
            for path in reversed(created):
                path.unlink()
            raise
    return {"game_dir": str(game), "dry_run": dry_run, "files_added": [str(p.relative_to(game)) for p in pending],
            "user_dir": USER_DIR}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--package-dir", type=Path,
                        help="Extracted ZIP directory containing the ASI and loader")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.game_dir, args.dry_run, args.package_dir), indent=2))
    except (OSError, ValueError) as error:
        print(f"DnDG bridge installation refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
