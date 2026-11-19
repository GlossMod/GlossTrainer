"""Failure-path regressions; all temporary artifacts stay under root output/."""

import argparse
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import build
import launch
from pck import (FormatError, Pack, bool_variant, path_overrides_disabled,
                 settings_decode, settings_encode, sha256, string_variant, write_pack)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        root = build.ROOT / "output/dndg-trainer-20261118/unit-tests"
        root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def original(self, version=build.GAME_VERSION):
        game = self.root / "game"
        game.mkdir()
        (game / "DnDG_64.exe").write_bytes(b"fake executable; never launched")
        (game / "steam_api64.dll").write_bytes(b"fake DLL")
        settings = {"application/config/name": string_variant(build.GAME_NAME),
                    "application/config/version": string_variant(version),
                    "autoload/Original": string_variant("*res://original.gd"),
                    "application/run/main_scene": string_variant("res://game.tscn"),
                    "unknown/opaque": bytes.fromhex("02000100aabbccddeeff0011")}
        write_pack(game / "DnDG_64.pck", {"project.binary": settings_encode(settings),
                                         "original.gd": b"extends Node\n"})
        return game, settings

    def test_unsupported_game_version_refused(self):
        game, _ = self.original("0.0.0")
        src = self.root / "src"
        src.mkdir()
        args = argparse.Namespace(game_dir=game, src_dir=src,
                                  output=self.root / "out", use_game_saves=False)
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "Only DnDG"):
            build.build(args)
        self.assertFalse(args.output.exists())

    def bundle(self, use_game_saves=False):
        game, settings = self.original()
        src = self.root / "src"
        src.mkdir()
        (src / "trainer_overlay.gd").write_text("extends Node\n", encoding="utf-8")
        args = argparse.Namespace(game_dir=game, src_dir=src,
                                  output=self.root / "out", use_game_saves=use_game_saves)
        with contextlib.redirect_stdout(io.StringIO()):
            build.build(args)
        return game, settings, args, args.output / "bundle"

    def test_binary_settings_roundtrip_preserves_opaque_variant(self):
        data = settings_encode({"opaque": b"\xff" * 12, "utf8/中文": string_variant("测试")})
        self.assertEqual(settings_encode(settings_decode(data)), data)

    def test_settings_reject_truncation_duplicates_and_trailing(self):
        good = settings_encode({"a": string_variant("b")})
        duplicate = b"ECFG" + struct.pack("<I", 2) + good[8:] * 2
        for data in (good[:-1], good + b"x", duplicate, b"NOPE" + good[4:]):
            with self.subTest(data=data), self.assertRaises((FormatError, UnicodeError)):
                settings_decode(data)

    def test_header_flags_versions_and_payload_bounds_fail_closed(self):
        path = self.root / "test.pck"
        write_pack(path, {"a.gd": b"hello"})
        good = path.read_bytes()
        for offset, value, fmt in ((4, 2, "I"), (12, 5, "I"), (20, 3, "I"),
                                   (20, 6, "I"), (24, 1, "Q"), (32, len(good) + 8, "Q")):
            data = bytearray(good)
            struct.pack_into("<" + fmt, data, offset, value)
            path.write_bytes(data)
            with self.subTest(offset=offset, value=value), self.assertRaises(FormatError):
                Pack(path)
        path.write_bytes(good[:-1])
        with self.assertRaises(FormatError):
            Pack(path)

    def test_payload_corruption_detected(self):
        path = self.root / "test.pck"
        write_pack(path, {"a.gd": b"hello"})
        data = bytearray(path.read_bytes())
        data[112] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(FormatError, "MD5 mismatch"):
            Pack(path).verify()

    def test_directory_encryption_flags_and_overlap_rejected(self):
        path = self.root / "test.pck"
        write_pack(path, {"a.gd": b"hello", "b.gd": b"world"})
        good = path.read_bytes()
        directory = struct.unpack_from("<Q", good, 32)[0]
        # Each short path record is 44 bytes: u32 length, four path bytes,
        # two u64 fields, MD5 and flags.
        for offset, value, fmt in ((directory + 4 + 40, 1, "I"),
                                   (directory + 4 + 44 + 8, 0, "Q")):
            data = bytearray(good)
            struct.pack_into("<" + fmt, data, offset, value)
            path.write_bytes(data)
            with self.subTest(offset=offset), self.assertRaises(FormatError):
                Pack(path)

    def test_resource_paths_reject_traversal_and_absolute(self):
        for i, name in enumerate(("../evil.gd", "/evil.gd", "a\\b.gd", "a//b.gd", "C:/evil.gd")):
            with self.subTest(name=name), self.assertRaises(FormatError):
                write_pack(self.root / (str(i) + ".pck"), {name: b"x"})

    def test_append_preserves_original_file_and_payload_offsets(self):
        game, _, _, bundle = self.bundle()
        original = Pack(game / "DnDG_64.pck")
        output = Pack(bundle / "normal/DnDG_64.pck")
        self.assertEqual(output.entries["original.gd"], original.entries["original.gd"])
        self.assertEqual(output.read("original.gd"), original.read("original.gd"))
        original.verify()
        output.verify()

    def test_normal_settings_preserve_autoloads_and_unknown_values(self):
        _, original, _, bundle = self.bundle()
        settings = settings_decode(Pack(bundle / "normal/DnDG_64.pck").read("project.binary"))
        for key, value in original.items():
            self.assertEqual(settings[key], value)
        self.assertEqual(settings["autoload/GlossTrainer"], string_variant("*res://gloss_trainer/trainer_overlay.gd"))
        self.assertEqual(settings["application/config/use_custom_user_dir"], bool_variant(True))

    def test_smoke_has_no_original_game_resources_or_autoloads(self):
        _, _, _, bundle = self.bundle()
        pack = Pack(bundle / "smoke/DnDG_64.pck")
        settings = settings_decode(pack.read("project.binary"))
        self.assertNotIn("original.gd", pack.entries)
        self.assertEqual([k for k in settings if k.startswith("autoload/")], ["autoload/GlossTrainer"])
        self.assertEqual(settings["application/run/main_scene"], string_variant("res://gloss_trainer/__smoke.tscn"))
        launch.prepare(bundle, True, False)

    def test_existing_bundle_never_overwritten(self):
        _, _, args, bundle = self.bundle()
        before = sha256(bundle / "normal/DnDG_64.pck")
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "Bundle exists"):
            build.build(args)
        self.assertEqual(sha256(bundle / "normal/DnDG_64.pck"), before)

    def test_launcher_rejects_changed_original_and_generated_pack(self):
        game, _, _, bundle = self.bundle()
        launch.prepare(bundle, False, False)
        path = bundle / "normal/DnDG_64.pck"
        good = path.read_bytes()
        path.write_bytes(good + b"tampered")
        with self.assertRaisesRegex(ValueError, "PCK hash mismatch"):
            launch.prepare(bundle, False, False)
        path.write_bytes(good)
        (game / "steam_api64.dll").write_bytes(b"new DLL")
        with self.assertRaisesRegex(ValueError, "Original game changed"):
            launch.prepare(bundle, True, False)

    def test_opt_in_game_saves_requires_second_explicit_launch_flag(self):
        _, _, _, bundle = self.bundle(use_game_saves=True)
        with self.assertRaisesRegex(ValueError, "allow-game-saves"):
            launch.prepare(bundle, False, False)
        launch.prepare(bundle, False, True)
        launch.prepare(bundle, True, False)

    def test_launcher_rejects_manifest_pack_path_escape(self):
        _, _, _, bundle = self.bundle()
        path = bundle / "manifest.json"
        data = json.loads(path.read_text())
        outside = bundle.parent / "outside.pck"
        outside.write_bytes((bundle / "normal/DnDG_64.pck").read_bytes())
        data["packs"]["normal"]["file"] = "../outside.pck"
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "Invalid bundle pack path"):
            launch.prepare(bundle, False, False)

    def test_dry_run_never_launches(self):
        _, _, _, bundle = self.bundle()
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle), "--dry-run"]), mock.patch("subprocess.Popen") as spawn, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launch.main(), 0)
            spawn.assert_not_called()

    def test_disabled_path_override_diagnostic_detected(self):
        path = self.root / "exe"
        marker = b"`--main-pack` was specified on the command line, but this Godot binary was compiled without support for path overrides."
        path.write_bytes(b"x" * (1024 * 1024 - 10) + marker)
        self.assertTrue(path_overrides_disabled(path))
        path.write_bytes(b"ordinary executable")
        self.assertFalse(path_overrides_disabled(path))

    def test_running_game_prevents_normal_launch(self):
        _, _, _, bundle = self.bundle()
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle)]), mock.patch("launch.running_game_pids", return_value=[38992]), mock.patch("subprocess.Popen") as spawn, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                launch.main()
            self.assertEqual(failure.exception.code, 1)
            spawn.assert_not_called()

    def test_process_enumeration_failure_prevents_normal_launch(self):
        _, _, _, bundle = self.bundle()
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle)]), mock.patch("launch.running_game_pids", side_effect=OSError("access denied")), mock.patch("subprocess.Popen") as spawn, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                launch.main()
            spawn.assert_not_called()

    def test_game_started_between_checks_prevents_normal_spawn(self):
        _, _, _, bundle = self.bundle()
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle)]), mock.patch("launch.running_game_pids", side_effect=[[], [999]]), mock.patch("subprocess.Popen") as spawn, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                launch.main()
            spawn.assert_not_called()

    def test_normal_command_uses_sidecar_cwd_and_no_arguments(self):
        _, _, _, bundle = self.bundle()
        process = mock.Mock(pid=123456)
        process.wait.return_value = 0
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle)]), mock.patch("launch.running_game_pids", return_value=[]) as enumeration, mock.patch("subprocess.Popen", return_value=process) as spawn, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launch.main(), 0)
        self.assertEqual(spawn.call_args.args[0], [str(bundle / "normal/DnDG_64.exe")])
        self.assertEqual(spawn.call_args.kwargs["cwd"], bundle / "normal")
        self.assertEqual(enumeration.call_count, 2)

    def test_sidecar_copies_are_byte_identical_and_tampering_is_rejected(self):
        game, _, _, bundle = self.bundle()
        for mode in ("normal", "smoke"):
            for name in ("DnDG_64.exe", "steam_api64.dll"):
                self.assertEqual((game / name).read_bytes(), (bundle / mode / name).read_bytes())
        (bundle / "smoke/DnDG_64.exe").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "Sidecar executable/DLL"):
            launch.prepare(bundle, True, False)

    def test_unexpected_sidecar_files_are_rejected(self):
        _, _, _, bundle = self.bundle()
        (bundle / "smoke/override.cfg").write_text("[autoload]\n")
        with self.assertRaisesRegex(ValueError, "Unexpected files"):
            launch.prepare(bundle, True, False)

    def test_packed_trainer_source_hash_is_verified(self):
        _, _, _, bundle = self.bundle()
        path = bundle / "manifest.json"
        data = json.loads(path.read_text())
        data["scripts"]["gloss_trainer/trainer_overlay.gd"] = "0" * 64
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "Trainer source hash mismatch"):
            launch.prepare(bundle, True, False)

    def test_timeout_kills_only_created_child_and_records_failure(self):
        import subprocess
        _, _, _, bundle = self.bundle()
        process = mock.Mock(pid=123456)
        process.wait.side_effect = [subprocess.TimeoutExpired("smoke", 1), 0]
        with mock.patch("sys.argv", ["launch.py", "--bundle", str(bundle), "--smoke", "--timeout", "1"]), mock.patch("subprocess.Popen", return_value=process), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launch.main(), 124)
        process.kill.assert_called_once_with()
        receipt = json.loads(next((bundle / "logs").glob("*.receipt.json")).read_text())
        self.assertTrue(receipt["timed_out"])
        self.assertEqual(receipt["pid"], 123456)


if __name__ == "__main__":
    unittest.main()
