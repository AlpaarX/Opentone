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
looping, and volume/pan automation. Master supports its own effect chain.

Audio import supports WAV, AIFF, FLAC and MP3. VST2 is not enabled.

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
- **Reverb:** size, damping, stereo width, pre-delay, freeze and dry/wet.
- **Delay:** tempo-sync divisions or milliseconds, feedback, filtering and ping-pong.
- **Compressor:** stereo-linked threshold, ratio, attack/release, soft knee, makeup and mix.
- **Limiter:** stereo-linked sample-peak ceiling, input gain and release.
- **Parametric EQ:** eight switchable bell/shelf/pass/notch bands; drag nodes for
  frequency/gain and scroll over the graph for Q.

Find them under Instruments or Effects. Controls appear directly in Device View,
including on Master for effects. Device settings save with the project.

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

Missing/unreadable audio remains visible in clips and Sampler with its filename.
Failed plug-ins stay in their device chain as unavailable cards. ALS imports also
retain unsupported Live devices as inert cards, including the original device XML
(up to the existing state-size limit), so saving and reopening keeps these entries.
Previously imported sets that discarded those devices need the original ALS
reimported to recover the missing entries.

Folded groups show a compact arrangement overview of their child tracks. Hold Alt/Option before dragging a clip header or piano-roll note to copy the selection; dragging selected regions copies only the selected portions. Escape cancels the drag, and Undo reverses it in one step.
