"""Capture the production DnDG ImGui overlay through offscreen DX12 WARP."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ("player.png", "cards.png", "card-menu.png", "preview.png")


def receipt(path: Path, width: int, height: int) -> dict:
    pixels = path.read_bytes()
    if (len(pixels) < 24 or pixels[:8] != b"\x89PNG\r\n\x1a\n" or pixels[12:16] != b"IHDR" or
            struct.unpack(">2I", pixels[16:24]) != (width, height)):
        raise ValueError(f"Unexpected screenshot dimensions or format: {path}")
    return {"name": path.name, "sha256": hashlib.sha256(pixels).hexdigest(), "bytes": len(pixels)}


def run(output: Path, modes: list[str]) -> None:
    output = output.resolve()
    tests = {mode: (ROOT / f"build/DnDG/{mode}/GlossTrainerDnDGDx12CaptureTests.exe").resolve(strict=True)
             for mode in modes}
    output.mkdir(parents=True, exist_ok=False)
    for theme in ("light", "dark"):
        (output / f"{theme}.ini").write_text(f"[UI]\nDark={int(theme == 'dark')}\nShowNext=1\nLanguage=0\n",
                                             encoding="ascii")
    cases = []
    for mode, test in tests.items():
        for theme in ("light", "dark"):
            ini = output / f"{theme}.ini"
            for width, height in ((1280, 720), (2560, 1440)):
                tag = f"{mode}-{theme}-{height}"
                directory = output / tag
                command = [str(test), str(ini), str(directory), str(height), theme]
                original_ini = ini.read_bytes()
                with (output / f"{tag}.log").open("wb") as log_file:
                    proc = subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log_file,
                                          stderr=subprocess.STDOUT, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
                log = (output / f"{tag}.log").read_text(encoding="utf-8", errors="replace").strip()
                print(log, flush=True)
                if (proc.returncode or f"D3D12 WARP readback {width}x{height} {theme}" not in log or
                        "card_menu_token=1" not in log or ini.read_bytes() != original_ini):
                    raise ValueError(f"Capture failed or changed the fixture INI: {tag}")
                cases.append({"case": tag, "command": command, "exit_code": proc.returncode, "stdout": log,
                              "images": [receipt(directory / name, width, height) for name in IMAGES]})
    (output / "results.json").write_text(json.dumps({"engine": "D3D12 WARP, windowless", "cases": cases}, indent=2) + "\n",
                                         encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New root output subdirectory")
    parser.add_argument("--mode", choices=("debug", "release"), action="append")
    args = parser.parse_args()
    try:
        run(args.output, args.mode or ["debug", "release"])
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"D3D12 capture refused: {error}\n")


if __name__ == "__main__":
    main()
