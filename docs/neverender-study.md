# Neverender instrumental study

The startup project is a 16-bar reconstruction attempt at 117 BPM, not an exact
transcription or a complete cover. It replaces the earlier invented four-chord
Afterglow demo. No recording or stems from the song are included. The original
vocal performance is absent, and musical timing and voicings need listening-based
verification.

The study uses an estimated eight-bar progression: F#m, E, Dmaj7, C#m7, Bm7, C#m7,
Dmaj7, E. The pad voicings omit some roots because the bass supplies them. The
arrangement is compressed into pad intro, pulse, full groove, and variation.
Two independently detuned, panned wavetable tracks supply stereo pad layers; FM
supplies short chord attacks and sparse high keys. Sub and mid bass are separate.
Editable volume curves provide the pumping. The kick, clap/snare, and hats are
newly generated for this study, rather than the generic factory sampler presets.

## Production references

[Justice's Mix with the Masters session](https://mixwiththemasters.com/videos/justice-justice-neverender)
describes the original Ableton project, the PPG Wave, resampling, resynthesis,
spectral transformation, drum sound design, and mix processing. Only the public
summary was available for this implementation; the full session and stems were
not used.

[Noise Chest's independent cover breakdown](https://noisechest.com/articles/justice-tame-impala-neverender)
demonstrates layered pads, pumping, additional bass layers, high keys, and vocal
processing. It is a useful reconstruction reference, not proof of the original
session's exact settings.

The harmonic estimate follows a
[public chord chart](https://chordify.net/chords/justice-starring-tame-impala-songs/neverender-chords).
Its automatic transcription is not treated as authoritative for exact voicings,
rhythm, or tempo.

## What Opentone still needs

These findings come from reading the current playback, model, and built-in DSP
code. They are capability limits, not measured explanations of how the mix sounds.

1. **Device-parameter automation.** Project envelopes currently cover volume,
   pan, mute, and sends. They do not store or play back synth cutoff, wavetable
   position, resonance, or effect-parameter envelopes. That blocks editable filter
   sweeps and detailed timbre changes within a part. MIDI mapping changes a live
   parameter but does not provide an arrangement automation lane for it.
2. **Richer built-in synth tone and stereo.** Wavetable has three procedural banks
   with four frames each; it cannot import a PPG wavetable or reproduce that
   instrument's signal path. It has an amplitude ADSR but no dedicated filter
   envelope, pulse-width modulation, or modulation matrix. Synth output is mono
   copied to both channels, and there is no built-in chorus/doubler. The study's
   panned layers approximate width but do not reproduce those missing behaviors.
3. **A production sound library and deeper sampling.** Generated one-shots are
   not a curated recorded drum library. Importing better samples already works.
   The built-in sampler has source range, root note, and gain, but no multisample
   zones, velocity layers, or sample-loop/amplitude-envelope controls. These make
   realistic articulation and sustained sampled instruments difficult.
4. **Internal resampling and audio transformation.** There is no user-facing
   freeze/flatten, bounce-to-audio, or internal-bus resampling workflow. Audio uses
   resampling for speed/pitch changes rather than independent time stretching and
   formant-preserving pitch shifts. Fine audio chopping is possible, but repeated
   synth-to-audio production and spectral transformations require external tools.
5. **Vocal production and source performances.** A sung/doubled vocal performance
   is essential source material for a full recreation. Opentone records and edits
   audio, but has no built-in pitch/formant editor, de-esser, or dedicated vocal
   doubler. Those processes need suitable recordings and external tools/plugins.

MIDI sequencing, multiple tracks, audio import, saturation, EQ, compression,
reverb, delay, returns, and volume automation already cover the basic composition
and mix. Pumping can be drawn with volume automation. A PPG-style instrument and
chorus can also be hosted as VST3 (and AU on macOS); the built-in sound palette is
not the limit of what the host can play.

The most useful DAW improvement for this project is device-parameter automation,
followed by filter modulation and stereo/chorus processing. Better drum assets and
a verified transcription are production inputs, not reasons to rebuild the engine.

## Validation

The project generator validates the complete model before opening it. Generated
drums check for finite, nonzero audio before encoding. This change receives a
release application build and launch; no automated audio tests, listening-based
A/B comparison, loudness measurements, or original-session verification were run.
