"""Conservative, stdlib-only Godot 4.6.3 PCK v3 / ECFG reader and writer.

Layout references: godotengine/godot tag 4.6, core/io/{file_access_pack,
pck_packer,marshalls}.cpp and core/config/project_settings.cpp.
Unknown layouts, encryption, removals, deltas and sparse packs are rejected.
"""

import hashlib
import io
from pathlib import PurePosixPath
import struct
from dataclasses import dataclass


class FormatError(ValueError):
    pass


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise FormatError("Truncated input")
    return data


def u32(stream):
    return struct.unpack("<I", read_exact(stream, 4))[0]


def string_variant(value):
    raw = value.encode("utf-8")
    return struct.pack("<II", 4, len(raw)) + raw + bytes(-len(raw) % 4)


def bool_variant(value):
    return struct.pack("<II", 1, int(value))


def settings_decode(data):
    stream = io.BytesIO(data)
    if read_exact(stream, 4) != b"ECFG":
        raise FormatError("Expected binary project settings (ECFG)")
    count = u32(stream)
    if count > 100000:
        raise FormatError("Unreasonable settings count")
    settings = {}
    for _ in range(count):
        length = u32(stream)
        if not 0 < length <= 65536:
            raise FormatError("Invalid settings key length")
        key = read_exact(stream, length).decode("utf-8")
        length = u32(stream)
        if length < 4 or length > len(data):
            raise FormatError("Invalid Variant length")
        value = read_exact(stream, length)
        if key in settings or "\0" in key:
            raise FormatError("Duplicate or NUL settings key")
        settings[key] = value
    if stream.read(1):
        raise FormatError("Trailing binary settings data")
    return settings


def settings_encode(settings):
    result = bytearray(b"ECFG" + struct.pack("<I", len(settings)))
    for key, value in settings.items():
        raw = key.encode("utf-8")
        result.extend(struct.pack("<I", len(raw)) + raw)
        result.extend(struct.pack("<I", len(value)) + value)
    return bytes(result)


def canonical_path(raw):
    text = raw.decode("utf-8")
    text = text.removeprefix("res://")
    path = PurePosixPath(text)
    if (not text or "\\" in text or ":" in text or "\0" in text
            or path.is_absolute() or any(p in ("", ".", "..") for p in text.split("/"))):
        raise FormatError("Invalid resource path")
    return text


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    size: int
    md5: bytes


class Pack:
    def __init__(self, path):
        self.path = path
        self.size = path.stat().st_size
        with path.open("rb") as stream:
            header = read_exact(stream, 104)
            magic, version, major, minor, patch, flags, base, directory = struct.unpack("<6I2Q", header[:40])
            if (magic, version, major, minor, patch, flags) != (0x43504447, 3, 4, 6, 3, 2):
                raise FormatError("Only unencrypted standalone Godot 4.6.3 PCK v3 flags=2 is supported")
            if any(header[40:]) or base < 104 or base % 16 or not base <= directory <= self.size - 4:
                raise FormatError("Unsupported PCK header/bounds")
            self.base = base
            self.directory = directory
            stream.seek(directory)
            count = u32(stream)
            if not 1 <= count <= 1000000 or count > (self.size - directory) // 44:
                raise FormatError("Invalid directory count")
            self.entries = {}
            for _ in range(count):
                length = u32(stream)
                if not 0 < length <= 65536 or length % 4:
                    raise FormatError("Invalid directory path length")
                padded = read_exact(stream, length)
                raw = padded.rstrip(b"\0")
                if len(padded) - len(raw) > 3:
                    raise FormatError("Invalid path padding")
                name = canonical_path(raw)
                offset, size = struct.unpack("<2Q", read_exact(stream, 16))
                digest = read_exact(stream, 16)
                if u32(stream) != 0:
                    raise FormatError("Encrypted, removed, delta or unknown entry rejected")
                offset += base
                if name in self.entries or offset < base or offset + size > directory:
                    raise FormatError("Duplicate entry or out-of-bounds payload")
                self.entries[name] = Entry(name, offset, size, digest)
            if stream.tell() != self.size:
                raise FormatError("Unexpected trailing directory data")
            end = base
            for entry in sorted(self.entries.values(), key=lambda e: e.offset):
                if entry.size and entry.offset < end:
                    raise FormatError("Overlapping payloads")
                end = max(end, entry.offset + entry.size)

    def read(self, name, limit=16 * 1024 * 1024):
        entry = self.entries[name]
        if entry.size > limit:
            raise FormatError("Resource exceeds read limit")
        with self.path.open("rb") as stream:
            stream.seek(entry.offset)
            data = read_exact(stream, entry.size)
        if hashlib.md5(data).digest() != entry.md5:
            raise FormatError("Payload MD5 mismatch: " + name)
        return data

    def verify(self):
        with self.path.open("rb") as stream:
            for entry in self.entries.values():
                stream.seek(entry.offset)
                digest = hashlib.md5()
                left = entry.size
                while left:
                    chunk = read_exact(stream, min(left, 1024 * 1024))
                    digest.update(chunk)
                    left -= len(chunk)
                if digest.digest() != entry.md5:
                    raise FormatError("Payload MD5 mismatch: " + entry.name)


def write_pack(destination, additions, original=None):
    """Append replacement data and a new directory; retain original payload offsets."""
    entries = dict(original.entries) if original else {}
    base = original.base if original else 112
    with destination.open("xb") as target:
        if original:
            with original.path.open("rb") as source:
                while chunk := source.read(1024 * 1024):
                    target.write(chunk)
        else:
            target.write(struct.pack("<6I2Q", 0x43504447, 3, 4, 6, 3, 2, base, 0) + bytes(base - 40))
        for name, data in additions.items():
            if canonical_path(name.encode()) != name:
                raise FormatError("Noncanonical added path")
            target.write(bytes(-target.tell() % 16))
            entries[name] = Entry(name, target.tell(), len(data), hashlib.md5(data).digest())
            target.write(data)
        target.write(bytes(-target.tell() % 16))
        directory = target.tell()
        target.write(struct.pack("<I", len(entries)))
        for entry in entries.values():
            raw = entry.name.encode("utf-8")
            padded = raw + bytes(-len(raw) % 4)
            target.write(struct.pack("<I", len(padded)) + padded)
            target.write(struct.pack("<2Q", entry.offset - base, entry.size) + entry.md5 + bytes(4))
        target.seek(32)
        target.write(struct.pack("<Q", directory))


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def path_overrides_disabled(executable):
    # This diagnostic is compiled only in main.cpp's !OVERRIDE_PATH_ENABLED
    # branch. Informational metadata only: ordinary sidecar loading does not
    # use a path override and is independent of this compile option.
    marker = b"`--main-pack` was specified on the command line, but this Godot binary was compiled without support for path overrides."
    with executable.open("rb") as stream:
        tail = b""
        while chunk := stream.read(1024 * 1024):
            data = tail + chunk
            if marker in data:
                return True
            tail = data[-len(marker):]
    return False
