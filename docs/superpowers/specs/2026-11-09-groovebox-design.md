# Cardputer Groovebox: first-version design

Date: 2026-11-09. Original status: under review by the project author.

**Historical specification:** this combines initial proposals with subsequent stage
results. Proposed effects and old controls are not a current feature list. Consult
the [current guides](../../README.md) and [visual review](../../visual-refresh.md).

## Goal

Standalone firmware for M5Stack Cardputer ADV that can create a complete track on
the device: sequence drums, record synths and samples, arrange scenes into a song
and mix it.

The workflow is inspired by Ableton's Drum Rack, MIDI clips and Session View.
It is an independent product and does not use GLIDE code, sounds or branding.

## First-version scope

Planned: step sequencer, instrument tracks, scenes, song chain, mixer with effects,
SD samples, microphone recording and slicing.

Excluded: on-device WAV export (the architecture allows it later), a free timeline,
real-time recording of full audio tracks, parameter automation and per-track EQ.

## Hardware and constraints

- ESP32-S3, dual core at 240 MHz, 8 MB flash, no PSRAM (confirmed by chip readout).
- 240×135 display and keyboard through TCA8418.
- Codec with microphone and speaker; presumed mono, to be checked in prototype 1.
- A 200 GB microSD card, initially presumed exFAT.
- Existing Launcher at `0x10000`, size `0x150000`, and GLIDE at `0x170000`, size
  `0xF0000`. Free space: `0x260000..0x800000`, about 5.6 MB.

## Stack and modules

ESP-IDF, C++ and M5GFX. Custom I2S/codec drivers support simultaneous input/output.
The engine and sequencer are hardware-independent and build on a PC.

| Module | Responsibility | Dependencies |
|---|---|---|
| `model` | Fixed-size project data | None |
| `sequencer` | Patterns, scenes, song and sample timing | `model` |
| `engine` | Synth voices, drums, sampler, mixer and effects | `model` |
| `hal` | Codec/I2S, keyboard, display, SD and flash | ESP-IDF |
| `storage` | Project saving and sample library | `model`, `hal` |
| `ui` | Screens and controls | `model`, `hal`, command queue |

No memory allocation during playback.

## Audio and timing

- Pin the audio task to one core; UI and SD run on the other.
- UI sends commands through a lock-free SPSC queue.
- The sequencer counts samples inside the audio stream.
- Target 32 kHz, 16-bit mono; fallback 22.05 kHz if prototype 2 lacks CPU headroom.

## Data model

- 96 ticks per quarter note; 4/4 bars of 384 ticks; 40–240 BPM.
- Per-pattern grid: 1/4 (96 ticks), 1/8 (48), 1/16 (24), 1/32 (12), 1/8T (32),
  1/16T (16). Default 1/16.
- Global swing 50–75%, with 50% straight. Delay alternate steps on 1/8, 1/16 and
  1/32 grids; do not affect 1/4 or triplets. Allow track exclusion from swing.
- Eight-sound drum rack, four synth tracks and a separate SMP track; 16 patterns
  per track, 16 scenes, up to 64 song entries.
- Per-pattern length: 4, 8 or 16 steps.
- Drum patterns hold 16 steps for each of eight sounds, with enabled state and velocity.
- Instrument patterns hold up to 64 notes: pitch, start/length in ticks and velocity.
  Grid changes affect editing/quantize steps without moving existing notes.
- Drum-grid changes remap by time: retain a step if it lands on the new grid,
  otherwise remove it, with undo available.
- Scenes contain a pattern number or empty value for each of six tracks.
- Pattern duration is step count × grid ticks. Scene duration is its longest
  pattern; shorter patterns loop inside it.
- A song entry contains a scene and a repeat count of its duration.
- Scene launch takes effect on the next bar boundary (384 ticks).

## Sound

**Synthesized drums:** kick, snare, clap, closed hat, open hat, tom, rim and perc.
Parameters: tuning, decay, tone and level. Each sound is monophonic and retriggers
on a new hit. Samples use separate SMP pads.

**Synth:** a shared pool across four tracks, ultimately five voices without samples
and four while samples play. Each voice has sine/triangle/saw/square, ADSR, resonant
low-pass and glide. Steal the quietest voice when full.

**Sampler:** 32 slices, eight pads with per-pattern assignments across 16 patterns,
four simultaneous one-shot voices, manual bounds, waveform zoom and equal chopping
into 4/8/16/32 parts.

**Planned mixer:** drum rack, four synths and SMP, each with level, mute, solo,
filter, drive and sends. Shared send delay/reverb, kick sidechain on selected tracks
and a master limiter. Panning was excluded because the output was presumed mono.
Effects described here were not implemented in the stage 4 results below.

## Samples and recording

- Source WAVs and microphone takes live in `/groovebox/samples/` on SD. Keep sources
  unchanged; record microphone WAV as 16-bit mono at 32 kHz.
- Resample imported WAVs to the working format.
- Copy used slices into a 4 MB flash partition (about 65 seconds at 32 kHz) when
  opening a project and read them there during playback. Write flash only while stopped.
