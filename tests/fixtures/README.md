# Stage 2 compatibility fixture

`stage2-v1.gbp` was written with `serializeProject` from revision `60fd8a5`, before
changing the codec. It is 27,111 bytes, format v1, SHA-256:

`003ab5d3897d4ac83bdd79e2a53d5b228be5bc25487d71c5ddb575c56edd0953`

It starts with a default initialized project and these changes:

- BPM 133, swing 62.
- Drum pattern 2, lane 1, step 3: velocity 99.
- Instrument track 1, pattern 4: one note `{startTick=96, lengthTicks=48, pitch=60, velocity=90}`.

Indices above are zero based. Keep this file unchanged when evolving the codec.
The host tests cover migration and using this v1 primary as a backup after saving v2.
The device synth test uploads this file into its isolated test project and checks
loading and saving through the actual SD path.


`stage3-v2.gbp` covers the v2→v3 migration. It extends the fixed v1 payload using the
v2 codec layout: four square-wave SynthSound records (other controls retain factory values)
and quantization flags `{true, false, true, true}`. Its CRC/header are written in the v2
format. Tests verify these settings survive and the mixer/arpeggiator get valid defaults.

`stage4-v3.gbp` is the fixed v2 payload extended with the v3 wire layout: master 37%,
five mixer channels at 100% (unmuted, no solo), and four disabled arpeggiators at
1/16, one octave, 50% gate. Its v3 header/CRC allow testing v3→v4 migration without
using the current serializer to create its own migration input. The new sampler
track, library, and pad patterns must receive valid empty/default values.
