# Interface screenshots

These PNGs render actual application UI drawing commands using the M5GFX device
font and RGB332 color conversion. They are example interface states, not camera
captures. Each individual screen is **480×270**, a 2× rendering of the native
240×135 display; galleries combine multiple screens with gutters.

| File | Screen |
|---|---|
| [interface.png](interface.png) | Performance, Drums, Piano roll, Sound editor, Mixer, Song |
| [sampler-projects.png](sampler-projects.png) | Scenes, Sampler Files/Cut/Pads, Menu, Projects |
| [performance.png](performance.png) | Synth pitch trails and keyboard minimap |
| [drums.png](drums.png) | Eight-lane drum step sequencer |
| [piano-roll.png](piano-roll.png) | Synth note editing |
| [sound-editor.png](sound-editor.png) | Synth parameter cards |
| [mixer.png](mixer.png) | Master and six track faders |
| [song.png](song.png) | Song entries and repeats |
| [scenes.png](scenes.png) | Six-track pattern assignments |
| [sampler-files.png](sampler-files.png) | WAV file browser |
| [sampler-cut.png](sampler-cut.png) | Waveform selection and equal-chop preview |
| [sampler-pads.png](sampler-pads.png) | Pad slots and step sequencer |
| [menu.png](menu.png) | Screen selection and menu tabs |
| [projects.png](projects.png) | Save, create, copy and demo actions |
| [song-follow-browse.png](song-follow-browse.png) | Playing row pinned above a different edit selection |
| [quick-start.png](quick-start.png) | First page of the built-in tutorial |

Additional audio views: [gallery](audio-visuals.png), [Equalizer FFT](equalizer-fft.png),
[spectrum](visual-spectrum.png), [waveform](visual-wave.png),
[waterfall](visual-waterfall.png), [orbit](visual-orbit.png) and
[settings](visual-settings.png). Their audio comes from the demo engine at 5%.

Regenerate all screenshots with `make render-interface` after a firmware build has
provided the M5GFX font. The renderer uses only Python's standard library.

[Back to documentation](../README.md)
