# Sample provenance and rights

The user supplied the original kit from:

`/Users/jackammon/Downloads/D_drone_stage_MainStage_kit`

Its setup note describes the material as user-selected/generated Stage-cleaned audio and identifies the WAVs as authoritative. No separate sample licence, contributor release, or generation-service licence statement was included. Files are copied unchanged under `Assets/Stage` and `Assets/References`; the Downloads source remains untouched.

`Assets/Banks` contains offline-transposed BODY, SWIRL, and AIR stems for the 11 keys other than D. `tools/render_pitch_banks.py` uses Rubber Band R3 with a fixed target duration and FFmpeg to write lossless stereo 24-bit FLAC. D is deliberately excluded from that process. The generated files stay separate from the source WAVs, and `Assets/Banks/BANK_MANIFEST.json` records each source hash, output hash, transposition, frame count, sample format, and tool version. Alternate banks still need subjective review for musical quality and loop behavior before performance use.

This repository is private. Do not make it public, send samples to collaborators, or distribute a plugin containing them unless the user confirms rights and destination. Keep derived or pitch-shifted material separate from the original files and record the tool/version, command, input hash, output hash, and listening-review status.

SHA-256 hashes for the supplied assets are recorded in `Assets/SHA256SUMS.txt`; generated-bank hashes are in `Assets/Banks/BANK_MANIFEST.json`. Run `tools/validate_audio_assets.py` to verify source PCM, endpoints, BODY + SWIRL reconstruction, bank metadata, and derived-file hashes.
