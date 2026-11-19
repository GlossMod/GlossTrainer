"""Prove 4.6.3 GDExtension entry-point reuse of an ASI-loaded module.

All executable inputs are copied into a fresh repository output directory.
The generated PCK contains only a minimal project and extension-list fixture;
it never starts the DnDG game scene, scripts, or save data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "output/dndg-native-bootstrap-probe-20261118-official-sdk"
SMOKE = ROOT / "output/dndg-next-card-yahei-20261118/final/bundle/smoke"
LOADER_ZIP = ROOT / "output/about-page-20261027/inputs/Ultimate-ASI-Loader_x64.zip"
EXPECTED_LOADER_SHA256 = "3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7"
EXPECTED_GODOT_VERSION = "4.6.3.stable"
GODOT_EDITOR_URL = "https://github.com/godotengine/godot/releases/download/4.6.3-stable/Godot_v4.6.3-stable_win64.exe.zip"
GODOT_EDITOR_ZIP_SHA256 = "e39986a178d585ce7ac198fb8de6ea436366dc0cc00e594810c2e3e104c04b90"
TIMEOUT_SECONDS = 40


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], *, cwd: Path, timeout: int = 30, env=None) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=cwd, env=env, text=True, encoding="mbcs", errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=timeout, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def find_vcvars() -> Path:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    require(vswhere.is_file(), "Visual Studio Installer vswhere.exe is unavailable")
    result = run([str(vswhere), "-latest", "-products", "*", "-requires",
                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                 cwd=OUT)
    installation = Path(result.stdout.strip())
    vcvars = installation / "VC/Auxiliary/Build/vcvars64.bat"
    require(result.returncode == 0 and vcvars.is_file(), "VS 2022 x64 vcvars64.bat was not found")
    return vcvars


def c_source() -> str:
    return r'''#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "gdextension_interface.h"
#define GDE_EXPORT __declspec(dllexport)

static HMODULE g_self_module;
static volatile LONG g_asi_initialize_called;

static void append_event(const char *event, HMODULE self, HMODULE named, LONG asi_seen) {
    wchar_t path[MAX_PATH];
    DWORD count = GetEnvironmentVariableW(L"GLOSS_NATIVE_PROBE_LOG", path, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[256];
    int length = sprintf_s(line, sizeof(line),
        "{\"event\":\"%s\",\"self\":\"%p\",\"named_module\":\"%p\",\"asi_initialize_seen\":%ld}\r\n",
        event, (void *)self, (void *)named, asi_seen);
    if (length > 0) {
        DWORD written = 0;
        WriteFile(file, line, (DWORD)length, &written, NULL);
    }
    CloseHandle(file);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) g_self_module = instance;
    return TRUE;
}

__declspec(dllexport) void InitializeASI(void) {
    InterlockedExchange(&g_asi_initialize_called, 1);
    append_event("InitializeASI", g_self_module, GetModuleHandleW(L"GlossTrainer.asi"), 1);
}

static void probe_initialize(void *userdata, GDExtensionInitializationLevel level) {
    (void)userdata;
    (void)level;
}

static void probe_deinitialize(void *userdata, GDExtensionInitializationLevel level) {
    (void)userdata;
    (void)level;
}

GDExtensionBool GDE_EXPORT gloss_probe_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization *initialization) {
    (void)get_proc_address;
    (void)library;
    if (initialization == NULL) return 0;
    HMODULE from_function = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)&gloss_probe_library_init, &from_function);
    HMODULE named = GetModuleHandleW(L"GlossTrainer.asi");
    LONG asi_seen = InterlockedCompareExchange(&g_asi_initialize_called, 0, 0);
    append_event("GDExtensionEntry", from_function, named, asi_seen);
    initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_CORE;
    initialization->userdata = NULL;
    initialization->initialize = probe_initialize;
    initialization->deinitialize = probe_deinitialize;
    return from_function != NULL && from_function == g_self_module && from_function == named;
}
'''


def main(resume: bool = False) -> int:
    require(sys.platform == "win32", "This fixture requires Windows x64")
    if OUT.exists() and any(OUT.iterdir()):
        previous = OUT / "results.json"
        valid_previous = False
        if resume and previous.is_file():
            saved = json.loads(previous.read_text(encoding="utf-8"))
            valid_previous = saved.get("schema") == 1 and saved.get("status") == "pass"
        valid_incomplete = (OUT / "failure.txt").is_file() and not previous.exists()
        require(resume and (valid_previous or valid_incomplete),
                f"Refusing existing output; use --resume only for this probe's prior run: {OUT}")
        if previous.exists():
            previous.unlink()
        failure = OUT / "failure.txt"
        if failure.exists():
            failure.unlink()
    OUT.mkdir(parents=True, exist_ok=True)
    source_exe = SMOKE / "DnDG_64.exe"
    source_steam = SMOKE / "steam_api64.dll"
    source_pck = SMOKE / "DnDG_64.pck"
    for path in (source_exe, source_steam, source_pck, LOADER_ZIP):
        require(path.is_file(), f"Missing expected input: {path}")
    loader_hash = sha256(LOADER_ZIP)
    require(loader_hash == EXPECTED_LOADER_SHA256,
            f"ASI Loader provenance hash mismatch: {loader_hash}")

    fixture = OUT / "isolated"
    fixture.mkdir(exist_ok=True)
    for name, source in (("DnDG_64.exe", source_exe), ("steam_api64.dll", source_steam)):
        shutil.copyfile(source, fixture / name)
    shutil.copyfile(source_exe, OUT / "DnDG_64.exe")
    shutil.copyfile(source_pck, OUT / "source-smoke.pck")
    shutil.copyfile(source_steam, OUT / "steam_api64.dll")
    copied_exe = OUT / "DnDG_64.exe"

    version = run([str(copied_exe), "--version"], cwd=OUT, timeout=15)
    (OUT / "version-check.log").write_text(version.stdout, encoding="utf-8")
    require(version.returncode == 0 and version.stdout.strip().startswith(EXPECTED_GODOT_VERSION),
            "Copied executable did not identify as Godot 4.6.3; see version-check.log")

    editor_zip = OUT / "Godot_v4.6.3-stable_win64.exe.zip"
    if not editor_zip.exists():
        request = urllib.request.Request(GODOT_EDITOR_URL, headers={"User-Agent": "GlossTrainer-native-probe"})
        with urllib.request.urlopen(request, timeout=90) as response, editor_zip.open("wb") as target:
            shutil.copyfileobj(response, target)
    require(sha256(editor_zip) == GODOT_EDITOR_ZIP_SHA256,
            "Official Godot 4.6.3 editor archive SHA-256 mismatch")
    with zipfile.ZipFile(editor_zip) as archive:
        editor_candidates = [name for name in archive.namelist()
                             if Path(name).name == "Godot_v4.6.3-stable_win64.exe"]
        require(len(editor_candidates) == 1, "Official editor ZIP did not contain its expected executable")
        editor_exe = OUT / Path(editor_candidates[0]).name
        with archive.open(editor_candidates[0]) as source, editor_exe.open("wb") as target:
            shutil.copyfileobj(source, target)
    editor_version = run([str(editor_exe), "--version"], cwd=OUT, timeout=30)
    (OUT / "editor-version.log").write_text(editor_version.stdout, encoding="utf-8")
    require(editor_version.returncode == 0 and editor_version.stdout.strip().startswith(EXPECTED_GODOT_VERSION),
            "Official API-dump tool was not Godot 4.6.3")

    api_header = OUT / "gdextension_interface.h"
    api_json = OUT / "extension_api.json"
    header_dump = run([str(editor_exe), "--headless", "--dump-gdextension-interface"], cwd=OUT, timeout=60)
    (OUT / "dump-interface.log").write_text(header_dump.stdout, encoding="utf-8")
    api_dump = run([str(editor_exe), "--headless", "--dump-extension-api"], cwd=OUT, timeout=60)
    (OUT / "dump-api.log").write_text(api_dump.stdout, encoding="utf-8")
    require(header_dump.returncode == 0 and api_header.is_file(),
            "Godot 4.6.3 did not emit gdextension_interface.h")
    require(api_dump.returncode == 0 and api_json.is_file(),
            "Godot 4.6.3 did not emit extension_api.json")

    source = OUT / "probe.c"
    source.write_text(c_source(), encoding="utf-8")
    scripts = fixture / "scripts"
    scripts.mkdir(exist_ok=True)
    dll = scripts / "GlossTrainer.asi"
    vcvars = find_vcvars()
    build_batch = OUT / "build-fixture.cmd"
    build_batch.write_text(
        f'@call "{vcvars}" >nul\r\n'
        '@if errorlevel 1 exit /b %errorlevel%\r\n'
        f'@cl /nologo /LD /O2 /MT /I"{OUT}" /Tc"{source}" '
        f'/link /MACHINE:X64 /OUT:"{dll}"\r\n', encoding="ascii")
    compiled = run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(build_batch)], cwd=OUT, timeout=90)
    (OUT / "compile.log").write_text(compiled.stdout, encoding="utf-8")
    require(compiled.returncode == 0 and dll.is_file(), "MSVC fixture build failed; see compile.log")

    with zipfile.ZipFile(LOADER_ZIP) as archive:
        candidates = [name for name in archive.namelist() if Path(name).name.casefold() == "dinput8.dll"]
        require(len(candidates) == 1, "Verified ASI Loader ZIP did not contain exactly one dinput8.dll")
        loader_data = archive.read(candidates[0])
    (fixture / "dinput8.dll").write_bytes(loader_data)
    (fixture / "global.ini").write_text(
        "[GlobalSets]\nLoadPlugins=1\nLoadFromScriptsOnly=1\n"
        "LoadRecursively=0\nDontLoadFromDllMain=1\nDebugLog=1\n", encoding="ascii")

    descriptor_path = dll.resolve().as_posix()
    descriptor = (
        "[configuration]\nentry_symbol = \"gloss_probe_library_init\"\n"
        "compatibility_minimum = \"4.6\"\nreloadable = false\n\n"
        "[libraries]\nwindows.release.x86_64 = \"" + descriptor_path + "\"\n"
    )
    scene = ('[gd_scene load_steps=2 format=3]\n'
             '[ext_resource type="Script" path="res://probe.gd" id="1"]\n'
             '[node name="NativeBootstrapProbe" type="Node"]\n'
             'script = ExtResource("1")\n')
    script = ('extends Node\n'
              'func _ready() -> void:\n'
              '    print("NATIVE_PROBE_SCENE_READY")\n'
              '    get_tree().quit(0)\n')
    from pck import Pack, bool_variant, settings_encode, string_variant, write_pack

    settings = {
        "application/config/name": string_variant("GlossTrainer Native Bootstrap Probe"),
        "application/run/main_scene": string_variant("res://probe.tscn"),
        "application/config/disable_project_settings_override": bool_variant(True),
        "application/config/use_custom_user_dir": bool_variant(True),
        "application/config/custom_user_dir_name": string_variant("GlossTrainer/DnDG/native-bootstrap-probe"),
        "rendering/renderer/rendering_method": string_variant("gl_compatibility"),
    }
    pack_path = fixture / "DnDG_64.pck"
    if pack_path.exists():
        pack_path.unlink()
    write_pack(pack_path, {
        "project.binary": settings_encode(settings),
        ".godot/extension_list.cfg": b"res://probe.gdextension\n",
        ".godot/global_script_class_cache.cfg": b"list=[]\n",
        "probe.gdextension": descriptor.encode("utf-8"),
        "probe.tscn": scene.encode("utf-8"),
        "probe.gd": script.encode("utf-8"),
    })
    Pack(pack_path).verify()

    events = OUT / "module-events.jsonl"
    env = os.environ.copy()
    env["GLOSS_NATIVE_PROBE_LOG"] = str(events)
    isolated_appdata = fixture / "appdata"
    isolated_localappdata = fixture / "localappdata"
    isolated_appdata.mkdir(exist_ok=True)
    isolated_localappdata.mkdir(exist_ok=True)
    env["APPDATA"] = str(isolated_appdata)
    env["LOCALAPPDATA"] = str(isolated_localappdata)
    if events.exists():
        events.unlink()
    loader_log = fixture / "dinput8.log"
    if loader_log.exists():
        loader_log.unlink()
    before = run(["tasklist.exe", "/FI", "IMAGENAME eq DnDG_64.exe", "/FO", "CSV", "/NH"], cwd=OUT)
    require(before.returncode == 0 and "DnDG_64.exe" not in before.stdout,
            "Refusing to run while a DnDG_64.exe process exists")
    started = time.time()
    execution = run([str(fixture / "DnDG_64.exe"), "--headless", "--verbose"],
                    cwd=fixture, timeout=TIMEOUT_SECONDS, env=env)
    (OUT / "fixture-run.log").write_text(execution.stdout, encoding="utf-8")
    require(execution.returncode == 0, f"Fixture process failed with {execution.returncode}; see fixture-run.log")
    require("NATIVE_PROBE_SCENE_READY" in execution.stdout,
            "Minimal fixture scene did not report ready; see fixture-run.log")
    require(events.is_file(), "Neither ASI InitializeASI nor GDExtension entry wrote a marker")
    lines = [json.loads(line) for line in events.read_text(encoding="ascii").splitlines() if line.strip()]
    asi_events = [item for item in lines if item.get("event") == "InitializeASI"]
    extension_events = [item for item in lines if item.get("event") == "GDExtensionEntry"]
    require(asi_events, "Ultimate ASI Loader did not call InitializeASI; inspect loader log")
    require(extension_events, "Godot did not invoke the configured GDExtension entry point")
    bases_asi = {item["self"] for item in asi_events}
    bases_extension = {item["self"] for item in extension_events}
    require(len(bases_asi) == len(bases_extension) == 1 and bases_asi == bases_extension,
            "ASI and GDExtension callbacks ran in different module instances")
    require(all(item["named_module"] == item["self"] for item in lines),
            "Module filename lookup disagreed with the callback's own image base")

    result = {
        "schema": 1,
        "status": "pass",
        "scope": "isolated minimal PCK; no original main scene, scripts, saves, or live gameplay",
        "godot_version": EXPECTED_GODOT_VERSION,
        "source_executable": str(source_exe),
        "source_executable_sha256": sha256(source_exe),
        "official_api_dump_editor": str(editor_exe),
        "official_editor_zip_sha256": sha256(editor_zip),
        "source_smoke_pck_sha256": sha256(source_pck),
        "source_steam_api_sha256": sha256(source_steam),
        "asi_loader_zip": str(LOADER_ZIP),
        "asi_loader_zip_sha256": loader_hash,
        "asi_loader_member": candidates[0],
        "godot_interface_header_sha256": sha256(api_header),
        "godot_extension_api_sha256": sha256(api_json),
        "fixture_executable_sha256": sha256(fixture / "DnDG_64.exe"),
        "fixture_pck_sha256": sha256(pack_path),
        "fixture_module_sha256": sha256(dll),
        "module_events": lines,
        "run_exit_code": execution.returncode,
        "elapsed_seconds": round(time.time() - started, 3),
        "evidence": {
            "compiler": "Visual Studio 2022 x64 cl.exe",
            "entry_symbol": "gloss_probe_library_init",
            "asi_callback": "InitializeASI",
            "same_module_image_base": True,
            "extension_manifest_res_path": "res://probe.gdextension",
            "extension_list_pck_path": ".godot/extension_list.cfg",
        },
    }
    (OUT / "results.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--resume", action="store_true",
                     help="Resume only this script's incomplete output directory")
    args = cli.parse_args()
    try:
        raise SystemExit(main(resume=args.resume))
    except subprocess.TimeoutExpired as error:
        (OUT / "failure.txt").write_text(f"Timed out: {error}\n", encoding="utf-8")
        raise
    except Exception as error:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "failure.txt").write_text(f"{type(error).__name__}: {error}\n", encoding="utf-8")
        raise