- Record microphone input to SD during playback if prototype 1 confirms it works.
- Set slice boundaries manually or divide a selection into equal parts.

## Proposed screens and controls

The original design used Tab to cycle screens, Fn+1..6 for direct access, Space for
transport, Alt for recording and a held-Fn shortcut panel. The panel was later
replaced by the Opt menu. Plain −/+ changes master by 5%, Ctrl by 1%; Fn+−/+ changes
tempo in Drums.

1. **Drums:** 8×16 grid; 1..8 chooses sound; q..i / a..k toggles steps; Shift accents;
   Fn+L changes length, Fn+G grid, Fn+W swing. Grid/swing also work in Synth.
2. **Instrument:** compact piano roll; z.., and q..i are white keys, rows above are
   black keys. Fn+−/+ changes octave. Live recording with optional quantize and
   cursor-based note editing.
3. **Scenes:** rows are scenes; columns hold track pattern numbers.
4. **Song:** scene × repeats list, playable from any entry.
5. **Mixer:** strips, proposed sends/sidechain and limiter.
6. **Sampler:** recording, waveform, slicing and assignment to sounds/keys.

Also planned: pattern copy/paste, last-action undo, autosave on stop and sound
preview on selection.

## Error handling

- Without SD, instruments work but saving is visibly unavailable; samples and
  microphone recording are disabled.
- Write a temporary project file and replace the old one only after success.
- Version and checksum failures show a clear message and leave the original intact.
- If flash is full, do not load slices that cannot fit; identify them in the UI.
- Count audio overflows and expose them in a debug indicator.

## Testing and implementation order

Host tests cover sequencer timing, pattern length, scenes, song chains and project
save/load including corrupt files. The engine renders reference WAVs for comparison.
Device checks run unattended: cable installation, a known speaker signal measured
by the microphone, load measurements and USB results. Human feedback assesses sound
and playing comfort.

Each stage ends in runnable firmware:

1. Prototypes: simultaneous input/output, voice capacity at 32 kHz, Launcher install
   with a sample partition.
2. Drum synthesis, grid, transport and saving.
3. Synth tracks, live recording and step editing.
4. Scenes and Song.
5. Mixer and effects.
6. SD samples.
7. Microphone recording and slicing.

## Risks and fallbacks

- If prototype 1 fails, permit microphone recording only while stopped.
- If prototype 2 lacks headroom, use 22.05 kHz or fewer voices.
- If prototype 3 fails, create the sample partition through cable installation;
  Launcher installation becomes an optional path without samples.
- If exFAT cannot mount, add FatFs support or consider a user-managed FAT32 card.
  The application itself must never format the card.

## Prototype results

Checked on the author's device on 2026-11-09–10.

- Sample rate: 32 kHz. The last probe's simple test voice used 5.4% for eight voices
  and 10.6% for sixteen; real synth voices would cost more.
- Simultaneous playback, microphone capture and SD writing worked. The final run
  wrote 960,000/960,000 bytes with zero lost blocks or I2S overflows during 15 seconds
  after a 256 ms settling period. Maximum SD write was 10 ms. Across all runs, the
  worst write was 47 ms, but that run's counter did not yet separate recording
  overflows from the final pause, so behavior at that latency remained unconfirmed.
- The author's filesystem mounted with an exFAT-enabled FatFs copy, but its type
  was not identified then, leaving the exFAT question open.
- Microphone left/right channels matched; use left, gain register `0x14 = 0x18`.
  Recorded speech was audible with ×8 digital gain. Speaker tone dominated speech
  during recording, requiring a separate over-playback check before stage 7.
- Speaker tones at 1, 2 and 3 kHz appeared in the microphone 70–84 dB above silence.
  The test did not distinguish acoustic from internal electrical coupling.
- A 4 MB sample partition was created by cable; writing and memory-mapped reading
  worked. Launcher creation was checked only in its source, pending a real install.

## Stage 2 results

Two consecutive automatic runs on the author's device passed on 2026-11-10.

- The card was identified as FAT32, resolving the exFAT question for this card.
- Snare playback used 9.4–10.6% of one audio core.
- Microphone peak was about 13,900 during playback versus 96–160 in silence,
  returning to 126–129 after stop.
- No audio overflows during three seconds of playback or saving.
- Steps, grid changes, undo, swing and saving across reboot worked.
- Display detected as 240×135; keyboard controller initialized without errors.
- Edits without playback autosaved after three seconds and survived reboot.
- Physical keys, image orientation and subjective sound awaited the author's review.

## Stage 3 results

Implemented and checked on 2026-11-10. Cable installation wrote the `groovebox`
slot without overwriting Launcher or GLIDE.

- Four synth tracks, 16 patterns each, 64 notes per pattern and **five shared voices**
  at 32 kHz. Both the eight-voice target and intermediate six-voice pool exceeded
  the accepted 50% core budget in trial builds.
