#!/usr/bin/env python3
"""Small filesystem and manifest checks for the directory pack importer."""

import importlib.util
import json
import struct
import tempfile
import unittest
import wave
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("import_still_pack", REPO / "tools/import_still_pack.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


def write_wav(path: Path, tone: float = 0.2) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        frames = []
        for index in range(256):
            sample = int(32767 * tone * (1 if index % 2 else -1))
            frames.append(struct.pack("<hh", sample, sample))
        wav.writeframes(b"".join(frames))


def make_pack(root: Path) -> Path:
    root.mkdir()
    (root / "audio").mkdir()
    write_wav(root / "audio/body.wav", 0.1)
    write_wav(root / "audio/swirl.wav", 0.08)
    manifest = {
        "schema_version": 1,
        "id": "test.still.pack",
        "version": "1.0.0",
        "display_name": "Test Still Pack",
        "instrument": "still",
        "source_license": "Synthetic test audio; generated for automated tests.",
        "extensions": {"still": {"original_root_pitch_class": 2}},
        "assets": [
            {
                "id": "body-d", "path": "audio/body.wav", "role": "drone_body",
                "pitch_class": 2, "loop_start_frame": 0, "loop_end_frame": 256,
                "loop_xfade_frames": 0,
                "extensions": {"still": {"synchronized_group": "d-main"}},
            },
            {
                "id": "swirl-d", "path": "audio/swirl.wav", "role": "drone_swirl",
                "pitch_class": 2, "loop_start_frame": 0, "loop_end_frame": 256,
                "loop_xfade_frames": 0,
                "extensions": {"still": {"synchronized_group": "d-main"}},
            },
        ],
    }
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (root / "provenance.md").write_text("Synthetic test samples, generated in test code.\n", encoding="utf-8")
    return root


class ImportStillPackTests(unittest.TestCase):
    def test_valid_pack_installs_with_checksums_and_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = make_pack(directory / "source")
            pack_root = directory / "installed"
            installed = IMPORTER.import_pack(source, pack_root)
            self.assertTrue((installed / "importer-checksums.sha256").is_file())
            self.assertTrue((installed / "provenance.md").is_file())
            self.assertFalse((source / "checksums.sha256").exists())
            self.assertEqual(installed, IMPORTER.import_pack(source, pack_root))

    def test_path_traversal_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = make_pack(Path(temporary) / "source")
            manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
            manifest["assets"][0]["path"] = "../outside.wav"
            (source / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "traverse"):
                IMPORTER.validate_assets(source, manifest)

    def test_body_swirl_pitch_coverage_must_match(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = make_pack(Path(temporary) / "source")
            manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
            manifest["assets"][1]["pitch_class"] = 3
            with self.assertRaisesRegex(ValueError, "coverage"):
                IMPORTER.validate_assets(source, manifest)

    def test_full_scale_pcm_is_rejected_as_possible_clipping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            clipped = Path(temporary) / "clipped.wav"
            write_wav(clipped, 1.0)
            with self.assertRaisesRegex(ValueError, "possible clipping"):
                IMPORTER.wav_info(clipped)

    def test_different_content_cannot_replace_same_id_and_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = make_pack(directory / "source")
            pack_root = directory / "installed"
            installed = IMPORTER.import_pack(source, pack_root)
            with (installed / "audio/body.wav").open("ab") as stream:
                stream.write(b"changed")
            with self.assertRaisesRegex(ValueError, "different contents"):
                IMPORTER.import_pack(source, pack_root)


if __name__ == "__main__":
    unittest.main()
