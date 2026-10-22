#!/usr/bin/env python3
"""Render equal-duration, 24-bit alternate-key samples for STILL.

Requires Rubber Band CLI (R3 engine) and FFmpeg. D is deliberately not rendered:
the Stage-cleaned D sources remain the untouched reference bank.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "Assets"
BANKS = ASSETS / "Banks"
SOURCES = {
    "BODY": ASSETS / "Stage" / "D_A_BODY_STAGE_48k.wav",
    "SWIRL": ASSETS / "Stage" / "D_A_SWIRL_STAGE_48k.wav",
    "AIR": ASSETS / "Stage" / "D_C_AIR_STAGE_48k.wav",
}
KEYS = [
    ("C", -2), ("Cs", -1), ("D", 0), ("Ds", 1), ("E", 2), ("F", 3),
    ("Fs", 4), ("G", 5), ("Gs", 6), ("A", 7), ("As", 8), ("B", 9),
]
SAMPLE_RATE = 48_000
FRAMES = {"BODY": 276_000, "SWIRL": 276_000, "AIR": 249_600}


def run(args: list[str]) -> str:
    completed = subprocess.run(args, check=True, text=True, capture_output=True)
    return (completed.stdout or completed.stderr).strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def stream_metadata(path: Path) -> dict[str, int]:
    data = json.loads(run([
        "ffprobe", "-v", "error", "-select_streams", "a:0", "-show_streams",
        "-of", "json", str(path),
    ]))
    streams = data.get("streams", [])
    if len(streams) != 1:
        raise RuntimeError(f"Expected one audio stream in {path}")
    stream = streams[0]
    return {
        "sample_rate": int(stream["sample_rate"]),
        "channels": int(stream["channels"]),
        "frames": int(stream["duration_ts"]),
        "bits_per_sample": int(stream.get("bits_per_raw_sample") or 0),
    }


def verify(path: Path, stem: str) -> None:
    metadata = stream_metadata(path)
    expected = {"sample_rate": SAMPLE_RATE, "channels": 2, "frames": FRAMES[stem]}
    for key, value in expected.items():
        if metadata[key] != value:
            raise RuntimeError(f"{path.name}: expected {key}={value}, got {metadata[key]}")
    if metadata["bits_per_sample"] != 24:
        raise RuntimeError(f"{path.name}: expected lossless 24-bit PCM content")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="replace generated bank files")
    args = parser.parse_args()
    rubberband = shutil.which("rubberband")
    ffmpeg = shutil.which("ffmpeg")
    if rubberband is None or ffmpeg is None or shutil.which("ffprobe") is None:
        raise SystemExit("Install Rubber Band CLI and FFmpeg (including ffprobe) first.")
    version = run([rubberband, "--version"])
    match = re.search(r"(\d+)\.(\d+)", version)
    if match is None or int(match.group(1)) < 3:
        raise SystemExit(f"Rubber Band 3 or newer is required; found {version!r}.")
    for source in SOURCES.values():
        if not source.is_file():
            raise SystemExit(f"Missing authoritative source: {source}")
    BANKS.mkdir(parents=True, exist_ok=True)
    expected_outputs = [BANKS / f"{key}_{stem}.flac" for key, shift in KEYS if shift
                        for stem in SOURCES]
    if not args.force:
        existing = [path for path in expected_outputs if path.exists()]
        if existing:
            raise SystemExit("Generated bank files already exist; use --force to replace them.")

    output_records: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="still-pitch-bank-") as temporary:
        temp_root = Path(temporary)
        for key, semitones in KEYS:
            if semitones == 0:
                continue
            for stem, source in SOURCES.items():
                temp_wav = temp_root / f"{key}_{stem}.wav"
                target = BANKS / f"{key}_{stem}.flac"
                duration = FRAMES[stem] / SAMPLE_RATE
                run([rubberband, "-3", "-D", f"{duration:.8f}", "-p", str(semitones),
                     str(source), str(temp_wav)])
                verify(temp_wav, stem)
                run([ffmpeg, "-v", "error", "-y", "-i", str(temp_wav), "-c:a", "flac",
                     "-compression_level", "8", str(target)])
                verify(target, stem)
                output_records.append({
                    "key": key,
                    "semitones_from_D": semitones,
                    "stem": stem,
                    "path": str(target.relative_to(ROOT)),
                    "frames": FRAMES[stem],
                    "sample_rate": SAMPLE_RATE,
                    "channels": 2,
                    "bits_per_sample": 24,
                    "sha256": sha256(target),
                })
                print(f"Rendered {key} {stem} ({semitones:+d} semitones)")

    manifest = {
        "format_version": 1,
        "reference_key": "D",
        "reference_samples_are_unprocessed": True,
        "sample_rate": SAMPLE_RATE,
        "rubber_band_version": version,
        "rubber_band_options": ["-3", "-D <original duration>", "-p <semitones>"],
        "ffmpeg_version": run([ffmpeg, "-version"]).splitlines()[0],
        "sources": {stem: {"path": str(path.relative_to(ROOT)), "sha256": sha256(path)}
                    for stem, path in SOURCES.items()},
        "derived_samples": output_records,
        "notes": [
            "The D source bank is copied from the authoritative Stage-cleaned WAV files without pitch processing.",
            "Each derived BODY/SWIRL pair has the same duration, frame count, sample rate, and Rubber Band settings.",
            "Alternate keys require listening evaluation for timbre and loop behavior before live use.",
        ],
    }
    manifest_path = BANKS / "BANK_MANIFEST.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote provenance manifest: {manifest_path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
