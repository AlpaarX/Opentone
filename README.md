# Opentone

A native C++17 / JUCE audio arrangement editor for macOS, Windows and Linux.
Milestone 0.01 implements a compact Arrangement View inspired by Ableton Live:
project audio at left, compact transport above, waveform lanes in the centre and
coloured track controls on the right. Flat grey panels, rectangular controls and
pastel clip headers follow the Arrangement View layout.

See [architecture and source layout](docs/architecture.md) for ownership and dependency boundaries.

## Build

Use CMake **3.22+**, Ninja, a C++17 compiler and network access on first configure.
JUCE **8.0.15** is fetched at commit
`91ad83ae34a81e0833b1a2b0866f54846370ae53`, with an archive SHA-256 check.
Run all commands from this directory. Dependencies and outputs stay under `build/`.

### macOS

Install Xcode or its command-line tools, CMake and Ninja (for example,
`brew install cmake ninja`). Select the installed developer tools with `xcode-select`
if needed.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --test-dir build/debug --output-on-failure
open build/debug/Opentone_artefacts/Debug/Opentone.app

cmake --preset release
cmake --build --preset release --parallel 4
ctest --test-dir build/release --output-on-failure
open build/release/Opentone_artefacts/Release/Opentone.app
```

To select an architecture explicitly, pass `-DCMAKE_OSX_ARCHITECTURES=arm64` or
`-DCMAKE_OSX_ARCHITECTURES=x86_64` on configure. Evaluation builds are unsigned and
not notarized; there is no installer.

### Windows

Install Visual Studio 2022 with Desktop development with C++, a Windows SDK, CMake
3.22+ and Ninja. Use an **x64 Native Tools Command Prompt** (or a Developer PowerShell
with the compiler environment loaded).

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --test-dir build/debug --output-on-failure
.\build\debug\Opentone_artefacts\Debug\Opentone.exe

cmake --preset release
cmake --build --preset release --parallel 4
ctest --test-dir build/release --output-on-failure
.\build\release\Opentone_artefacts\Release\Opentone.exe
```

### Linux

On Ubuntu 24.04 / Debian, install the libraries for JUCE GUI, font rendering and
ALSA output; browser/curl support is disabled. The device settings can select the
available JUCE audio backends. Run on an X11 desktop (or through XWayland).

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --test-dir build/debug --output-on-failure
./build/debug/Opentone_artefacts/Debug/Opentone

cmake --preset release
cmake --build --preset release --parallel 4
ctest --test-dir build/release --output-on-failure
./build/release/Opentone_artefacts/Release/Opentone
```

The `check` build target runs focused checks. `--smoke-test` on the application
opens a window, writes a render snapshot beside the executable and exits without opening an audio device. On a headless Linux CI
runner, use `xvfb-run -a` (package `xvfb`). CI configures, builds and checks Debug
and Release on all three platforms and retains evaluation artifacts.

## Live C++ UI development

From this directory, with Python 3.9+ and the build prerequisites installed:

```sh
python3 tools/dev.py
# Windows, in a developer terminal:
py -3 tools/dev.py
```

The first run builds the `dev` preset and opens **Opentone [Live UI]**. Save changes
in `src/ui/` (editor, browser, transport, timeline or theme): the watcher
compiles just `OpentoneUI`, then the existing process replaces its editor component.
The shared JUCE/session/storage/audio runtime stays alive. Project data, dirty
state, undo history and playback are preserved. UI-only callbacks, drawing,
controls and component layouts can change as ordinary C++; this is not a JSON
styling layer. Native code still compiles, but JUCE and the host are reused.

Reload waits for clip/control gestures, text entry, file pickers and dialogs.
Imports, loads and saves are owned by the stable presenter and can continue through
a reload. Failed compilation keeps the current editor active; save a correction
to retry. The component tree is recreated; selection, zoom, snap and scroll are
retained by the presenter, while keyboard focus resets.

Changes outside `src/ui/` (app, domain, application, infrastructure, shared
exports and CMake configuration) require a restart. The
watcher stops without overwriting a running runtime. Save and close the dev app,
then rerun the command. The host and UI must use the same compiler/configuration
and shared headers; an incompatible module ABI is rejected.

Old UI module versions remain mapped until process exit because undo actions and
queued callbacks may contain pointers into them. Memory grows with reload count;
save/restart between long development sessions. Versioned copies avoid locking the
build output on Windows; copies still locked at shutdown may remain under ignored
`build/dev/`. The dev preset uses local build paths and is not a distribution build.
Normal Debug/Release builds compile the UI into the app. Dev device preferences
are separate from ordinary app preferences.

Ctrl+C stops watching and leaves the app open; close it normally for the usual
unsaved-change prompt. Build/reload messages go to the terminal and
`build/dev/live-ui.log`. Relaunch the watcher after closing that development app.

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --test-dir build/dev --output-on-failure
python3 tests/development/test_dev.py
```

