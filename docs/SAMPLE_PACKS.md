# STILL sample packs

STILL reads the shared sample-pack manifest v1. Required top-level fields and asset fields follow the shared contract in `docs/02_SAMPLE_PACKS.md`; this page records only STILL-specific playback rules.

## STILL v1 fields

- `instrument` must be `still`.
- Each asset uses `role: "drone_body"`, `"drone_swirl"`, or `"drone_air"` and a `pitch_class` from 0 through 11 (or `root_midi`, from which the pitch class is derived).
- Loop positions are integer frame counts. `loop_end_frame` is exclusive. The optional `loop_xfade_frames` sets the linear crossfade at the loop tail.
- BODY and SWIRL pitch coverage must match when both roles exist. At each shared pitch class they must use equal frame counts, sample rates and loop points, plus the same non-empty `extensions.still.synchronized_group`. This keeps OPEN morphing phase aligned.
- The optional top-level `extensions.still.original_root_pitch_class` identifies the pack's preferred fallback bank. If omitted, the first BODY or SWIRL pitch class becomes the root.
- Custom v1 assets must be mono or stereo WAV at 8–192 kHz. The reader accepts PCM or float WAV and resamples by playback-rate conversion. Keep exported performance packs at 48 kHz stereo as recommended by the shared preparation workflow.
- STILL rejects unsafe relative paths, missing or corrupt assets, invalid loops, unsupported roles, non-finite or over-full-scale samples, and BODY/SWIRL banks that cannot stay synchronized.

Example extension fields:

```json
{
  "extensions": { "still": { "original_root_pitch_class": 2 } },
  "assets": [
    {
      "id": "body-d",
      "path": "audio/body-d.wav",
      "role": "drone_body",
      "pitch_class": 2,
      "loop_start_frame": 0,
      "loop_end_frame": 288000,
      "loop_xfade_frames": 0,
      "extensions": { "still": { "synchronized_group": "d-main" } }
    }
  ]
}
```

Use an honest `source_license` and retain `provenance.md` with the pack. Record the source, rights, creator, preparation choices, and source/export checksums. The importer adds `importer-checksums.sha256` in the installed copy without changing the source directory or any source-provided checksum file.

## Import and select

The V1 importer accepts a pack directory. It validates the manifest, audio, loop bounds, path containment, provenance, and synchronized layer coverage, then copies the pack into:

```text
~/Library/Application Support/STILL Instruments/Packs/<id>/<version>/
```

Run:

```sh
python3 tools/import_still_pack.py /path/to/PackDirectory
```

Use `--pack-root /path/to/test-packs` to validate an import in a disposable directory. Re-importing identical content is safe. An existing ID/version with different files is rejected; increment `version` for a replacement. Restart STILL or wait for the selector to refresh, then choose the pack in the top bar. Decoding happens on a worker thread and the old pack crossfades for 100 ms after the replacement is ready.

The selector stores `pack_id` and `pack_version` with the plugin state. Old states without these fields use the embedded Factory Stage Drone. A state that names an unavailable or invalid custom pack mutes the instrument and displays a missing-pack warning until that pack is installed or another pack is selected.

## Factory pack and rights

`Assets/FactoryPack/manifest.json` describes the included Stage-cleaned factory sound using the same v1 core fields. The supplied sample kit has no separate redistribution license, so the repository and factory audio remain private. The embedded factory assets and their documented source provenance must not be posted publicly or redistributed.

## Rollback

To return a concert to the approved embedded drone, choose **Factory Stage Drone** in the selector and save a new preset only after checking that the sound is correct. Existing concert files are not changed by importing packs. To remove an imported version, quit MainStage/Logic first, then remove only that version directory under `~/Library/Application Support/STILL Instruments/Packs/<id>/<version>/`; saved states that refer to it will show the missing-pack warning. Keep the factory pack in the private plugin build.

For code rollback, build and validate a known branch or commit in a separate checkout, then install that AU manually after closing hosts. Do not overwrite an open MainStage concert or use a feature build as a performance release before the listening and PA checks in `docs/validation.md` are complete.
