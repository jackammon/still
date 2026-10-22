# Development workflow

- Commit C++ source, DSP, tests, sample-processing scripts, docs, CMake files, and presets as normal Git files. Track WAV, MP3, and FLAC audio with Git LFS.
- Preserve supplied sample bytes. Keep generated audio under `Assets/Banks` and record source hashes, processing commands, tool versions, output hashes, and listening-review status.
- Regenerate alternate keys with `tools/render_pitch_banks.py`; use `--force` to replace existing banks.
- Build and run CTest before marking an implementation milestone validated.
- GitHub Actions builds macOS AU and Standalone targets, validates embedded assets and DSP behavior, and runs Apple `auval`.
- Tag only releases that pass automated checks and are ready for performance use. A successful build or AU validation alone is not a performance-ready release.
- Import performance packs with `python3 tools/import_still_pack.py <pack-directory>`. The importer copies validated files to the user's Application Support directory, keeps source packs unchanged, and refuses to replace an existing ID/version with different content.
- Keep pack audio private unless its source license explicitly allows redistribution. Preserve `provenance.md`, license terms, and checksums with each pack.
- A pack-selection rollback is the embedded **Factory Stage Drone** in the plugin menu. A host state that references an absent pack displays a warning and outputs silence. Code rollbacks should be built from a known-good commit in a separate checkout and installed only after closing audio hosts.
