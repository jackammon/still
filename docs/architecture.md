# Audio architecture

## Sample banks

The three original D stems are embedded from `Assets/Stage`. Rubber Band R3 renders BODY, SWIRL, and AIR at the same semitone offset for the other 11 keys. The derived files are lossless stereo 24-bit FLAC at 48 kHz. BODY and SWIRL have the same frame count and are always read from one shared phase position. AIR has its own position and loop period.

All 12 banks decode during `prepareToPlay`, outside the real-time callback. This uses about 80 MiB of decoded audio per plugin instance, trading memory for allocation-free, disk-free key changes. Key changes crossfade over 100 ms. The 11 pitch-shifted banks use a 5 ms loop-boundary crossfade with a matching playback-rate compensation so their repeat period remains the original 5.75 seconds for A and 5.20 seconds for AIR. The original D source skips this processing.

At 48 kHz, the D source samples pass through without interpolation. Other host sample rates use wrapped Catmull–Rom interpolation. Mono hosts receive the arithmetic mean of stereo channels.

## Controls

At each sample, OPEN applies:

- BODY = `0.34 + 0.66 × OPEN^0.9`
- SWIRL = `OPEN^1.65`

AIR independently controls the asynchronously looping AIR layer. START fades the output without resetting sample positions. LEVEL applies smoothed output gain in dB. BRIGHTNESS is a smoothed low-pass control; full brightness is a direct bypass so the D reference setting stays unchanged. MOTION adds very slow, low-depth playback-rate variation and is exactly neutral at zero.

All controls use AudioProcessorValueTreeState parameters for host automation and state save/restore. The instrument accepts MIDI input as an AU music device but does not require held notes to run.

## Real-time constraints

Sample decode and buffer allocation happen in `prepareToPlay`. `processBlock` reads parameter atomics, advances preloaded buffers, applies smoothing and interpolation, and fills the host-provided output. It performs no file I/O, allocation, or locking. The source loops remain in place when START is off so playback resumes without a phase reset.