- Sine/triangle/saw/square, ADSR, resonant low-pass, glide, live chords, quantized or
  free recording, piano roll, copy/paste and undo. Runtime tokens independently
  release repeated pitches and live/sequencer notes.
- 156 core tests with ASan/UBSan and 46 Python tests passed. `device_synth_test.py`
  and `device_test.py` both reported `0 failures` on the final build.
- Stress scenario: all eight drums, four simultaneous 64-note patterns, loop-length
  notes, maximum resonance/release/glide and intentional excess polyphony. Each
  tempo ran for 60 seconds after warmup with display updates and USB polling.
  Audio-task peaks: **46.2% at 40 BPM**, **48.4% at 240 BPM**. Extra live playing,
  stopping and saving did not raise these peaks.
- No new TX/RX I2S overflows, exchange errors or event-buffer overflows in either
  run, including live playing/saving. Measurements used increments after warmup;
  initial I2S counter values were not counted as run failures.
- All voices released after stop, allowing up to four seconds of release. A tiny
  test buffer used AllNotesOff on overflow and suppressed attacks for the rest of
  the block. Normal capacity was 544 events; render chunks were at most 256 frames.
- Shared drum/synth sine table; cached drum coefficients and note schedules;
  separate linear release/attack passes. Schedules update on edits/grid/swing.
  No allocation during rendering.
- Free heap: 112,808 bytes under load, 112,632 after saving. Minimum observed stack
  headroom: audio 7,056 and UI 4,380 bytes out of 8,192. Static `.data + .bss`:
  156,280 bytes; application 667,184 bytes in the `0x180000` slot.
- Microphone peaks: live chord 2,187, recorded loop 2,696, silence 60. Snare recheck:
  13,955 versus 58, then 72 after stop; audio load 8.2% in that run.
- Notes, synth parameters and quantize survived reboot. A real old-revision v1 file
  loaded from SD without LOAD ERR and saved as v2 after editing. Temporary/primary
  validation reused shared scratch instead of a fourth Project copy.
- Both tests used a separate project and retained test mode across reset, then
  restored the regular project. `default.gbp` and `.bak` checksums matched before
  and after each test; the card was not formatted.
- Physical comfort and subjective timbre remained for the author to assess.
  See [synth controls](../../stage-3.md); `make render-synth` generates audio examples.

## Stage 4 results and author-requested additions

Checked and installed on the author's Cardputer on 2026-11-10.

- Five workspaces via Tab / Fn+1..5: Drums, Synth, Scenes, Song and Mixer.
- Fn+Backspace clears current content with undo while retaining grid, length and
  sound. Clearing during recording and restoring finalized notes were checked.
- Piano roll immediately displays held keys and follows pitch. FIT includes all
  notes; Fn+F selects precise zoom; opening a pattern focuses its register.
- Per-track Off/Up/Down/UpDown arpeggiators with six grids including triplets,
  1–3 octaves and gate. Live/clip chords are supported; the original chord is
  stored. Stop, clear and mode changes release notes.
- Sixteen scenes assign independent patterns across the then-five tracks, including
  empty cells. Live launches wait for the global bar boundary. Up to 64 song entries,
  1–255 repeats, insert/delete/edit/undo, play from selection and natural completion.
- Editor pattern selection changes the selected scene rather than always scene 0.
  Recording over Song targets the playing scene and ends at the next entry.
  Holds use continuous transport ticks.
- MAIN after the limiter plus DRUM/T1..T4, levels 0–100%, mute, solo and undo.
  PCM changes, silence at master zero and strip independence were checked.
- File v3 stores mixer/arpeggiators and reads v1/v2 with defaults for new fields.
  Scene/song/mixer/arp restoration and piano-roll focus were checked after reset.
- 172 C++ tests with ASan/UBSan and 49 Python tests passed. The new device scenario,
  arpeggiator stress and both previous regressions reported `0 failures`.
- Each stress scenario ran 60 seconds after warmup: four arpeggiators + eight drums
  peaked at 47.0%; four dense 64-note patterns + eight drums peaked at 47.1% at
  40 BPM and 49.4% at 240 BPM. Five voices at 32 kHz; no new I2S/event overflows or
  exchange errors, including live playing/saving.
- Free heap: 110,376 bytes under load, 110,200 after saving. Minimum stack headroom:
  audio 7,040, UI 4,348 bytes. Static data+bss: 158,712; application: 680,896 bytes
  in the `0x180000` slot.
- At the author's request, synthetic hardware checks use master 5% and an independent
  5% post-limiter ceiling, including fixture loads/reboots. PCM peaks must be at
  most 1557 and I2S exchange successful. Microphone results are diagnostic because
  the signal may not sufficiently exceed background noise at this volume.
- All scenarios used an isolated project; regular-file and backup checksums matched.
  Launcher/GLIDE were not overwritten and the card was not formatted. The old probe
  also built with a 5% limit but was not installed.
- See [controls and reproducible commands](../../stage-4.md). Timbre and physical
  playing comfort remained for the author; planned stage 5 effects were not added.
