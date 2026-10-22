# STILL

STILL is a private macOS Audio Unit instrument for organic, long-form drone textures. Its original D bank plays the user's approved Stage-cleaned samples directly. BODY and SWIRL remain synchronized under OPEN, while AIR runs independently.

The current instrument exposes KEY, OPEN, AIR, BRIGHTNESS, MOTION, LEVEL, and START to host automation. It accepts MIDI input for use as a MainStage/Logic instrument, but the drone starts without held notes. Host parameter mapping is handled through the Audio Unit's exposed parameters.

## Included audio

- `Assets/Stage` contains the supplied WAV sources and reference MP3s, preserved unchanged.
- `Assets/Banks` contains the 11 offline-transposed alternate keys in lossless FLAC. D is always the original, unprocessed source bank.
- Git LFS stores source and generated audio. The repo is private because the source kit has no separate sample licence. Do not make the repository public or distribute the original samples.
- `Assets/Banks/BANK_MANIFEST.json` records source hashes, output hashes, processing tools, sample format, and transposition.

The D reference path uses the 48 kHz source samples without pitch, filtering, or motion processing when BRIGHTNESS is full, MOTION is off, LEVEL is 0 dB, and AIR is muted. Other keys are pitch-shifted offline with Rubber Band R3; their timbre and loop behavior still require listening evaluation.

## Build on macOS

Requirements: Xcode Command Line Tools, CMake 3.22+, Python 3, Git, and Git LFS. CMake fetches JUCE 9.0.4 at configure time. Rubber Band CLI and FFmpeg/ffprobe are only needed to regenerate the alternate-key banks; they are not linked into or bundled with the plugin.

    git lfs install --local
    git lfs pull
    cmake --preset macos-release
    cmake --build --preset macos-release --parallel
    ctest --preset macos-release

The build produces an Audio Unit component and a Standalone app. The macOS GitHub Actions workflow builds both, validates all embedded assets and key-bank metadata, runs DSP checks, and runs Apple's AU validation tool.

## Regenerate alternate keys

Install Rubber Band CLI and FFmpeg (including ffprobe), then run:

    python3 tools/render_pitch_banks.py

The script leaves D unchanged, generates fixed-duration 24-bit stereo samples for the other keys, and writes a provenance manifest. Use `--force` to replace already-generated banks. The project's JUCE build does not require Rubber Band or FFmpeg.

## Validation status

Automated tests cover source integrity, all 11 FLAC banks, BODY/SWIRL frame alignment, D reconstruction, all-key loop seams, key-change crossfades, AIR independence, LEVEL, BRIGHTNESS, MOTION, mono output, and 44.1 kHz operation. GitHub Actions also runs `auval`.

The current code has not been auditioned in MainStage by the user, tested through a PA, or approved for performance. The alternate keys need listening evaluation. See [docs/validation.md](docs/validation.md), [docs/SAMPLE_PROVENANCE.md](docs/SAMPLE_PROVENANCE.md), and [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
