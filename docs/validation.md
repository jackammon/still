# Validation status

Automated local checks currently cover:

- Original WAV format, stereo channel count, sample rate, exact loop endpoint frames, and BODY + SWIRL reconstruction against the full D reference.
- All 33 alternate-key FLACs: hashes, 48 kHz stereo 24-bit metadata, and exact frame counts.
- Embedded decoding for all 12 keys and stable preloaded-memory size across key changes.
- OPEN gain curves, direct 48 kHz D output against the supplied full reference, and AIR independence.
- Every key's A loop boundary, key changes, LEVEL gain, BRIGHTNESS smoothing, and MOTION behavior.
- Long D playback, safe peak range, mono output, and a 44.1 kHz host rate.
- Shared v1 pack manifest and asset validation, synchronized BODY/SWIRL pitch coverage, and directory import idempotence/path safety.
- External-pack loader path containment and two consecutive pack transitions with worker-side retirement.

GitHub Actions builds the AU and Standalone targets, runs these checks, and runs Apple's `auval` on the AU component.

## Remaining listening and host checks

Automated checks do not establish that the alternate-key audio sounds natural, that the interface feels right during performance, or that a MainStage concert behaves correctly. The current version has not yet been loaded into the user's open MainStage concert. Do not alter that concert. The user still needs to evaluate key-bank timbre, noise, loop character, automation feel, and the interface, followed by a rehearsal/PA soundcheck. No live PA test is claimed.

No performance-ready release tag has been created.
