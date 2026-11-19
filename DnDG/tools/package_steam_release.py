"""Build the DnDG 1.0.0 Steam publication ZIP (Python 3.11+)."""

import argparse
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zipfile

from native_bundle import LOADER_INI, read_loader

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ("install_steam_bridge.py", "build.py", "launch.py", "native_bundle.py", "pck.py")
LICENSES = ("imgui.txt", "minhook.txt", "nlohmann_json.txt", "ultimate_asi_loader.txt")
EXPORTS = {b"InitializeASI", b"GlossTrainerGodotInit"}
ABSOLUTE_PATH = re.compile(
    r"(?<![a-z0-9_:/\\])(?:[a-z]:[\\/]+|\\\\+|/(?:Users|home|mnt|tmp|private|Volumes|opt|var)/)"
    r"[\w.() %~&-]+(?:[\\/]+[\w.() %~&-]+)+",
    re.IGNORECASE,
)


def assert_no_machine_paths(data, label):
    # MSVC can keep __FILE__ literals and CodeView paths in Release DLLs.
    for text in (data.decode("utf-8", errors="replace"), data.decode("utf-16le", errors="replace"),
                 data[1:].decode("utf-16le", errors="replace")):
        if ABSOLUTE_PATH.search(text):
            raise ValueError(f"{label} contains an absolute path; use publication-safe inputs")


def assert_asi(data):
    """Check that the two exported functions are defined by an x64 PE DLL."""
    try:
        pe, = struct.unpack_from("<I", data, 0x3C)
        machine, count, _, _, _, optional_size, flags = struct.unpack_from("<HHIIIHH", data, pe + 4)
        optional = pe + 24
        magic, = struct.unpack_from("<H", data, optional)
        if (data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or machine != 0x8664
                or not flags & 0x2000 or magic != 0x20B or optional_size < 120):
            raise ValueError("Expected a Windows x64 PE DLL for GlossTrainer.asi")
        directory_count, export_rva, export_size = struct.unpack_from("<III", data, optional + 108)
        if not directory_count or not export_rva or export_size < 40:
            raise ValueError("GlossTrainer.asi has no export directory")

        sections = []
        for index in range(count):
            section = optional + optional_size + index * 40
            _, rva, length, start = struct.unpack_from("<IIII", data, section + 8)
            characteristics, = struct.unpack_from("<I", data, section + 36)
            sections.append((rva, length, start, characteristics))

        def locate(rva, size, executable=False):
            for base, length, start, characteristics in sections:
                delta = rva - base
                end = start + delta + size
                if (0 <= delta <= length - size and end <= len(data)
                        and (not executable or characteristics & 0x20000000)):
                    return start + delta
            raise ValueError("GlossTrainer.asi has an invalid export table or target")

        directory = locate(export_rva, 40)
        functions, names, addresses, name_rvas, ordinals = struct.unpack_from("<5I", data, directory + 20)
        addresses = locate(addresses, functions * 4)
        name_rvas = locate(name_rvas, names * 4)
        ordinals = locate(ordinals, names * 2)
        found = set()
        name_limit = max(map(len, EXPORTS)) + 1
        for index in range(names):
            rva, = struct.unpack_from("<I", data, name_rvas + index * 4)
            start = locate(rva, 1)
            name, terminator, _ = data[start:start + name_limit].partition(b"\0")
            if not terminator or name not in EXPORTS:
                continue
            ordinal, = struct.unpack_from("<H", data, ordinals + index * 2)
            if ordinal >= functions:
                raise ValueError("GlossTrainer.asi has an invalid export ordinal")
            target, = struct.unpack_from("<I", data, addresses + ordinal * 4)
            if not target or export_rva <= target < export_rva + export_size:
                raise ValueError("GlossTrainer.asi must define both entry points directly")
            locate(target, 1, executable=True)
            found.add(name)
        if found != EXPORTS:
            raise ValueError("GlossTrainer.asi must export InitializeASI and GlossTrainerGodotInit")
    except struct.error as error:
        raise ValueError("GlossTrainer.asi has truncated PE headers") from error

    if re.search(rb"(?:msvcp\d+|vcruntime\d+(?:_\d+)?|ucrtbase)d\.dll\0", data, re.IGNORECASE):
        raise ValueError("GlossTrainer.asi uses a debug CRT; supply a Release build")
    assert_no_machine_paths(data, "GlossTrainer.asi")


def package(artifact, loader, output):
    if output.suffix.lower() != ".zip":
        raise ValueError("The output filename must end in .zip")
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"Output already exists: {output}")
    if artifact.parent.name.lower() == "debug":
        raise ValueError("Supply the Release GlossTrainer.asi build")
    asi = artifact.read_bytes()
    assert_asi(asi)
    dll = read_loader(loader)

    docs = ROOT / "DnDG" / "docs"
    readme = (docs / "release-readme.md").read_bytes()
    assert_no_machine_paths(readme, "README.md")
    files = {
        "README.md": readme,
        "LICENSE": (ROOT / "LICENSE").read_bytes(),
        "dinput8.dll": dll,
        "dinput8.ini": LOADER_INI,
        "scripts/GlossTrainer.asi": asi,
        "licenses/godot-cpp.txt": (docs / "godot-cpp-license.txt").read_bytes(),
    }
    for name in LICENSES:
        files[f"licenses/{name}"] = (ROOT / "licenses" / name).read_bytes()
    for name in TOOLS:
        source = (ROOT / "DnDG" / "tools" / name).read_bytes()
        assert_no_machine_paths(source, f"tools/{name}")
        files[f"tools/{name}"] = source

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".dndg-release-") as directory:
        temporary = Path(directory) / "package.zip"
        with zipfile.ZipFile(temporary, "x") as archive:
            for name, data in sorted(files.items()):
                entry = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
                archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED)
        # Publish the complete ZIP without replacing a target created by another run.
        if os.name == "nt":
            temporary.rename(output)
        else:
            os.link(temporary, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", "--asi", type=Path, required=True, help="Release GlossTrainer.asi")
    parser.add_argument("--loader", type=Path, required=True, help="official Ultimate ASI Loader v10.0.0 x64 ZIP")
    parser.add_argument("--output", type=Path, required=True, help="new ZIP file path")
    args = parser.parse_args()
    try:
        package(args.artifact, args.loader, args.output)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"DnDG publication package refused: {error}", file=sys.stderr)
        return 1
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
