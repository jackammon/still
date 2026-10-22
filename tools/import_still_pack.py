#!/usr/bin/env python3
"""Validate and install a directory-based STILL sample pack without changing its source."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tempfile
from typing import NoReturn


PACK_ROOT = Path.home() / "Library/Application Support/STILL Instruments/Packs"
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_SAMPLE_RATE = 192000
MIN_SAMPLE_RATE = 8000


def fail(message: str) -> NoReturn:
    raise ValueError(message)


def read_manifest(root: Path) -> dict:
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        fail("Pack directory must contain manifest.json.")
    if manifest_path.stat().st_size > MAX_MANIFEST_BYTES:
        fail("manifest.json exceeds the 1 MiB safety limit.")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"Could not read manifest.json: {exc}")
    if not isinstance(manifest, dict):
        fail("manifest.json must contain a JSON object.")
    if manifest.get("schema_version") != 1:
        fail("Only sample-pack schema_version 1 is supported.")
    for key in ("id", "version", "display_name", "instrument", "source_license"):
        if not isinstance(manifest.get(key), str) or not manifest[key].strip():
            fail(f"manifest.json requires a non-empty string {key!r}.")
    if manifest["instrument"] != "still":
        fail('This importer only accepts instrument: "still" packs.')
    for key in ("id", "version"):
        value = manifest[key]
        if len(value) > 128 or not value[0].isascii() or not value[0].isalnum() or any(
            not (char.isascii() and (char.isalnum() or char in "._-")) for char in value
        ):
            fail(f"Pack {key} may contain only letters, digits, dot, underscore, and hyphen.")
    if not (root / "provenance.md").is_file():
        fail("Pack directory must include provenance.md so source rights and processing history travel with it.")
    extensions = manifest.get("extensions", {})
    if not isinstance(extensions, dict):
        fail("manifest.json extensions must be an object when present.")
    still_extension = extensions.get("still", {})
    if not isinstance(still_extension, dict):
        fail("manifest.json extensions.still must be an object when present.")
    root_pitch = still_extension.get("original_root_pitch_class")
    if root_pitch is not None and (not isinstance(root_pitch, int) or isinstance(root_pitch, bool)
                                   or not 0 <= root_pitch <= 11):
        fail("extensions.still.original_root_pitch_class must be an integer from 0 through 11.")
    assets = manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("manifest.json assets must be a non-empty array.")
    return manifest


def wav_info(path: Path) -> tuple[int, int, int, float]:
    """Validate the RIFF/WAVE structure and scan decoded samples for bad values."""
    with path.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            fail(f"Asset is not a RIFF/WAVE file: {path.name}")
        fmt = None
        data_offset = None
        data_size = None
        file_size = path.stat().st_size
        while stream.tell() + 8 <= file_size:
            chunk_head = stream.read(8)
            chunk_id, chunk_size = chunk_head[:4], struct.unpack("<I", chunk_head[4:])[0]
            chunk_offset = stream.tell()
            if chunk_offset + chunk_size > file_size:
                fail(f"WAV chunk extends past end of file: {path.name}")
            if chunk_id == b"fmt ":
                raw = stream.read(min(chunk_size, 40))
                if len(raw) < 16:
                    fail(f"WAV format chunk is incomplete: {path.name}")
                encoding, channels, rate, _, block_align, bits = struct.unpack("<HHIIHH", raw[:16])
                if encoding == 0xFFFE and len(raw) >= 40:
                    encoding = struct.unpack("<H", raw[24:26])[0]
                fmt = (encoding, channels, rate, block_align, bits)
            elif chunk_id == b"data":
                data_offset, data_size = chunk_offset, chunk_size
            stream.seek(chunk_offset + chunk_size + (chunk_size & 1))
        if fmt is None or data_offset is None or data_size is None:
            fail(f"WAV file needs fmt and data chunks: {path.name}")

        encoding, channels, rate, block_align, bits = fmt
        bytes_per_sample = bits // 8
        if channels not in (1, 2) or not MIN_SAMPLE_RATE <= rate <= MAX_SAMPLE_RATE:
            fail(f"Asset must be mono/stereo at 8–192 kHz: {path.name}")
        if encoding not in (1, 3) or bits not in (8, 16, 24, 32, 64):
            fail(f"Asset must use PCM or IEEE float WAV encoding: {path.name}")
        if encoding == 3 and bits not in (32, 64):
            fail(f"IEEE float WAV must use 32-bit or 64-bit samples: {path.name}")
        if encoding == 1 and bits == 64:
            fail(f"PCM WAV must use 8-, 16-, 24-, or 32-bit samples: {path.name}")
        if block_align != channels * bytes_per_sample or data_size == 0 or data_size % block_align:
            fail(f"WAV data chunk has invalid frame alignment: {path.name}")
        frames = data_size // block_align
        if frames <= 0:
            fail(f"Asset has zero frames: {path.name}")

        peak = 0.0
        stream.seek(data_offset)
        remaining = frames * channels
        while remaining:
            count = min(remaining, 8192)
            raw = stream.read(count * bytes_per_sample)
            if len(raw) != count * bytes_per_sample:
                fail(f"WAV data is truncated: {path.name}")
            if encoding == 3:
                code = "f" if bits == 32 else "d"
                values = struct.unpack("<" + code * count, raw)
                for value in values:
                    if not math.isfinite(value) or abs(value) >= 1.0:
                        fail(f"Asset contains non-finite or clipped audio: {path.name}")
                    peak = max(peak, abs(value))
            elif bits == 8:
                if any(value in (0, 255) for value in raw):
                    fail(f"Asset contains full-scale PCM samples (possible clipping): {path.name}")
                peak = max(peak, max((abs(value - 128) / 128 for value in raw), default=0.0))
            elif bits == 16:
                values = struct.unpack("<" + "h" * count, raw)
                if any(value in (-32768, 32767) for value in values):
                    fail(f"Asset contains full-scale PCM samples (possible clipping): {path.name}")
                peak = max(peak, max((abs(value / 32768.0) for value in values), default=0.0))
            elif bits == 24:
                for offset in range(0, len(raw), 3):
                    value = int.from_bytes(raw[offset : offset + 3], "little", signed=False)
                    if value & 0x800000:
                        value -= 0x1000000
                    if value in (-0x800000, 0x7FFFFF):
                        fail(f"Asset contains full-scale PCM samples (possible clipping): {path.name}")
                    peak = max(peak, abs(value / 8388608.0))
            else:
                values = struct.unpack("<" + "i" * count, raw)
                if any(value in (-2147483648, 2147483647) for value in values):
                    fail(f"Asset contains full-scale PCM samples (possible clipping): {path.name}")
                peak = max(peak, max((abs(value / 2147483648.0) for value in values), default=0.0))
            remaining -= count
    return frames, channels, rate, peak


def validate_assets(root: Path, manifest: dict) -> None:
    root = root.resolve(strict=True)
    ids: set[str] = set()
    by_pitch: dict[int, dict[str, tuple[dict, int, int, float, str]]] = {}
    for asset in manifest["assets"]:
        if not isinstance(asset, dict):
            fail("Every asset entry must be a JSON object.")
        for key in ("id", "path", "role"):
            if not isinstance(asset.get(key), str) or not asset[key].strip():
                fail(f"Every asset needs a non-empty string {key!r}.")
        asset_id = asset["id"]
        if asset_id in ids:
            fail(f"Duplicate asset id: {asset_id}")
        ids.add(asset_id)
        role = asset["role"]
        if role not in ("drone_body", "drone_swirl", "drone_air"):
            fail(f"Unsupported STILL asset role {role!r} for {asset_id}.")
        raw_path = asset["path"]
        relative = PurePosixPath(raw_path)
        if relative.is_absolute() or ".." in relative.parts or "\\" in raw_path:
            fail(f"Asset path must be relative and may not traverse directories: {raw_path}")
        candidate = root.joinpath(*relative.parts)
        try:
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError):
            fail(f"Asset is missing or resolves outside the pack: {raw_path}")
        if not resolved.is_file() or resolved.suffix.lower() != ".wav":
            fail(f"Pack v1 STILL assets must be WAV files: {raw_path}")
        pitch = asset.get("pitch_class")
        if pitch is None and isinstance(asset.get("root_midi"), int):
            pitch = asset["root_midi"] % 12
        if not isinstance(pitch, int) or isinstance(pitch, bool) or not 0 <= pitch <= 11:
            fail(f"Asset {asset_id} requires pitch_class 0–11 or root_midi.")
        loop_start = asset.get("loop_start_frame")
        loop_end = asset.get("loop_end_frame")
        xfade = asset.get("loop_xfade_frames", 0)
        if any(not isinstance(value, int) or isinstance(value, bool) for value in (loop_start, loop_end, xfade)):
            fail(f"Asset {asset_id} needs integer loop frame fields.")
        if loop_start < 0 or loop_end <= loop_start or xfade < 0 or xfade * 2 >= loop_end - loop_start:
            fail(f"Asset {asset_id} has invalid loop points or crossfade length.")
        frames, _, sample_rate, peak = wav_info(resolved)
        if loop_end > frames:
            fail(f"Asset {asset_id} loop_end_frame exceeds its WAV length.")
        if role in ("drone_body", "drone_swirl"):
            still_ext = asset.get("extensions", {}).get("still", {})
            group = still_ext.get("synchronized_group", "") if isinstance(still_ext, dict) else ""
            bank = by_pitch.setdefault(pitch, {})
            if role in bank:
                fail(f"Duplicate STILL role/pitch_class entry: {role} / {pitch}.")
            bank[role] = (asset, frames, sample_rate, peak, group)

    body_pitches = {pitch for pitch, bank in by_pitch.items() if "drone_body" in bank}
    swirl_pitches = {pitch for pitch, bank in by_pitch.items() if "drone_swirl" in bank}
    if not body_pitches and not swirl_pitches:
        fail("A STILL pack needs at least one BODY or SWIRL asset.")
    if body_pitches and swirl_pitches and body_pitches != swirl_pitches:
        fail("BODY and SWIRL must provide matching pitch_class coverage.")
    for pitch, bank in by_pitch.items():
        if "drone_body" not in bank or "drone_swirl" not in bank:
            continue
        body, body_frames, body_rate, _, body_group = bank["drone_body"]
        swirl, swirl_frames, swirl_rate, _, swirl_group = bank["drone_swirl"]
        fields = ("loop_start_frame", "loop_end_frame", "loop_xfade_frames")
        if (body_frames != swirl_frames or body_rate != swirl_rate
                or any(body.get(field, 0) != swirl.get(field, 0) for field in fields)
                or body_group != swirl_group or not body_group):
            fail(f"BODY and SWIRL need matching loop metadata and synchronized_group at pitch_class {pitch}.")


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def content_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file() and p.name != "importer-checksums.sha256"):
        relative = path.relative_to(root).as_posix()
        file_hash = hash_file(path)
        digest.update(relative.encode("utf-8") + b"\0" + file_hash.encode("ascii") + b"\n")
    return digest.hexdigest()


def write_checksums(root: Path) -> None:
    entries = []
    for path in sorted(p for p in root.rglob("*") if p.is_file() and p.name != "importer-checksums.sha256"):
        entries.append(f"{hash_file(path)}  {path.relative_to(root).as_posix()}")
    (root / "importer-checksums.sha256").write_text("\n".join(entries) + "\n", encoding="utf-8")


def import_pack(source: Path, pack_root: Path) -> Path:
    if source.is_symlink():
        fail("Pack directory may not be a symlink.")
    source = source.resolve(strict=True)
    if not source.is_dir():
        fail("Source must be a directory containing manifest.json.")
    if any(path.is_symlink() for path in (source, *source.rglob("*"))):
        fail("Pack directory may not contain symlinks.")
    manifest = read_manifest(source)
    validate_assets(source, manifest)
    pack_id, version = manifest["id"], manifest["version"]
    identity = content_digest(source)
    target = pack_root / pack_id / version
    if target.exists():
        if target.is_dir() and any(path.is_symlink() for path in target.rglob("*")):
            fail(f"Installed destination contains symlinks: {target}")
        if target.is_dir() and content_digest(target) == identity:
            return target
        fail(f"{target} already exists with different contents; increment the pack version instead.")

    target.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{version}.import-", dir=target.parent))
    try:
        shutil.copytree(source, staging, dirs_exist_ok=True)
        write_checksums(staging)
        os.replace(staging, target)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack_directory", type=Path, help="STILL pack directory with manifest.json and provenance.md")
    parser.add_argument("--pack-root", type=Path, default=PACK_ROOT,
                        help=f"install root (default: {PACK_ROOT})")
    args = parser.parse_args()
    try:
        target = import_pack(args.pack_directory, args.pack_root.expanduser())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Import failed: {exc}", file=sys.stderr)
        return 1
    print(f"Installed pack: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
