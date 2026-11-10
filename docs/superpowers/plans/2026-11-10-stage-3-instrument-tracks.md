# Groovebox stage 3: instrument tracks implementation plan

Date: 2026-11-10. Status: completed; final host and Cardputer checks passed.
This is a historical plan. See the [current synth guide](../../stage-3.md) for controls.

**Goal:** play a synth from the keyboard over the existing drum machine, record a
polyphonic loop, edit notes in a piano roll and save the project to SD.

**Basis:** [design specification](../specs/2026-11-09-groovebox-design.md),
[stage 3 review notes](stage-3-notes.md) and stage 3 findings in
[stage 2 notes](stage-2-notes.md). Baseline revision: `60fd8a5`; before changes,
122 C++ tests with ASan/UBSan and 40 Python tests passed.

## User outcome

- Four instrument tracks, 16 patterns each, up to 64 notes per pattern.
- A shared pool targeting eight voices: sine, triangle, saw, square, ADSR, resonant
  low-pass and glide. Final capacity depends on device measurements with all drums.
- Chords with transport stopped or playing, with two keyboard octaves.
- Overdub recording with the selected grid or quantize off.
- Piano roll: add, select, delete, change pitch/start/length/velocity.
- Pattern copy/paste and undo of the last edit or recording pass.
- Synth parameters and parts survive reboot; stage 2 projects still load.

## Constraints and decisions

- Retain 32 kHz, 16-bit mono and 256-frame audio blocks. No allocation, file I/O or
  waiting for UI in the audio task during playback.
- The audio task owns `audioProject`; UI owns `uiProject`. Display code does not
  read the mutable audio model. Commands use SPSC; recording results return over
  a separate SPSC. Model mutation is not shared across cores.
- Do not add a full `InstrumentPattern` to `Command`. Single edits use one Note;
  paste/undo uses a transaction of individual commands.
- Live keys apply at the next block: recording resolution 96 PPQN and up to 8 ms
  processing latency, excluding I2S output queues and keyboard polling. Quantize
  off preserves ticks instead of rounding to editor steps.
- The audio task records using sequencer time; redraw rate and SD latency do not
  determine note lengths. Holds bind to physical keys, not the current octave.
- Until stage 4, selected track patterns are assigned to scene 0 for combined
  playback. Changing assignment releases voices on that track.
- Instrument-grid changes retain absolute starts/lengths. Pattern duration remains
  `length * gridTicks`, so notes beyond the new end remain stored but silent.
  Restoring length/grid makes them available again; the editor marks hidden notes.
- Model limits: start 0–1535, length 1–1536 ticks, MIDI pitch 0–127, velocity 1–127.
  New notes start inside the current loop and last no more than one loop. Loading
  does not discard notes beyond a shortened pattern.
- A recorded note crossing the loop end stores start modulo pattern length and a
  positive duration. Holds longer than one loop are capped at one loop without
  repeatedly adding notes.
- Separate Scenes/Song views, mixer/effects and sampler are later stages.

## Proposed controls at this stage

Use existing physical keyboard codes. Firmware translates printed arrows via Fn
rather than expecting dedicated arrow keycodes from the controller.

| Keys | Instrument-screen action |
|---|---|
| Tab, Fn+1, Fn+2 | Switch Drums/Instrument or select directly |
| Space | Global start/stop |
| Alt | Toggle recording; while stopped, arm only |
| z x c v b n m , | Lower-octave white keys, C through next C |
| s d g h j | Lower-octave black keys |
| q w e r t y u i | Next-octave white keys |
| 2 3 5 6 7 | Next-octave black keys |
| −, = | Octave down/up within MIDI range; later moved to Fn+−/+ |
| Fn+[ / Fn+] | Previous/next instrument track |
| [ / ] | Previous/next pattern |
| Fn+arrows | Time/pitch cursor; selected-note start/pitch edits |
| Enter | Select under cursor or add a note |
| Fn+Enter | Deselect; repeated selection cycles overlapping notes |
| Backspace | Delete selected note |
| Shift+Fn+←/→ | Decrease/increase note length |
| Shift+Fn+↑/↓ | Increase/decrease velocity |
| Fn+L, Fn+G, Fn+W | Length, grid, global swing |
| Fn+Q | Recording quantize: pattern grid / off |
| Fn+C, Fn+V, Fn+Z | Copy, paste, undo |
| Fn+S | Synth parameters, selected/adjusted with arrows |

Keep existing drum controls. Parameter mode releases notes and uses arrows for the
selected parameter. Changing screen/track/pattern finishes recording, releases live
voices and disarms recording.

## Task 1. Model, validation and individual commands

