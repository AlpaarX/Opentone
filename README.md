# Opentone

The compact transport includes Link network tempo/phase synchronization (right-click
Link for shared Start/Stop), tap tempo, meter and loop-position editing, punch recording,
MIDI overdub, mixer automation recording, follow playback, draw mode, major/minor scale
highlighting, and a simple sixteenth-note swing amount. Live groove templates and
Session launching are not reproduced. Cue/metronome volume currently shares the main
output pair. MIDI/Key mapping mode overlays available controls: click a control, then
move an absolute MIDI CC, play a MIDI note, or press a key. Right-click a mapped control
to remove its assignment. External plug-in device buttons offer a parameter picker.
Mappings, cue level, scale and swing settings are saved in native projects (format 13);
older formats remain readable. Existing projects keep their saved send levels; new
tracks start with their return sends at silence. The arrangement ruler stays fixed while
tracks scroll, and the grid's shaded sections adapt to zoom.


A native desktop audio workstation built with C++17 and JUCE. In active development.

Features include audio and MIDI tracks, microphone and MIDI recording, a piano
roll, built-in instruments and effects, AU/VST3 effects and instruments, a mixer, arrangement
looping, and volume/pan automation. Master is the same output track in Arrangement and Mixer; its volume controls the
combined track and return mix. Cue controls library previews and the metronome.
Master supports its own effect chain.

Audio import supports WAV, AIFF, FLAC and MP3. VST2 is not enabled.

Opentone starts with a blank project.

## Optional demo

