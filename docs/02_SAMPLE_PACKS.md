# Shared sample-pack design — swap sounds without recompiling

## Requirement

This is **core product functionality**, not a later embellishment. The user will repeatedly source Suno audio and/or original recordings, work with the assistant to prepare it, then import new samples through Codex or a built-in pack selector. Replacing sample material must not require a C++ rebuild.

Use a **manifest-driven directory pack** as V1; later allow zipped `.stillpack` archives with verification/import. Keep authoring/source masters separate from instrument-ready exports.

## Example layout

```text
Luminous Strings/
  manifest.json
  audio/
    C4_soft_attack.wav
    C4_loop.wav
    C4_release.wav
    Eb4_soft_attack.wav
    Eb4_loop.wav
    Eb4_release.wav
    A4_soft_attack.wav
    A4_loop.wav
    glint_D5_01.wav
    glint_A5_01.wav
  provenance.md
  artwork.png                  # optional
```

Use different asset types per instrument, not a magical single waveform expected to do everything:

- **BLOOM:** pitched note zones (root MIDI note, key range, velocity range), optional attack/sustain/release components, loop points and crossfade length.
- **PARTICLES:** short one-shots (pitch can be null for unpitched), natural decay, brightness class, category, nominal gain, weight, no exact beats.
- **STILL:** layered seamless drone loops with base key, common duration/alignment metadata, layer roles.
- **AFTERGLOW / MEMORY:** primarily live input; factory packs optional for effect coloration/impulse responses.
- **BODY:** low register velocity layers with release characteristics.

## Manifest v1 contract (minimum)

Required: `schema_version`, `id`, `version`, `display_name`, `instrument`, `source_license`, `assets`.

Each asset: stable `id`, relative `path`, `role`, optional `root_midi`, `key_low`, `key_high`, `velocity_low/high`, `pitch_class`, `loop_start_frame`, `loop_end_frame`, `loop_xfade_frames`, `gain_db`, `tags`, `weight`. Use frame counts rather than floating seconds for reproducibility.

Accept audio sample rates supported by the preparation pipeline and normalize packaged exports to 48k stereo WAV float or PCM with validation. Playback engine must still resample to host rate. Reject corrupt, missing, clipped, zero-length, non-finite, path-traversal, or invalid manifest assets. Validate that loop end > start and crossfade fits loop; verify at least one valid playable zone for BLOOM. Preserve checksum and provenance. See `examples/packs/luminous-strings/manifest.json` for the initial shape.

## Separate authoring from live playback

**Authoring-side assets** (never necessarily committed): original Suno renders, all time-coded selections, denoise profiles, stretch settings, reverse decisions, spectrograms, rejected versions and licensing information.

**Performance pack:** selected, tuned, shortened and looped WAV assets plus manifest only. Package build must be reproducible from source recipes when source licenses permit and files remain available.

## UX expected

1. `Import Pack…` accepts directory or pack archive.
2. Scanner validates, displays instrument type, key/pitch coverage, approximate memory and warnings.
3. New pack is staged in persistent support directory; active audio keeps playing.
4. Performer selects pack or preset; plugin crossfades voices/loops safely and retains released voices until tails finish.
5. Restore concert: load same pack ID/version; if unavailable, fail audibly silent or fallback with **prominent** missing-pack UI warning, never a surprise replacement sound in a live set.
6. Disallow missing packs being silently 'repaired' by searching random files.

For V1, a CLI importer is acceptable if the plugin at least supports selection between installed packs. A GUI import button is a phase-two usability improvement.

## Rights/provenance

The private project can contain derived audio only where licensing permits; a purchased or generated sample is **not automatically redistributable**. Store license terms, source URL, generation date, plan/rights metadata, human note, whether pack may be redistributed, and author/creator. Keep private commercial samples out of public Git and Actions artifacts. Do not assume the same rights for every Suno plan or generation date. Avoid generating artist-clone prompts for commercial distribution.

## Compatibility rules

- `schema_version` governs loader compatibility (reject unknown major versions).
- `pack_id` is stable across edits; increment semantic `version` for replacements.
- Stable asset IDs let future presets refer to a timbre even if file paths move.
- Plugin can restore parameters without mutating the current pack files.
- Tests load two packs back-to-back without audio-thread allocation or stale pointer access.