**Files:** `model.h`, `commands.h/.cpp`, `validate.h/.cpp` in `components/gb_core/`;
`tests/test_instrument_model.cpp`, `tests/test_validate.cpp`, `tests/test_commands.cpp`.

- [x] Add `Waveform`, compact per-track `SynthSound`, and per-track recording quantize.
  Parameters: waveform, attack, decay, sustain, release, cutoff, resonance, glide,
  level. Controllers use 0–127; validate waveform as an enum.
- [x] Define deterministic defaults and DSP ranges: attack/decay 0–2 s, release
  0–4 s, glide 0–1 s, cutoff 100–12000 Hz. Limit resonance to a stable filter range.
- [x] Validate note bounds, `DrumSound`, step velocities and synth parameters on
  load/edit. Reject zero-length and zero-velocity active notes.
- [x] Add Add/Set/Delete/ClearNote, instrument length/grid and SetSynthParam.
  Validate command fields and target `noteCount`/index; invalid operations leave
  the model intact.
- [x] Separate LiveNoteOn/LiveNoteOff, record enable and live stop from edit commands.
  Event track numbers are 1–4; instrument array indices are 0–3. Test conversion.
- [x] Add Begin/Set/Commit/Cancel bulk edits with one fixed staging pattern in the
  engine. Validate/replace atomically at Commit; incomplete transactions do not play.
- [x] Enforce `sizeof(Command)` at build time: a small note shares payload storage
  with `DrumPattern` rather than adding 388 bytes to every queue entry.

**Checks:** all command boundaries, 64th/65th note, invalid parameters, partial
transactions and identical results in both project copies.

## Task 2. File v2 and memory savings

**Files:** `project_io.h/.cpp`, `tests/test_project_io.cpp`, and a small v1 fixture
in `tests/fixtures/` generated using the baseline revision's format.

- [x] Add v2 synth/quantize fields while retaining an explicit bytewise codec.
- [x] Read v1 with defaults for new fields. Migrate in memory and return success;
  modify the file only through a normal successful save.
- [x] Validate CRC, size, version and full model before changing `out`. Unknown
  versions remain BadVersion; corruption must not become a successful empty project.
- [x] Remove static `Project verified`; validate temporary/primary files through
  the shared decoder and existing scratch, retaining unchanged `out` on failure.
- [x] Keep `.tmp` → verification → `.bak` → primary and LOAD ERR/BACKUP protection.
  A valid v1 file is a valid backup candidate.
- [x] Verify v2 fits `kProjectBufferSize`; test the actual serialized size.

**Checks:** unchanged v1 fixture → migration → v2 → reload; every parameter/note;
corrupt/truncated versions; backup in another supported version; original file after
failed saving. Ship model fields and migration together.

## Task 3. Note release and event capacity

**Files:** `sequencer.h/.cpp`, `engine.h`, `tests/test_sequencer.cpp`.

- [x] Replace silent loss with a release reserve and overflow counter. If the reserve
  is exhausted, guarantee AllNotesOff and suppress NoteOn for the rest of the block.
  Even a tiny test buffer must not leave a sounding note without a future release.
- [x] Size a fixed buffer for 256 frames at 32 kHz/240 BPM: at most four ticks per
  block; four patterns can supply 256 notes and 512 NoteOn/NoteOff events, plus
  drums/releases. Document conservative headroom in code. Engine splits large
  requests into bounded blocks.
- [x] Preserve chronological order; releases precede attacks on the same frame.
- [x] Add runtime identity: track, live/clip source and token. Repeated same-pitch
  keys and adjacent same-pitch notes must not release another voice.
- [x] Calculate swung note ends in unwrapped time before modulo. End must be after
  start and no later than the next occurrence of the same note.
- [x] Release relevant notes on stop, song restart and playing-pattern edits,
  including note deletion, pitch/length/grid edits and scene reassignment.

**Checks:** four full patterns; tiny/zero buffers and recovery; NoteOff without
NoteOn; identical pitch; loop boundary/full-loop length; `start=30, length=378,
swing=75`; `playSong` while playing; deleting a held note. Match events by token.

## Task 4. Synth and shared voice pool

**New:** `include/gb/synth.h`, `src/synth.cpp`, `tests/test_synth.cpp`,
`tools/render_synth.cpp`. **Modify:** `components/gb_core/CMakeLists.txt`, `Makefile`.

- [x] Implement four oscillator shapes, ADSR, stable resonant low-pass and glide.
  Compute frequency tables/expensive coefficients at initialization or parameter
  change, not `pow`/`exp` per voice per sample.
- [x] Implement a fixed shared pool initially targeting eight voices, not eight
  per track. Steal quietest, with oldest breaking a tie.
- [x] Bind releases to token/source. Ignore late NoteOff for a stolen voice.
  Clip-AllNotesOff leaves live keys sounding unless explicitly stopped globally.
