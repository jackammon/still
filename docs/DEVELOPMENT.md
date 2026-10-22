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

## Local Audio Unit install and rollback

The macOS build places the component at `out/build/macos-release/STILL_artefacts/Release/AU/STILL.component`. GitHub Actions installs only into its disposable runner to run `auval`; local installation is a separate, manual step.

Before installing, quit MainStage and Logic. Keep a backup of the current component, then copy the validated build and refresh Apple's component registrar:

```sh
STILL_COMPONENT_SOURCE="out/build/macos-release/STILL_artefacts/Release/AU/STILL.component"
STILL_COMPONENT_DEST="$HOME/Library/Audio/Plug-Ins/Components/STILL.component"
STILL_COMPONENT_BACKUP="$STILL_COMPONENT_DEST.backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$(dirname "$STILL_COMPONENT_DEST")"
if [ -d "$STILL_COMPONENT_DEST" ]; then
  ditto "$STILL_COMPONENT_DEST" "$STILL_COMPONENT_BACKUP"
fi
ditto "$STILL_COMPONENT_SOURCE" "$STILL_COMPONENT_DEST"
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

To roll back, quit audio hosts and restore the timestamped backup created above:

```sh
STILL_COMPONENT_BACKUP="/path/to/the-STILL.component.backup-created-above"
STILL_COMPONENT_DEST="$HOME/Library/Audio/Plug-Ins/Components/STILL.component"
ditto "$STILL_COMPONENT_BACKUP" "$STILL_COMPONENT_DEST"
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

If there was no earlier component, remove the newly installed `STILL.component` instead. This replaces only the Audio Unit bundle; it does not edit or overwrite any MainStage concert. Keep the known-good `5ae8d0b` build for performance use until the feature build has been auditioned in MainStage and checked on the PA.