The dev CTest suite includes a real module-swap check: playback continues, project
and dirty state survive, view state is retained, runtime undo/redo works, gestures
block replacement and an incompatible module is rejected. On headless Linux it
uses `xvfb-run` when available. CI includes the dev preset on all three platforms.
The save/build/reload loop was exercised locally on macOS in about 4.7 seconds for
one editor implementation change, without changing the app PID.

## Editing

- Import mono/stereo WAV, AIFF, FLAC or MP3 into the project browser with Import audio
  (Cmd/Ctrl+I); drag browser items onto a lane to place existing assets.
- Drop audio files onto the timeline to import and place clips. Dropping below the
  lanes creates a track. Multiple-file drops create separate tracks at the same
  grid-snapped time in file order. Loading placeholders cannot be edited.
- Add tracks, double-click a track name to rename it, adjust volume or mute, or
  delete a track with its X button. Deletion keeps the underlying media files.
  Drag the bottom border of a track header to change its height; Escape cancels.
- Click a clip's header to select it; Shift-click adds or removes clips. Drag below
  headers, over waveforms or empty space, to select time across tracks. A click
  marks the nearest grid line; Shift-click extends a time selection. Cmd/Ctrl+A
  selects all clips, and Escape clears selection.
- Drag selected headers to move clips together across time or tracks. Dragging a
  header inside a time selection splits out and moves only the selected audio,
  preserving the pieces outside the selection. Only the two header ends resize
  a whole clip; hover shows `[` on the left end and `]` on the right end.
  Dragging waveform edges selects time.
  Release commits one undo action; Escape cancels the split and move together.
  Delete/Backspace removes selected clips or just the selected time in one undo step.
  A clip placed over another clip on the same track cuts away the covered audio.
  Uncovered pieces retain their source positions. Drag previews leave other clips
  intact; the cut happens on release and undoes with the placement.
- Cmd/Ctrl+D duplicates selected audio immediately after the selection. Cmd/Ctrl+C
  copies it; Cmd/Ctrl+V inserts it at the clicked marker, preserving relative track
  positions and adding tracks if needed. Covered audio on the destination track is
  replaced; existing audio outside the pasted clips is retained. The audio
  clipboard belongs to the current project and clears on New/Open. Text fields
  keep their normal clipboard shortcuts. Cmd/Ctrl+Z undoes; Shift+Cmd/Ctrl+Z redoes.
- Pinch the trackpad or use the zoom slider to zoom. Pinch keeps the time under the
  pointer fixed. Zoom reaches 40,000 pixels per second. The grid becomes finer as
  you zoom in; snapping uses that same grid.
  Toggle Snap for free timing and scroll the arrangement normally.
  Timing is in seconds; BPM controls the 4/4 grid and metronome. Audio plays at original speed.
- Play/Pause or Space uses the shared transport; Stop holds the cursor. Click the
  ruler to seek. Every Play starts at the selected time or selected clips, including
  after Pause/Stop; with no selection it starts at zero. The insertion line stays
  within one track.
  Space/Delete do not intercept text entry or modal dialogs.
- Toggle Metronome in the transport for a tempo-synced click with an accented
  first beat in each 4/4 bar. It follows the selected playback start and master
  volume, sounds only while playing, and lets you play beyond the last clip or
  in an empty arrangement. The toggle is a session setting, not saved in projects.
- Add a MIDI track with **+ MIDI track**. Double click its lane to create a
  four-beat MIDI clip. Double click an existing MIDI clip's header or press
  **backtick / tilde (~)** to open its piano roll at the bottom. Press the same key to close it,
  or drag its top divider fully down. Drag the divider to resize the roll vertically.
  Double click empty space to add a note. Click a note to select it, Shift-click
  to select several, or drag a selection box. Drag selected note bodies to move
  them together; drag either edge to stretch them, with `[` / `]` cursors.
  Delete/Backspace or right click deletes selected notes. Escape cancels a drag;
  undo/redo and
  clip selection, slicing, duplication, copy/paste also work for MIDI clips.
