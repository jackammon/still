# Audio architecture — first milestone

The three runtime WAV stems are embedded with JUCE BinaryData. During prepareToPlay, before audio callbacks, JUCE decodes them into stereo float buffers. The audio thread never opens files, allocates memory, or performs blocking work.

BODY and SWIRL share one double-precision transport position and 5.75-second period. AIR has its own position and 5.20-second period. At 48 kHz, source sample values pass through unprocessed. Other host rates use wrapped Catmull–Rom interpolation.

At each sample, OPEN applies:

- BODY = 0.34 + 0.66 × OPEN^0.9
- SWIRL = OPEN^1.65

OPEN, AIR, and START each use a short linear ramp. Mono hosts receive the arithmetic mean of stereo output to avoid a 6 dB boost. Validation checks source PCM format, exact loop endpoints, and BODY + SWIRL reconstruction against the supplied full reference.

OPEN, AIR, and START are host-automatable AudioProcessorValueTreeState parameters. Loop positions are not reset by parameter changes. Key transposition, brightness, motion, overall level, and preset import/export are later milestones.

Sample decoding and buffer allocation happen in prepareToPlay. processBlock reads parameter atomics, advances sample positions, applies gain and interpolation, and fills the host-provided output buffer.
