# Development workflow

- Commit C++ source, DSP, tests, sample-processing scripts, docs, CMake files, and presets as normal Git files. Track WAV, MP3, and FLAC audio with Git LFS.
- Preserve supplied sample bytes. Keep generated audio under `Assets/Banks` and record source hashes, processing commands, tool versions, output hashes, and listening-review status.
- Regenerate alternate keys with `tools/render_pitch_banks.py`; use `--force` to replace existing banks.
- Build and run CTest before marking an implementation milestone validated.
- GitHub Actions builds macOS AU and Standalone targets, validates embedded assets and DSP behavior, and runs Apple `auval`.
- Tag only releases that pass automated checks and are ready for performance use. A successful build or AU validation alone is not a performance-ready release.
