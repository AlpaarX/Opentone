# Opentone

A native desktop audio workstation built with C++17 and JUCE. In active development.

Features include audio and MIDI tracks, microphone and MIDI recording, a piano
roll, a built-in sampler, AU/VST3 effects and instruments, a mixer, arrangement
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
