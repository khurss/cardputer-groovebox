# Original retrowave and synthwave demo projects

Three complete songs for Cardputer Groovebox, in GBP v5 format. Their melodies and
arrangements were generated for this project; no external recordings or sample
files are required. [Project overview](../README.md) · [Documentation](../docs/README.md)

| Project | Character | Tempo / key | Duration |
|---|---|---|---|
| [Neon Drive](projects/neon-drive.gbp) | Driving retrowave with pulsing bass, bright arpeggios and lead | 108 BPM, A minor | ≈3:02 |
| [Sunset Arcade](projects/sunset-arcade.gbp) | Softer, slower synthwave with gentle swing | 92 BPM, D minor | ≈3:34 |
| [Midnight Run](projects/midnight-run.gbp) | Faster night-drive synthwave with a sharper bass | 122 BPM, E minor | ≈2:41 |

Each song has **82 bars, 29 Song entries and 16 scenes**, spanning intro, verse,
build, chorus, breakdown, final chorus, outro and a final tonic cadence. Synth
tracks are T1 bass, T2 two-voice pad, T3 notated arpeggio and T4 lead. Drums and
synths are generated on the device.

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

## Arrangement map

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

## Listen and regenerate

Twenty-second chorus previews:
[Neon Drive WAV](previews/neon-drive.wav) ·
[Sunset Arcade WAV](previews/sunset-arcade.wav) ·
[Midnight Run WAV](previews/midnight-run.wav).

```bash
make demos         # regenerate the three .gbp files in examples/projects
make render-demos  # full WAVs in build/demo-audio, with a 5% ceiling
```

Generation is deterministic. Full WAV renders are checked for song completion,
nonzero signal, the output ceiling and event-queue overflows.