- [x] Release all voices and briefly smooth forced retriggers. Handle zero
  attack/decay/release without division by zero or stuck sustain.
- [x] Glide from the track's last pitch to the new one; older polyphonic voices
  remain independent. Active parameter changes retain oscillator phase.
- [x] Add `make render-synth` for waveforms, chord, filter and glide WAV examples.

**Checks:** pitch tolerance, ADSR stages/eventual silence, long renders at extreme
settings, no NaN/Inf, voice-stealing selection and track independence. Check
measurable WAV properties rather than bit-identical floating point across platforms.

## Task 5. Engine integration

**Files:** `engine.h/.cpp`, `tests/test_engine.cpp`.

- [x] Process sequencer NoteOn/NoteOff/AllNotesOff at the exact event frame.
- [x] Add live commands, stopped audition and combined drums/synths through the
  existing master limiter. Verify final int16 range.
- [x] Bound commands processed per block so command floods and large pastes cannot
  displace rendering. Stop/releases must not silently disappear.
- [x] Publish transport, active voices and counters atomically for UI/tests.
  Do not expose mutable Project or read sequencer fields directly across cores.

**Checks:** exact-frame synth attack, chord over all drums, live stop/play, late
releases, global stop, playing-pattern edits and limiting at maximum levels.

## Task 6. Live recording and returning edits to UI

**New:** `include/gb/recorder.h`, `src/recorder.cpp`, `tests/test_recorder.cpp`.
**Modify:** `engine.h/.cpp`, queue-message definitions and component CMake.

- [x] Implement off/armed/recording. Alt while stopped arms; Space starts playback
  and recording. Alt while playing starts/finishes recording in the target pattern.
- [x] On NoteOn, retain track, pattern, physical key/token, pitch and unwrapped time.
  Monitor immediately. NoteOff finishes the matching note even after octave changes.
- [x] Quantize start/end to the nearest grid line, ties forward. Calculate duration
  before wrapping into the loop; minimum one tick, maximum one loop. A quick tap
  rounded to one position still produces positive duration.
- [x] Reserve note capacity at NoteOn. When full, retain monitoring, stop accepting
  new recorded notes and notify UI with NOTE FULL.
- [x] Add completed notes to `audioProject` and return exact accepted edits through
  reverse SPSC. UI applies without resending and increments editCount. Playback
  begins at the next traversal of the new start; finishing a short tap in the
  same block must not retrigger its already-monitored live note.
- [x] Lock the recording target against structural edits; finish holds and receive
  acknowledgement before changing it. Drum editing remains available.
- [x] Justify reverse-queue capacity: up to 64 completions plus state acknowledgements
  per pass. Admit a new pass after the previous results are consumed. Audio never
  waits for space; pending results remain in a fixed buffer.
- [x] On stop/record exit, finish holds, return all results and allow save only after
  acknowledgement. One undo restores the pre-pass state.

**Checks:** chords, short notes, loop crossing, long holds, both quantize modes,
full pattern, stop with held keys, late releases, slow UI/full queues, matching
models after acknowledgement and no duplicate monitoring note.

## Task 7. Shared UI and piano roll

**New:** `include/gb/ui.h`, `src/ui.cpp`, `include/gb/instrument_ui.h`,
`src/instrument_ui.cpp`, `tests/test_ui.cpp`.
**Modify:** `drum_ui.h/.cpp`, `tests/test_drum_ui.cpp`, component CMake.

- [x] Move Canvas, keycodes and CommandSink into a common header. Add AppUi owning
  screen, modifiers, shared transport, warnings and edit tracking. Screens must
  not maintain independent play flags or intercept each other's shortcuts.
- [x] Implement the proposed piano mapping, MIDI clipping and physical-key holds.
  Repeated keydown does not add a second hold; release uses stored pitch/track/token
  even after Fn/Shift changes.
- [x] Draw a 240×135 piano roll with track/pattern header, PLAY/REC/ARM, grid, pitch
  window, cursor, selection and playhead. Show loop-crossing notes as two segments.
  Pitch scrolls with the cursor; the footer shows pitch/length/velocity.
- [x] Implement the control table and note-count/full indicators. With quantize
  off, cursor movement retains the editor grid without shifting recorded notes.
- [x] Keep one clipboard and one undo pattern locally, not Project copies in history.
  Paste/undo uses task 1 transactions; UI applies the result as a whole.
- [x] Add synth parameters and non-overlapping SD/load/record/hidden-note warnings.
  Test new display scenarios through a test Canvas.

**Checks:** shortcut priority, both octaves/chords, octave changes while holding,
screen/track changes, cursor bounds, overlapping notes, copy/paste/undo, recording
pass undo, original DrumUi scenarios and display bounds.

