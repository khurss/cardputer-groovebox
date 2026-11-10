# Notes for the stage 3 plan

Historical findings from the final stage 2 review.
See the [current documentation](../../README.md) for implemented behavior.

## Required in stage 3

- Events beyond 64 per block are silently lost. Reserve space for NoteOff and
  AllNotesOff and size the buffer for the worst case.
- Reject `lengthTicks = 0` on creation/load; validate `startTick`, velocity and
  `DrumSound` values when loading.
- The measured 9–11% load covers only the snare. Measure all eight drums with synth
  voices before fixing the voice count.
- Do not put a full 388-byte `InstrumentPattern` in `Command`: queue entries copy
  by value. Use individual note commands.
- A new file version makes old files return BadVersion, causing LOAD ERR and
  blocking autosave. Ship migration with the format change.
- Memory: 165 KB static data plus a 65 KB sprite. Four `Project` copies consume much
  of it; remove the `verified` copy first.

## Stage 4

- `running_` duplicates `playing`: song completion inverts Space behavior.
- `SetScenePattern` cannot express an empty pattern.
- `selectPattern` immediately changes scene 0, conflicting with bar-quantized launches.
- Scene launch from Song counts bars from the entry start; entry end is cached
  while scene length can change live.

## Stage 5

- Changing swing/grid during playback may double or lose a hit once
  (`sequencer.cpp`, exact tick equality).
- The shortcut to exclude a track from swing is missing.
- Overload counters are USB-only; the specification calls for an on-screen indicator.
- Address clicks on rapid retrigger, limiter overshoot below 0.4%, a playhead that
  ignores swing, and labels for the other tracks.

## Stages 6 and 7

- Confirm that Launcher itself creates the sample partition.
- Separately check voice recording over loud playback.
- Add a key or dialog to release the autosave lock after LOAD ERR or BACKUP.