- In the piano roll, white-key rows are lighter and black-key rows darker. The
  area outside the clip's source range is shaded, but notes there can still be
  added, selected, moved and stretched. They remain stored and become part of
  playback when the clip's header edges are extended to include them.
  Cmd/Ctrl+C/V/D copies, pastes at the clicked insertion line, and duplicates
  selected notes after their selection. Arrow keys move notes by grid steps or
  semitones; Shift+Up/Down transposes octaves and Shift+Left/Right adjusts lengths.
  Keyboard movement follows the [Ableton note editing shortcuts](https://www.ableton.com/en/live-manual/12/editing-midi/).
- Open **Instruments** in the left Library, select a MIDI track, and double-click
  **Sampler**, or drag it onto a MIDI track. The search field filters devices.
  The sampler stays at the bottom when the piano roll is closed. Load a WAV, AIFF,
  FLAC or MP3 file, or drop one onto the MIDI lane or bottom sampler panel. Drag a browser audio file onto
  a MIDI lane to load it. **Use audio clip** loads a clip's trimmed source region;
  dragging an audio clip header onto a MIDI lane also loads that region and keeps
  the original clip. Alt-drag an audio clip header to drop it in the sampler panel.
  The sampler root defaults to MIDI note 60 (C4); other notes transpose by changing
  playback speed. Notes use velocity and stop at note-off or the sample region's
  end. MIDI clips hold up to 512 notes, with timing in seconds.
- The sampler's waveform shows the selected source region. Its Start/End controls
  edit that region, Root sets the untransposed MIDI note, and Volume controls the
  instrument independently of the track fader. Drag the coloured **Sampler** device
  header onto another MIDI track's header or lane to move it, including its sample,
  region, root and volume settings. The destination must have no instrument.
  Click the device's X, or focus its header and press Delete, to remove it. Moving
  or removing a sampler retains notes and audio files and can be undone.
  Notes use the track colour, and piano keys highlight mouse presses,
  computer-keyboard notes and MIDI playback. Expand the roll's top divider up to
  the app header. Click a track's coloured name or background to select it; the
  selected track has a highlighted header and name outline.
- Placing, selecting or moving a piano-roll note auditions it without moving the
  arrangement playhead, including while paused. The computer keyboard plays the
  selected MIDI track's sampler: **Z S X D C V G B H N J M** span C4–B4;
  **Q 2 W 3 E R 5 T 6 Y 7 U** span C5–B5. Hold keys for chords; release stops them.
  Text fields and Cmd/Ctrl shortcuts retain their usual behavior. Audition supports
  16 voices, follows track mute/gain and master volume, and stops at the sample end.
- **File > Settings > Audio** selects output, sample rate and buffer size. Device settings are
  local, outside the project. Changing devices stops playback and rebuilds buffers.
- Structural edits and undo/redo prepare asynchronously while the current
  arrangement continues playing. A status message indicates pending playback
  changes; intermediate drag previews may be coalesced. Gain/mute are smoothed.
- The master is an additive mix; the status bar reports clipping and underruns.
  The top-right display shows audio callback CPU load and recent peak UI lag in
  milliseconds. UI lag measures overdue updates on the message thread; a stall
  longer than one update interval highlights the display in red.

## Projects

New/Open/Save/Save As have conventional Cmd/Ctrl shortcuts. Save/Discard/Cancel
protects unsaved work on project replacement and Quit. Save selects a project
directory for a new project; choose/create a folder in the native directory picker.

A project directory contains `project.opentone` (JSON format version 5) and `Audio/`.
Version 1 audio projects still open. Version 2 saves MIDI notes, track types and
sampler source/root settings and AU/VST3 device chains with saved plug-in state.
Versions 1–4 remain readable; older builds cannot open version 5 projects.
Imports are copies under unique filenames. Move the whole directory to relocate
it. The project never depends on original import locations. Unsaved imports live
in an application-owned temporary workspace; shutdown removes only that workspace.
Missing media reopens as visible silent clips with a warning. Malformed manifests,
unsupported versions and paths escaping the directory are rejected before project
replacement. Undo history, output preferences and waveform caches are not serialized.

Saving captures a generation/revision-specific snapshot and replaces the manifest
only after its media copies succeed. Different audio at an existing destination
gets a fresh filename; failed saves preserve existing audio and remove their new copies.
Later edits remain unsaved. Save and Save As
continue playback. Clip/track removal never deletes media. Save As asks before
replacing an unrelated existing project manifest.

## Checks and measurement

Focused checks generate their own WAV fixtures and exercise model validation,
undo/redo, gesture cancellation, portable storage, failed saves, sample scheduling,
mono/stereo mapping, resampling, gain/mute, live revisions and callback ownership.
See [validation evidence](docs/validation.md) for actual results and outstanding checks.

```sh
# Device-free paced measurement (does not establish hardware playback performance)
./build/release/OpentoneChecks_artefacts/Release/OpentoneChecks --performance 600
# Ten minutes, default stereo hardware output, requesting 48 kHz / 256 samples
./build/release/OpentoneChecks_artefacts/Release/OpentoneChecks --performance 600 --hardware
```

Both measurements use 16 stereo voices and repeatedly reposition one clip. They
report underruns and revision activation latency. A zero-underrun assertion fails
if the target is missed. Record actual hardware/device/backend, settings, CPU and
memory along with the output; results apply only to that machine.

For supported Clang environments, create a separate build directory for sanitizers:

```sh
cmake --preset debug -B build/tsan \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build/tsan --target OpentoneChecks --parallel 4
ctest --test-dir build/tsan --output-on-failure
```

## First-release scope

Clip/time selection, audio arrangement, MIDI piano-roll editing and a built-in
sampler, microphone recording and hardware MIDI note input. No stretch/warp,
plug-in parameter automation, export, grouping,
cloud storage, installer signing or auto-updater. Live changes may be delayed by
preparation; seeking can interrupt sound briefly. Missing audio is not relinked
automatically. Waveform images are cached only for visible clip sections, at the
display's pixel density; slicing and playhead redraws reuse those images. The
waveform cache is bounded; decoded audio uses streaming buffers,
not whole-file loading. Track/master gains range from 0 to 2; the timeline canvas is capped at ten million pixels.

Application source is [AGPLv3](LICENSE). See [dependency notices](THIRD_PARTY_NOTICES.md)
and `licenses/` for JUCE and its bundled dependencies.

### Plug-ins and track creation

Right-click empty arrangement or track-header space to add audio or MIDI tracks.
Ctrl/Cmd+T and Ctrl/Cmd+Shift+T also add audio and MIDI tracks.
On macOS the app uses the native menu bar.

In Settings → Plug-ins, use **Scan / folders…** to choose AU (macOS) or VST3,
scan standard locations, or specify custom folders. Discovery runs in a separate
process per plug-in; failed or timed-out scans are blacklisted and can be retried
from the scanner menu. The discovered list and search paths are saved locally.
VST2 is excluded from this build.

Open **Instruments** in the Library to browse Sampler and discovered instruments.
Double-click to add one to the selected MIDI track, or drag it onto a track.
Right-click empty Device View space to add an effect. A device's **Open plug-in**
button opens its native editor (or JUCE parameter editor when none is supplied).
Devices can be bypassed, dragged by their headers onto another track, or removed
with Delete or the right-click menu. One instrument fits on a MIDI track; effects
run in device order. Missing plug-ins keep their project metadata and saved state.

Plug-in audio is processed live in the app. This first host implementation uses
mono/stereo main buses, disables sidechains, and does not compensate latency
between tracks. Discovery is isolated; an instantiated plug-in shares the app
process. Projects contain plug-in references and settings, not the plug-in binaries.

MIDI clips keep a source loop length separately from their arrangement duration.
Dragging a clip header edge repeats the source loop. Piano-roll controls show
loop start, loop length, and arrangement clip length in beats; disable Loop to
use a linear source region. Editing notes outside the source loop keeps them in
the clip; extend or move the source loop to include them in playback. The playback
needle wraps through that source loop.

The computer piano keyboard and Space (play/pause) also work while a hosted
plug-in editor is focused. Piano keys leave plug-in text fields available for typing; Space remains a host
play/pause shortcut while a plug-in window is focused.

Fresh projects start with one audio track and one MIDI track. Plug-in windows
stay above the arrangement while Opentone is active and return to normal window
level when another app is active.

Press **Tab** or click **Mixer** at the bottom left to switch between arrangement
and mixer views. The mixer includes track volume faders, mute and Solo buttons,
stereo output meters, and a master strip. Selecting a channel shows its devices
below. Right-click empty mixer space to add tracks, or a channel to copy, paste,
or delete it. Double-click a fader to return to 0 dB.

### Library folders

The left browser has **Sounds**, **Instruments**, and **Effects** folders.
In Sounds, **+ Folder** adds a reference to a sample directory. Choose a linked
folder from the location menu and expand its subfolders to browse WAV, AIFF,
FLAC, and MP3 files. Browsing does not import media. Dropping a sample onto an
audio track, sampler/MIDI track, or Device View, or double-clicking to use it,
copies that file through the normal project import path. **Project Audio** lists
media already imported. Removing a folder link leaves its files on disk.

Instruments includes Sampler; Effects includes the built-in **Utility** device
with gain and stereo balance controls. Scanned AU/VST3 devices appear in the
folder matching their instrument metadata. Right-click a third-party device
and choose **Assign to Instruments** or **Assign to Effects** to override its
classification. Folder references and category assignments are local preferences,
kept across app restarts. Drag devices onto arrangement tracks, mixer channels,
or Device View to add them. Dropping into empty arrangement space creates the
appropriate track with the device in the same model edit.

### Device and piano-roll zoom

Device View scrolls horizontally using its bottom scrollbar, trackpad horizontal
scroll, or Shift+wheel. Sampler is shown in a bordered device card with a combined
waveform, sample boundaries and time scale, and root, gain, start, and end controls.
Pinch or Ctrl/Cmd+wheel over its waveform to zoom, drag to pan, and double-click
to return to the full sample. Arrangement waveforms also use a single combined
shape instead of separate channel rows.

The piano roll's **H** control changes time zoom; **V** changes note/key height.
Pinch or Ctrl/Cmd+wheel zooms time around the pointer; Alt/Option+wheel changes
note height. The grid becomes finer as time zoom increases.

Master volume is in the fixed bottom Master row of the arrangement and the mixer
Master strip. The transport contains an icon for the metronome; scrolling over
BPM changes it by one integer BPM per wheel event. Playback continues past the
last clip until paused or stopped. Light and Dark presets use relative gray
highlights (darker in Light, lighter in Dark); custom highlight colors remain
editable in Appearance.

### Recording

Click the round record-arm button beside a track's Solo button, then the round
Record button beside Stop in the transport. If no tracks are armed, Record arms
the selected track. Recording starts at the arrangement selection. Stop or pause
finishes the take and adds editable clips; clicking Record again finishes the
take while playback continues. A take is one undo operation.

Audio tracks record the first enabled microphone/input channel to a mono 24-bit
WAV in the project Audio folder. Choose an input in Settings > Audio and allow
macOS microphone access when prompted. Armed audio tracks show live microphone
levels even while playback is paused. Multiple armed audio tracks share this
input. MIDI tracks record note pitch, velocity and duration from the computer
keyboard on the selected MIDI track, or from enabled MIDI controllers on all
armed MIDI tracks. Connected controllers are enabled automatically; input toggles
are available in Settings > Audio. Load Sampler or a plug-in instrument to hear
MIDI input. Controller automation, pitch bend and sustain are not recorded.
MIDI takes currently stop at the project's 512-note-per-clip limit.

### Pan, automation and Master effects

Track headers and Mixer strips show volume in dB and pan as C, L or R. Pan ranges
from 50 L to 50 R; double-click the pan control to center it. Master has the same
controls and saves its volume, pan and effect chain with the project.

Click Automation above the track headers to overlay volume or pan envelopes on
the arrangement, including Master. Choose Volume or Pan in each track header.
Empty envelopes show dotted lines at the current value. Click to add a point, drag empty space to box-select points, and Shift-click to
add to a selection. Drag selected points together to change time/value. Use
Cmd/Ctrl+C, X, V, D and A to copy, cut, paste, duplicate and select all; Delete or
the right-click menu removes selected points. Alt bypasses the grid during edits. Automation interpolates
linearly during playback, and edits support undo. Plug-in parameters are not yet
automated.

Select Master to open its Device View, then double-click an effect in the library
or drag an effect/device onto Master. Effects can also be dragged from another
track's Device View onto Master, and removed with Delete or their context menu.
Master effects process the summed track output before Master volume and pan.

### Arrangement loop

Select a time region or clips and press Cmd/Ctrl+L to set and enable the loop.
The loop toggle beside Record enables/disables it. A brace above the time ruler
stays visible while scrolling vertically: drag its ends to resize, its middle
to move, or drag empty brace space to draw a range. Double-click the brace to
toggle looping; right-click it for Loop selection. Loop settings are saved with
the project. Playback repeats audio, MIDI and automation, and reports loop timing
to hosted plug-ins. Looping pauses during recording so takes remain continuous.
Clip headers show small ticks at source-loop repeats and arrangement-loop cuts.

Double-click volume to return to 0 dB and pan to center, including numeric fields
and Master controls. Sampler gain/root, piano zoom/clip controls and tempo also
support double-click defaults. Automation envelopes still control automated
parameters during playback.

Green is the default primary color for enabled controls and unmuted track buttons.
Change it in Settings > Appearance > Primary. Device power is a rounded square
at the top-right of the Sampler or plug-in header; green means enabled and a
neutral square means disabled. Sampler power and plug-in bypass states save with
the project and support undo.