## Task 8. Firmware and diagnostic console

**Files:** `firmware/main/main.cpp`, `hardware.h/.cpp` as needed;
`tools/device_test.py`, `tools/test_device_test.py`.

- [x] Wire AppUi and reverse recording queue. Accept results before saving; do not
  start SD work before recorded-note synchronization finishes.
- [x] Share editCount/save requests across screens. Retain three-second stopped
  autosave and protection for unsupported/corrupt files.
- [x] Keep `K` as a complete press for old tests. Add separate keydown/keyup,
  Alt/Ctrl modifiers, instrument state, note lists and synth parameters over USB.
- [x] Add a separate test-project path and retain mode across reset. Enter/exit via
  console while stopped. Do not use default.gbp or its backup as test fixtures;
  both device suites use isolation.
- [x] Add active/peak voices, event overflows, peak load, free heap and stack margins.
  Sample at a consistent checkpoint and print outside the audio task. Allow reset
  of the measurement window.
- [x] Update ESP-IDF build; check memory map and large stack frames. Check application
  size against `0x180000` and heap remaining after the 240×135 sprite.

**Checks:** `tools/idf.sh firmware build`, `tools/idf.sh firmware size`, console
protocol tests and old drum scenarios under AppUi. Do not use `idf.py flash`.

## Task 9. Automatic Cardputer validation and results

**New:** `tools/device_synth_test.py`, `tools/test_device_synth_test.py`.
**Modify:** specification results section.

- [x] Create a repeatable scenario through normal keydown/keyup: live chord,
  recording, release, loop playback, edit/undo, track change and stop.
- [x] Run in an isolated temporary project; preserve/restore default.gbp and backup,
  including error exits. Never leave demo notes in place of the user's project.
- [x] Measure silence, live synth, recorded loop and post-release/stop silence via
  microphone. Also read note/voice state: a microphone peak alone cannot prove
  correct recording or absence of stuck notes.
- [x] Check notes, synth parameters and quantize after hardware reset; separately
  load v1 without LOAD ERR or autosave lock.
- [x] Measure eight drums plus the eight-voice target, resonance and long release,
  then exceed polyphony. Check 40/240 BPM, dense four-track events, screen updates,
  live playing and saving; include steady loops and bursts of new notes.
- [x] Eight-voice acceptance: peak audio work at most 50% of the block window, no
  new I2S overflows, exchange errors or lost events for 60 seconds after warmup.
  If it fails, optimize DSP, then measure a smaller pool and document the result.
  Do not change sample rate without a separate decision.
- [x] Record actual CPU, memory, stacks, voice count and microphone levels. The
  author assesses timbre/physical comfort; automation validates timing, state,
  persistence and signal presence.

Final commands:

```bash
make test
python3 -m unittest discover -s tools -p 'test_*.py'
make render-synth
tools/idf.sh firmware build
tools/idf.sh firmware size
python3 tools/slot.py install-app firmware/build/groovebox.bin
python3 tools/device_test.py
python3 tools/device_synth_test.py
```

Before either hardware suite, implement user-project preservation: the baseline
drum test modified default.gbp. Never overwrite Launcher at `0x10000` or the app
at `0x170000`; never format the card. Current audio tests also enforce the author's
subsequently requested 5% ceiling.

## Execution order and completion criteria

Model → compatibility/memory → event safety → DSP → engine → recording → UI →
firmware → device. Each task ends with relevant checks and a coherent change;
format changes ship only with migration. Run additional tests for changed behavior.

The stage is complete when the device can sequence drums, record chords over them,
edit a note, stop, reboot and restore the same part/settings, while stress checks
confirm the chosen voice count without lost events or stuck notes.

## Implementation outcome

- 156 C++ tests with ASan/UBSan and 46 Python tests passed; both hardware programs
  reported `0 failures`. Each stress run lasted 60 seconds after warmup.
- Measurements selected **five shared voices** at 32 kHz: peaks 46.2% at 40 BPM and
  48.4% at 240 BPM with all eight drums, including live playing and saving after
  stop. Eight- and six-voice trial builds exceeded the 50% limit.
- Optimizations: shared sine table, drum-coefficient and note-schedule caches,
  linear release/attack passes, parameter-time coefficient calculation and DSP tuning.
- Commands retain compact union payloads; instrument patterns transfer through
  transactions. Recording results use a separate reverse queue.
- Verified SD v1 migration, rebooted notes/settings, edit/parameter undo, no stuck
  notes or new audio overflows, and unchanged user-project/backup checksums.
- [User guide](../../stage-3.md); measurements and limits are in the specification's
  [stage 3 results](../specs/2026-11-09-groovebox-design.md#stage-3-results).
