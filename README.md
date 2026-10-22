# STILL

STILL is a native macOS Audio Unit instrument for long, evolving organic drone textures. It plays the user's approved Stage-cleaned recordings as synchronized layers so one OPEN control moves from a quiet BODY foundation to the original full BODY + SWIRL sound.

This private project keeps supplied audio and provenance with the C++ source. WAV and reference MP3 assets are stored with Git LFS. Do not make this repository public or distribute the original samples.

## First milestone

The prototype targets the original D recording:

- BODY and SWIRL share one sample position and repeat period.
- AIR has an independent position and loop period.
- OPEN follows the requested body and swirl curves with smoothed automation.
- AIR is an independent smoothed control.
- START / STOP fades output without resetting either loop position.
- At a 48 kHz host rate, source samples pass through without resampling. Other host rates use wrapped cubic interpolation.
- Original BODY, SWIRL, AIR, full reference WAV, and listening MP3s are preserved under Assets.

Key selection, brightness, motion, overall level, expanded preset handling, and host/long-duration validation are later milestones. This prototype does not claim MainStage or PA acceptance.

## Build on macOS

Requirements: Xcode Command Line Tools, CMake 3.22+, Python 3, Git, and Git LFS.

Install Git LFS if needed, then initialize it for this repository:

    brew install git-lfs
    git lfs install --local

Fetch audio objects, configure, build, and run checks:

    git lfs pull
    cmake --preset macos-release
    cmake --build --preset macos-release --parallel
    ctest --preset macos-release

CMake fetches JUCE 9.0.4 from the official JUCE repository at configure time. The build produces an Audio Unit component and a Standalone app.

## Audio and rights

The supplied WAV files are authoritative; MP3 files are listening references only. Preserve the source files. Put any derived audio in a separate generated-assets directory and record its process and source hashes. See docs/SAMPLE_PROVENANCE.md and docs/architecture.md.

The kit describes the material as user-selected/generated audio but has no separate sample licence. Keep this repository private and verify applicable rights before sharing the source or a plugin containing samples. JUCE licensing also applies; see docs/DEPENDENCIES.md.
