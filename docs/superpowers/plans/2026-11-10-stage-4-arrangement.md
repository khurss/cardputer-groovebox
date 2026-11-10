# Stage 4: scenes, song, mixer and synth improvements

Historical implementation record. See the [current guide](../../stage-4.md).

The project author requested the next stage, clear-current-pattern controls,
immediately visible notes, an arpeggiator, full-song arrangement and volume control,
and authorized implementation. All synthetic hardware tests must stay at **5% maximum volume**.

## Implementation order

- Model: MASTER + DRUM/T1..T4 mixer with volume/mute/solo; Off/Up/Down/UpDown
  arpeggiator, grid, 1–3 octaves and gate; file v3 with v1/v2 loading.
- Sequencer: scene launch on a global bar boundary, song response to length edits,
  playback from any entry and safe stopping after chain edits.
- Engine: fixed arpeggiator buffers for live/clip chords, separate track levels,
  master after the limiter and an independent 5% test ceiling.
- UI: Fn+1..5 / Tab for Drums, Synth, Scenes, Song, Mixer; 16×5 scenes and up to
  64 song entries with repeats. Navigation must not overwrite scene 0.
- Fn+Backspace clears only current-pattern content with undo. During recording,
  finalize holds and accept results first. Piano roll follows pitch, immediately
  shows held keys and focuses existing notes on opening.
- Checks: core/I/O/UI regression, arpeggiator/voice release, independent levels,
  screen bounds, hardware scenarios and load. Isolate the test project and force
  master 5% plus output ceiling 5% on boot, fixture import and reset.

Delay/reverb/sidechain remain a separate part of the planned stage 5. This stage
implements levels, mute and solo. The arpeggiator records the source chord and
settings, then generates the sequence at playback, preserving the 64-note limit.

## Completed

Implementation and installation on Cardputer completed. All 172 C++ tests with
ASan/UBSan, 49 Python tests and four hardware scenarios passed: UI/arrangement,
four arpeggiators under load, synth regression and drum regression; all reported
`0 failures`.

Single-audio-core peaks were 47.0% for four arpeggiators with eight drums, 47.1% for
dense synths at 40 BPM and 49.4% at 240 BPM. Each stress run lasted 60 seconds after
warmup. The pool remained five voices at 32 kHz. No new I2S/event overflows or
exchange errors occurred.

Clear is available by keyboard and labeled on both screens. FIT shows the entire
note range; Fn+F returns to precise zoom. Recording over Song targets the scene
actually playing and ends at the next scene boundary. Note duration uses continuous
transport time rather than resettable scene time.

Tests have an independent 5% post-limiter ceiling, master 5% after every reset/import,
actual PCM peak checks (at most 1557) and I2S result checks. Microphone background
noise is not a mandatory audibility threshold at this level. The old probe was also
limited to 5% and built, but was not installed in place of the main firmware.

File v3 reads v1/v2. User-project and backup checksums matched after every scenario.
Controls and reproducible commands: [stage 4 guide](../../stage-4.md).
