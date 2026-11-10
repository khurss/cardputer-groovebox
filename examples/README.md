# Original synthwave, techno and drum and bass demo projects

Seven complete songs for Cardputer Groovebox, in GBP v5 format. Their melodies and
arrangements were generated for this project; no external recordings or sample
files are required. [Project overview](../README.md) · [Documentation](../docs/README.md)

| Project | Character | Tempo / key | Duration |
|---|---|---|---|
| [Neon Drive](projects/neon-drive.gbp) | Driving retrowave with pulsing bass, bright arpeggios and lead | 108 BPM, A minor | ≈3:02 |
| [Sunset Arcade](projects/sunset-arcade.gbp) | Softer, slower synthwave with gentle swing | 92 BPM, D minor | ≈3:34 |
| [Midnight Run](projects/midnight-run.gbp) | Faster night-drive synthwave with a sharper bass | 122 BPM, E minor | ≈2:41 |
| [Circuit Pressure](projects/circuit-pressure.gbp) | Acid-inspired techno: resonant saw bass, offbeat stabs and bright hooks | 132 BPM, A minor | ≈3:24 |
| [Concrete Pulse](projects/concrete-pulse.gbp) | Industrial-inspired techno: four-on-the-floor kick, toms, rims and short stabs | 140 BPM, F minor | ≈3:12 |
| [Night Transit](projects/night-transit.gbp) | Dark rolling drum and bass: broken kicks, snare ghosts and sliding saw bass | 174 BPM, D minor | ≈3:19 |
| [Liquid Signal](projects/liquid-signal.gbp) | Liquid drum and bass: sine sub-bass, warm two-note pads and melodic answers | 170 BPM, G minor | ≈3:23 |

Each song has **29 Song entries and 16 scenes**. The synthwave songs have 82 bars,
the techno songs 112 bars, and the drum and bass songs 144 bars. All include an
intro, groove, build, chorus/drop, breakdown, final chorus/drop, outro and a tonic
ending. T1 is bass, T2 two-voice pad/stabs, T3 notated arpeggio/hook and T4 lead or
melodic response. Drums and synths are generated on the device; the drum and bass
beats are programmed with synthesized drums, not sampled break recordings.

## Load a demo on the device

![Project manager with the Add demo songs action](../docs/images/projects.png)

1. Stop playback. Open **Opt → SCREENS → Projects → Add demo songs**.
2. Select a demo name and confirm opening. Your current project saves first.
3. After loading, press **Fn+4 → Enter** to play the full song from the first row.
4. `Space` stops playback. To make your own version, use **Save copy as…**, then
   open the copy before editing.

Alternatively, copy files from `projects/` into `/groovebox/projects/` on microSD,
then reopen Projects. Menu installation **does not overwrite** matching filenames.
To restore an original demo, rename your version or copy the source `.gbp` from
your computer.

All demos start with **MAIN at 5%**. Adjust volume with `+` / `−` after loading.
Checks and audio previews also use 5% without loudness normalization.

## Synthwave arrangement map

![Song view showing scene entries and their repeat counts](../docs/images/song.png)

*The Song interface used to arrange these projects; the screenshot shows an example state.*

| Song rows | Scenes | Bars | Section |
|---|---|---|---|
| 01–04 | 01–04 | 8 | Intro: pad and sparse arpeggio |
| 05–08 | 05–08 | 16 | Verse: bass and main rhythm enter |
| 09–12 | 05–08 | 8 | Build: harmony changes twice as often |
| 13–16 | 09–12 | 16 | Chorus: lead and fuller drums |
| 17–20 | 13–16 | 8 | Breakdown: sparse rhythm and long notes |
| 21–24 | 09–12 | 16 | Final chorus |
| 25–28 | 01–04 | 8 | Outro |
| 29 | 13 | 2 | Return to the tonic, then stop |

Each scene is one bar; Song entries supply the repeats. Replace a track's pattern,
reorder song rows or change timbres in Sound editor. Pattern edits affect every
scene that uses that pattern. See [arrangement and mixer](../docs/stage-4.md).

## Techno and drum and bass arrangement map

The four new songs use these sections, with different notes, sound settings and
rhythms. Intro and breakdown omit the bass; drops bring all four synth tracks in.
Techno keeps a kick on each beat. Drum and bass uses syncopated kick placements,
backbeats on beats two/four and quieter snare ghost notes. Every fourth drum
variation adds a fill.

| Song rows | Scenes | Techno bars | Drum and bass bars | Section |
|---|---|---|---|---|
| 01–04 | 01–04 | 16 | 16 | Intro |
| 05–08 | 05–08 | 16 | 16 | Groove |
| 09–12 | 05–08 | 8 | 16 | Build |
| 13–16 | 09–12 | 24 | 32 | First drop |
| 17–20 | 13–16 | 8 | 16 | Breakdown |
| 21–24 | 09–12 | 24 | 32 | Final drop |
| 25–28 | 01–04 | 12 | 12 | Outro |
| 29 | 13 | 4 | 4 | Tonic ending, then stop |

Circuit Pressure uses an accented, resonant bass phrase; Concrete Pulse uses a
shorter square bass phrase with tom/rim syncopation. Night Transit has a denser
saw bass and sharper synth responses. Liquid Signal pairs sine sub-bass with longer
pads and a more melodic hook. The sound engine's existing envelopes, filter and
glide settings supply the timbres; these files do not require extra effects.

## Listen and regenerate

Twenty-second chorus previews:
[Neon Drive WAV](previews/neon-drive.wav) ·
[Sunset Arcade WAV](previews/sunset-arcade.wav) ·
[Midnight Run WAV](previews/midnight-run.wav) ·
[Circuit Pressure WAV](previews/circuit-pressure.wav) ·
[Concrete Pulse WAV](previews/concrete-pulse.wav) ·
[Night Transit WAV](previews/night-transit.wav) ·
[Liquid Signal WAV](previews/liquid-signal.wav).

```bash
make demos         # regenerate all seven projects in the current GBP v6 format
make render-demos  # full WAVs in build/demo-audio; excerpts in build/demo-previews
```

Generation is deterministic. Full WAV renders are checked for song completion,
nonzero signal, the 5% output ceiling and event-queue overflows. New previews start
at the first drop (Song row 13) and have short edge fades without normalization.
To render only the four dance demos into the published preview directory:

```bash
make build/render_demos
./build/render_demos build/demo-audio examples/previews 3
```