**House 4 — Keijiro Takahashi** is an eight-bar [public Ableton Live MIDI sketch](https://github.com/keijiro/music/tree/master/Sketches/House%204%20Project).
It uses three native Operator synth instances for chords, bass and kick, nested
drum racks, Note Length, Dynamic Tube, Saturator and a kick-sidechained compressor.
The local MIDI-only adaptation omits two recorded Vengeance drum-loop tracks
whose samples are absent from the public repository, and omits unused Session clips.
The demo has no recorded audio clips, external VST/AU devices, or rendered stems.
Press **Space** to play the loop. MIDI notes and native device controls remain editable.
Opentone implements its own DSP; this is not a verified sound-identical Live emulation.

The author's repository specifies **private use only**. The set is installed
outside this repository and must not be bundled with Opentone releases.
Run `python3 tools/install_demo.py` to download the reviewed set and prepare the
MIDI-only adaptation. It installs under the JUCE user application data directory
(`~/.config` on Linux, `~/Library` on macOS): `Opentone/Demo/House 4/House 4.als`.
The installed local adaptation repairs two kick receive-note mappings to 36/60;
the downloaded original is retained alongside it as `House 4.original.als`.
Open the installed set manually to try it. **File → New** creates a blank project.
Save a copy to keep edits.

## Build and run

Requires CMake 3.22+, Ninja, a C++17 compiler, and internet access for the first
configure. On macOS, install Xcode or its command-line tools.

```sh
cmake --preset release
cmake --build build/release --target Opentone --parallel 2
```

Launch on macOS:

```sh
open build/release/Opentone_artefacts/Release/Opentone.app
```

The macOS build signs the app locally so it can request microphone permission.
Choose audio and MIDI inputs under **File → Settings → Audio**.

## Controls

- **Cmd/Ctrl+T / Shift+T:** add audio / MIDI tracks.
- **Tab:** switch between Arrangement and Mixer.
- **Double-click a MIDI clip header / `~`:** open the piano roll.
- **Cmd/Ctrl+L:** loop the selected region; drag the loop brace to adjust it.
- **Record:** arm tracks beside Solo, then press the transport Record button.
- **Automation:** use the button above tracks; click to add points and drag to select or move them.
- **Cmd/Ctrl+C, X, V, D / Delete:** copy, cut, paste, duplicate or delete automation points.
- **Double-click volume or pan:** reset to 0 dB or center.

Select Master to add effects to its Device View. Device power controls are green
when enabled. UI colors are editable under **Settings → Appearance**.

Recording currently captures MIDI notes and velocity; controller automation and
pedals are not recorded. MIDI takes stop at 512 notes, and arrangement looping
pauses during recording. New saves use project format 5; versions 1–4 remain readable.

## Project

See [architecture](docs/architecture.md) for the source layout and [AGENTS.md](AGENTS.md)
for the development workflow.

Licensed under [AGPL-3.0](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md)
for JUCE and other bundled components.

## Built-in devices

- **FM Synth:** four operators, eight routings, per-operator waves/ratio/ADSR,
  feedback and filtering; 16 voices with live pitch bend and sustain.
- **Wavetable Synth:** three banks with four morphable frames each, scan modulation,
  dual detuned oscillators, ADSR and filtering; 16 voices with pitch bend and sustain.
- **Operator:** four-operator synthesis with per-operator fixed frequency, phase and
  velocity response, plus per-voice filter and pitch envelopes; 20 factory presets.
  A/B/C/D select oscillator controls; A+/B+/C+/D+ select tuning controls.
- **Dynamic Tube:** three saturation curves, drive, output, bias, envelope follower,
  attack/release, tone and dry/wet with 2× oversampling.
- **Note Length:** timed or tempo-synced MIDI note gates before plug-in instruments. Imported drum racks retain
  nested native chains and expose their device editors in tabs; rack creation is
  currently available through Live import.
- **Reverb:** size, damping, stereo width, pre-delay, freeze and dry/wet.
- **Delay:** tempo-sync divisions or milliseconds, feedback, filtering and ping-pong.
- **Saturator:** eight curves (Analog Clip, Soft Sine, Bass Shaper, Medium Curve,
  Hard Curve, Sinoid Fold, Digital Clip, Waveshaper), Bass threshold, six Waveshaper
  controls, Color pre/inverse-post filters with draggable EQ handles, post Soft/Hard
  Clip, Pre-DC Filter, Hi-Quality (4× oversampling), Drive/Output and Dry/Wet.
  The curve display shows input level; Color and Waveshaper tabs expose advanced
  controls. Hi-Quality and Pre-DC Filter are also available by right-clicking the
  display. Settings save with the project. These are original DSP curves inspired
  by Live 12's feature set, not a sample-identical Ableton emulation. Oversampling
  latency is aligned within Dry/Wet; parallel tracks still lack host compensation.
- **Compressor:** stereo-linked threshold, ratio, attack/release, soft knee, makeup and mix.
- **Limiter:** stereo-linked sample-peak ceiling, input gain and release.
- **Parametric EQ:** eight switchable bell/shelf/pass/notch bands; drag nodes for
  frequency/gain and scroll over the graph for Q.

Single-click a sound in the Sounds library or a linked sample folder to audition
it. Single-click a factory instrument preset to hear one note. Synth presets play
C4 held for half a second, with up to three seconds of sound; Sampler presets play
their sampled note. A new click replaces the current preview; Escape in the browser
stops it. Preview volume follows Cue level; auditioning does not modify the project.
Double-click and drag keep loading sounds and presets as usual.

Find them under Instruments or Effects. Controls appear directly in Device View,
including on Master for effects. Sampler, FM Synth and Wavetable Synth each include
20 factory presets. Click the arrow to the left of an instrument in the browser to
expand its presets, then double-click a preset or drag it onto a MIDI track. Loading
a preset onto the same instrument updates its sound. Presets also appear in search
results. Device View shows the wavetable shape, the selected FM operator's wave, or
Sampler's audio waveform; built-in controls stay embedded in Device View.
Sampler presets are generated sounds (keys, plucks, basses, pads and percussion),
with their WAV audio copied into the project; sustained samples play to their end.
Preset selection and device settings save with the project.

### Return tracks and Live Set import

New projects include **A Reverb** and **B Delay**, both fully wet. Audio and MIDI
tracks have post-fader A/B sends (new tracks default to 0 dB, adjustable up to +6 dB). Returns have volume, pan, mute,
solo, meters, effects, and volume/pan automation; they stay above Master in the
arrangement. Return-to-return feedback uses a one-block delay.

Use **File → Open** to open an Ableton `.als` file. Opentone converts Arrangement
tracks and their Live 10–12 palette colors, heights/folding, annotations, armed state,
audio references, MIDI notes/loops, clip names/colors/gain/enabled state, static
mixer/send settings, MIDI input channels, fixed meter, locators, arrangement loops,
and volume/pan automation. Audio transposition uses repitch playback; fades are
converted to linear fades. Disabled notes/clips remain editable (select and press
`0` to toggle them). Track height/folding is saved in Opentone projects. It copies only referenced
samples into its working project and leaves the Live Set untouched. Save creates
an Opentone project, not an `.als` file. Existing Opentone projects still open.

Opentone focuses on Arrangement. Session clips/scenes and live launching are
intentionally omitted from ALS import. Legacy Session data in existing native
projects is retained on save, without a launch UI or playback.

This is a best-effort importer. take lanes/comping, frozen processing, groove,
MIDI/key mappings, external inputs/outputs, custom input/MIDI routing, cyclic
audio output routing, clip/device envelopes, tempo/meter
changes and Live warp algorithms remain unsupported. Unsupported devices and
third-party plug-in state are reported; editable source clips are kept. Reverb/Delay and single-sample Simpler/Sampler are mapped to Opentone
counterparts with sound/parameter differences. Up to 32 returns and 4096 notes per MIDI clip are supported. Warped audio plays at original sample speed;
audio loops are split into source-valid clips. Missing samples remain marked.
Audio output destinations for tracks, groups and returns are imported and saved.
Use the track output dropdown to change them. A receiving audio track uses its
In/Auto monitoring setting; group and return inputs remain active. Output cycles
are rejected, and deleted destinations reroute to Main. Unsupported ALS routes
are reported and mapped to Main. Right-click a send field to choose any return. Return-to-return feedback is delayed one audio block and
bounded to prevent non-finite feedback. Mute/send automation and Bézier curves
are retained. MIDI probability, signed velocity variation and release velocity
are retained; per-note pitch/pressure/timbre curves are sent as MPE to compatible
instruments (15 simultaneous member channels, 48-semitone bend range). Sampler
does not reproduce MPE modulation.
**File → Import notes** shows a report that is saved with the converted project.

Live Sets reference external sample files; collecting media in Live first makes
projects easier to move ([Ableton's project transfer guide](https://help.ableton.com/hc/en-us/articles/209071909-Transferring-Projects-to-another-computer)).

### Track groups

Cmd/Ctrl-click or Shift-click track headers to select tracks, then use
**Cmd/Ctrl+G** or **Edit → Group tracks**. **Cmd/Ctrl+Shift+G** ungroups them.
Groups can contain audio/MIDI tracks and nested groups. Fold buttons hide their
members; group lanes show member clip overviews. Members route through the group's
volume, pan, effects and sends. Mute/solo respect the hierarchy. Drag headers onto
a group to move them in, or onto an ungrouped track/empty timeline to move them
out; the header context menu also has **Move out of group**. Group deletion removes
its members; Group/Ungroup/membership changes support Undo. Copying/duplicating a
group copies its members, devices and clips. Imported `.als` groups now retain
hierarchy and mixer routing; unsupported Live devices still appear in import notes.

Sampler's waveform displays its active audio position during MIDI playback,
keyboard/controller input, and note audition, including waveform zoom/pan.
Drag the waveform's start/end poles to choose the playable region; each drag can
be undone. With waveform focus, Left/Right nudges Start by one sample and
Shift+Left/Right nudges End. The controls to the left select One-shot (play the
region through after note release) or Loop (repeat the region while a note is
held). With both off, note release stops playback. These modes save with projects.

Missing/unreadable audio remains visible in clips and Sampler with its filename.
Failed plug-ins stay in their device chain as unavailable cards. ALS imports also
retain unsupported Live devices as inert cards, including the original device XML
(up to the existing state-size limit), so saving and reopening keeps these entries.
Previously imported sets that discarded those devices need the original ALS
reimported to recover the missing entries.

Folded groups show a compact arrangement overview of their child tracks. Hold Alt/Option before dragging a clip header or piano-roll note to copy the selection; dragging selected regions copies only the selected portions. Escape cancels the drag, and Undo reverses it in one step.
