# Notes for the stage 2 plan

Historical findings from the final prototype and timing-core review.
See the [current documentation](../../README.md) for implemented behavior.

## Required in stage 2

- Validate ranges when loading projects and applying commands: `bpm`, `length`,
  `repeats`, `noteCount` and scene index. Zero `bpm` or `length` divides by zero in
  the audio task; lengths above 16 and excess notes access arrays out of bounds.
- Add `CMakeLists.txt` for `gb_core`. For a root project, `tools/idf.sh` creates
  `components/fatfs`, but `.gitignore` currently ignores only the probe copy.
  Verify that `sed` actually enables exFAT.
- UI edits must reach `Project` without tearing: `render` reads it directly, all
  `Sequencer` calls run in the audio task, and commands use an SPSC queue.
- `Project` occupies about 27 KB. Reset it in place rather than copying it onto
  the stack; undo must not store full project copies.
- Drum-grid conversion with step remapping and undo is still missing.
- The engine must tolerate NoteOff without a matching NoteOn.
- Detect and report the card filesystem; the exFAT risk remains open.

## Stage 3

- Reserve event-buffer space for NoteOff and AllNotesOff; overflow currently loses
  them and leaves notes stuck.
- Reject `lengthTicks = 0` when creating or loading notes.
- The swung end of a nearly full-length off-grid note can land just after its start
  (`start 30, length 378`, swing 75).
- Test `playSong` during playback and note release across a loop boundary.

## Stage 4

- Launching a scene from a song counts bars from the song-entry start rather than
  global time.
- A song entry's end is cached even though scene length can change live.

## Stages 6 and 7

- Confirm that Launcher itself creates the sample partition during installation.
- Separately check voice recording over a loud track: the microphone picks up the
  tone much more strongly than speech.
- The observed worst SD write is 47 ms; behavior at that latency is unconfirmed.

## Validation rules

Device checks run unattended: cable installation, speaker signal measured by the
microphone, and results returned over USB.
