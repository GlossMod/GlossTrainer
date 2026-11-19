"""Validate and launch an unmodified executable beside its same-named PCK.

No shell, injection, original-file writes, implicit save copying or arbitrary
Godot arguments. Smoke never starts the original main scene or autoloads.
"""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from pck import Pack, bool_variant, settings_decode, sha256, string_variant


def running_game_pids():
    """Fail closed if Windows process enumeration fails; never terminate peers."""
    if os.name != "nt":
        raise OSError("Normal launch requires Windows process enumeration")

    class ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32FirstW.restype = wintypes.BOOL
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        found = []
        ok = kernel.Process32FirstW(handle, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.casefold() == "dndg_64.exe":
                found.append(entry.th32ProcessID)
            ok = kernel.Process32NextW(handle, ctypes.byref(entry))
        error = ctypes.get_last_error()
        if error != 18:  # ERROR_NO_MORE_FILES is the only successful terminator.
            raise ctypes.WinError(error)
        return found
    finally:
        kernel.CloseHandle(handle)


def require_game_stopped():
    pids = running_game_pids()
    if pids:
        raise ValueError("Normal launch requires all DnDG_64.exe processes to be closed; running PIDs: " + ", ".join(map(str, pids)))


def prepare(bundle, smoke, allow_game_saves):
    manifest = json.loads((bundle / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 2 or manifest.get("layout") != "godot-sidecar" or manifest.get("local_only") is not True:
        raise ValueError("Unknown bundle manifest")
    if not smoke and not manifest["isolated"] and not allow_game_saves:
        raise ValueError("Bundle uses original saves; pass --allow-game-saves explicitly")
    game = Path(manifest["game_dir"]).resolve(strict=True)
    exe = Path(manifest["executable"]).resolve(strict=True)
    if exe != game / "DnDG_64.exe" or bundle == game or game in bundle.parents:
        raise ValueError("Invalid executable or bundle location")
    original = Path(manifest["original_pck"]).resolve(strict=True)
    if original != game / "DnDG_64.pck":
        raise ValueError("Invalid original PCK location")
    for path, expected in ((exe, manifest["executable_sha256"]),
                           (original, manifest["original_pck_sha256"]),
                           (game / "steam_api64.dll", manifest["steam_dll_sha256"])):
        if sha256(path) != expected:
            raise ValueError("Original game changed; rebuild bundle: " + str(path))
    mode = "smoke" if smoke else "normal"
    selected = manifest["packs"][mode]
    pack_path = (bundle / selected["file"]).resolve(strict=True)
    directory = (bundle / mode).resolve(strict=True)
    if (directory != bundle / mode or selected["file"] != mode + "/DnDG_64.pck"
            or pack_path != directory / "DnDG_64.pck"):
        raise ValueError("Invalid bundle pack path")
    for name, expected in (("DnDG_64.exe", manifest["executable_sha256"]),
                           ("steam_api64.dll", manifest["steam_dll_sha256"])):
        copied = (directory / name).resolve(strict=True)
        if copied != directory / name or sha256(copied) != expected:
            raise ValueError("Sidecar executable/DLL hash or location mismatch")
    if {p.name for p in directory.iterdir()} != {"DnDG_64.exe", "DnDG_64.pck", "steam_api64.dll"}:
        raise ValueError("Unexpected files in sidecar directory; rebuild in a clean output")
    if sha256(pack_path) != selected["sha256"]:
        raise ValueError("Bundle PCK hash mismatch")
    pack = Pack(pack_path)
    for name, expected in manifest["scripts"].items():
        if not name.startswith("gloss_trainer/") or hashlib.sha256(pack.read(name)).hexdigest() != expected:
            raise ValueError("Trainer source hash mismatch: " + name)
    settings = settings_decode(pack.read("project.binary"))
    if settings.get("autoload/GlossTrainer") != string_variant("*res://gloss_trainer/trainer_overlay.gd"):
        raise ValueError("Trainer autoload missing")
    if settings.get("application/config/disable_project_settings_override") != bool_variant(True):
        raise ValueError("External settings override must be disabled")
    if smoke or manifest["isolated"]:
        name = manifest["user_dir_name"] + ("/smoke" if smoke else "")
        if not name.startswith("GlossTrainer/DnDG/") or any(p in ("", ".", "..") for p in name.split("/")) or "\\" in name or ":" in name:
            raise ValueError("Invalid isolated user directory")
        if (settings.get("application/config/use_custom_user_dir") != bool_variant(True)
                or settings.get("application/config/custom_user_dir_name") != string_variant(name)):
            raise ValueError("Pack user directory isolation mismatch")
    if smoke:
        autoloads = [key for key in settings if key.startswith("autoload/")]
        if autoloads != ["autoload/GlossTrainer"]:
            raise ValueError("Smoke pack contains original game autoloads")
        if settings.get("application/run/main_scene") != string_variant("res://gloss_trainer/__smoke.tscn"):
            raise ValueError("Smoke main scene mismatch")
    return manifest, directory, directory / "DnDG_64.exe", pack_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--smoke", action="store_true", help="Run isolated empty scene, without original autoloads")
    parser.add_argument("--dry-run", action="store_true", help="Validate all hashes and print command; do not launch")
    parser.add_argument("--headless", action="store_true", help="Smoke only")
    parser.add_argument("--timeout", type=float, default=30.0, help="Smoke deadline in seconds (1..120)")
    parser.add_argument("--allow-game-saves", action="store_true")
    args = parser.parse_args()
    try:
        if args.headless and not args.smoke:
            raise ValueError("--headless requires --smoke")
        if not 1 <= args.timeout <= 120:
            raise ValueError("Smoke timeout must be between 1 and 120 seconds")
        bundle = args.bundle.resolve(strict=True)
        manifest, game, exe, pack = prepare(bundle, args.smoke, args.allow_game_saves)
        logs = bundle / "logs"
        stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + str(time.time_ns())
        command = [str(exe)]
        if args.headless:
            command.append("--headless")
        print(json.dumps({"command": command, "cwd": str(game), "smoke": args.smoke,
                          "isolated": args.smoke or manifest["isolated"],
                          "layout": "godot-sidecar",
                          "user_dir_name": manifest["user_dir_name"] + ("/smoke" if args.smoke else "")}, indent=2), flush=True)
        if args.dry_run:
            return 0
        if not args.smoke:
            require_game_stopped()
        logs.mkdir(exist_ok=True)
        capture = logs / (stamp + ".stdout.log")
        receipt = {"command": command, "cwd": str(game), "smoke": args.smoke,
                   "stdout": str(capture), "layout": "godot-sidecar",
                   "isolated": args.smoke or manifest["isolated"]}
        with capture.open("wb") as stream:
            if not args.smoke:
                require_game_stopped()
            process = subprocess.Popen(command, cwd=game, stdin=subprocess.DEVNULL,
                                       stdout=stream, stderr=subprocess.STDOUT,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            receipt["pid"] = process.pid
            try:
                receipt["exit_code"] = process.wait(timeout=args.timeout if args.smoke else None)
            except subprocess.TimeoutExpired:
                process.kill()  # Only the process created by this invocation.
                process.wait()
                receipt["timed_out"] = True
                receipt["exit_code"] = 124
            except KeyboardInterrupt:
                process.terminate()
                process.wait()
                receipt["exit_code"] = 130
        if args.smoke:
            content = capture.read_text(encoding="utf-8", errors="replace")
            receipt["autoload_ok"] = "GLOSS_SMOKE_AUTOLOAD_OK" in content
            receipt["complete"] = "GLOSS_SMOKE_COMPLETE" in content
            directories = [line.split("=", 1)[1].strip() for line in content.splitlines()
                           if line.startswith("GLOSS_SMOKE_USER_DIR=")]
            receipt["observed_user_directory"] = directories[-1] if directories else None
            suffix = "/" + manifest["user_dir_name"] + "/smoke"
            receipt["user_directory_ok"] = bool(directories) and directories[-1].replace("\\", "/").endswith(suffix)
            receipt["script_errors"] = any(marker in content for marker in ("SCRIPT ERROR", "Parse Error", "Failed to load script", "ERROR:"))
            if not receipt["autoload_ok"] or not receipt["complete"] or not receipt["user_directory_ok"] or receipt["script_errors"]:
                receipt["exit_code"] = receipt["exit_code"] or 2
        (logs / (stamp + ".receipt.json")).write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(json.dumps(receipt, indent=2))
        return receipt["exit_code"]
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, "Launch refused: " + str(error) + "\n")


if __name__ == "__main__":
    sys.exit(main())
