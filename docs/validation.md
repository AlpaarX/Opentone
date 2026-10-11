# Milestone 0.01 validation

Implementation and local validation: 10–11 October 2026 (Asia/Tokyo).
The application and focused checks are implemented. Cross-platform release
validation remains pending: this workspace has no GitHub remote, and the workflow
has not been run on hosted runners. Opentone must be the repository root for the
included `.github/workflows/build.yml` to be discovered by GitHub Actions.

## Local evidence

| Check | Result |
| --- | --- |
| macOS Debug configure/build | Passed; pinned JUCE 8.0.15 |
| macOS Release configure/build | Passed; pinned JUCE 8.0.15 |
| Minimum CMake 3.22 host smoke | Not established; downloaded 3.22.6 binary was terminated by this host |
| Debug focused checks | Passed |
| Release focused checks | Passed |
| ThreadSanitizer focused checks | Passed; no reported races |
| Debug/Release window smoke | Passed; opens, renders and closes, no audio device required |
| Ten-minute 16-voice Release hardware measurement | Passed, zero underruns |
| Final Release ten-second hardware smoke | Passed, zero underruns |
| Windows/Linux CI builds/checks | Configured; not executed here |
| Windows/Linux hardware playback | Unavailable |

The window smoke produces `window-smoke.png` beside the application executable.
The initial interface snapshot was inspected for layout and readable controls.
Local build outputs and raw logs are under ignored `build/`; ordinary evaluation
applications are `build/{debug,release}/Opentone_artefacts/{Debug,Release}/Opentone.app`.
They include application/dependency license notices. These are unsigned and not
notarized. No release was published.

## Focused and integrated checks

`OpentoneChecks` synthesizes small WAV, AIFF and FLAC fixtures at runtime. It checks:

- Empty defaults, finite values, valid references, source bounds, stable IDs,
  track deletion, undo/redo, drag cancellation, one undo per gesture, revision
  progression, no edit on selection, BPM-independent timing and snapping/trimming.
- Portable JSON round trips, same-name imports, authoritative metadata, missing
  media, schema/version rejection, traversal (including symlink escapes), failed
  copies and failed atomic manifest replacement preserving the previous destination.
- Cancelled workers and queued stale import completions, actual background saves
  with newer edits, and Save As retaining a stable runtime media root.
- Gaps, additive overlap, source offsets, half-open boundaries inside blocks,
  mono duplication, stereo mapping and sample-rate conversion. Nonconstant source
  ramps verify resampling speed and trimmed positions within `1e-4` amplitude;
  unresampled samples use `2e-6` or tighter tolerance for 24-bit fixtures.
- Stop/resume, arrangement-end behavior, seeking, stop/edits during a pending seek,
  rapid seeks, device configuration changes and project generation replacement.
- Rapid coalesced edits, unchanged-voice cursor continuity, intentionally stalled
  preparation, deferred adoption when cleanup is saturated, bounded outstanding
  arrangement ownership and intentionally starved streaming.
- Instrumented C++ allocations/frees during the application renderer: zero. The
  device backend and OS are outside that instrumentation. Source inspection confirms
  the renderer only consumes prepared float rings and immutable scheduling data;
  JUCE buffering/resampling calls, which have locks and may allocate, run on workers.

The integrated backend check follows the milestone scenario through supported
mixed-rate imports, arrangement playback, live move/trim/delete, track removal,
undo/redo, mute/volume, a background import, background save with a newer edit,
Save As, stop, directory relocation, exact reopen and missing-media playback.
It uses the same session/storage/engine operations as the editor, with device-free
rendering. It does not substitute for manual native-dialog or pointer testing.
The exact round-trip check caught JUCE's default JSON decimal rounding; manifests
now write 17 decimal places to retain double timing precision.

## Hardware measurement

Release console check executable; 600 seconds of stereo output with 16 concurrent
stereo voices. One clip was repositioned every 500 ms. Each edit followed the
normal immutable snapshot/preparation/handoff/acknowledgement path.

| Setting / result | Observed |
| --- | --- |
| OS | macOS 26.3, build 25D5087f |
| Hardware | MacBook Pro 18,1, Apple M1 Pro, 10 cores, 16 GB RAM |
| Executable | x86_64 Release, running with Rosetta on Apple Silicon |
| Toolchain | Xcode 26.4 / Apple Clang, CMake 3.27.2, Ninja 1.11.1 |
| Audio output / backend | MacBook Pro Speakers / CoreAudio |
| Actual sample rate / buffer | 48,000 Hz / 256 samples |
| Storage | Internal APPLE SSD AP1024R, APFS |
| Duration / transport progress | 600 s / 600 s |
| Underruns | **0** |
| Acknowledged edits | 1,196 |
| Mean / maximum activation latency | 162.601 ms / 196.222 ms |
| Sampled CPU (one-core percentage) | Mean 9.54%, range 4.6–15.0% |
| Sampled RSS | Peak 92.67 MiB; final 65.26 MiB |
| Renderer allocations / frees detected | **0 / 0** |

CPU/RSS were sampled every five seconds with `ps`; they are process observations,
not callback CPU percentages or a proof of a universal memory ceiling. Fixture
creation precedes the timed loop; latency is measured from revision submission
to engine acknowledgement. CPU sampling also includes concurrent development
work on this machine. Queue saturation checks establish the ownership bound;
RSS did not show monotonic growth during the hardware run.

The ten-minute run preceded final save precision, path encoding and transport
edge-case hardening. The subsequently rebuilt Release executable passed focused
checks, window smoke and a ten-second hardware run: zero underruns, 20 acknowledged
edits, mean 160.313 ms and maximum 162.248 ms activation, zero renderer allocations
or frees. The hardware runs exercised CoreAudio output callbacks; audible listening
quality was not independently assessed. These observations apply to this machine.

## Ownership and remaining release checks

The application owns session, storage and engine; the window is destroyed first.
The message-thread session owns the editable model and undo history. Its snapshots
carry generation/revision; saving changes the destination while keeping media
readers and concurrent imports rooted in the existing runtime workspace.

`AudioEngine::PlaybackPreparer` holds a bounded latest-request mailbox. A separate
preparation future opens/warms new voices while the streaming worker continues
refilling active voices. The callback adopts a ready arrangement at a block
boundary only if generation, device configuration, base revision and deadline
match. Unchanged voices retain their rings/cursors. Changed voices receive 5 ms
transition ramps; incompatible or late updates are retired and re-prepared.

The callback never releases ownership. The retirement ring has 15 usable slots;
adoption reserves room and defers when cleanup is saturated. Workers destroy
retired arrangements/readers. Shutdown first removes device callbacks, joins
preparation/streaming, destroys voices, then stops JUCE read-ahead. Storage cancels
jobs, waits for workers and removes only its application-owned temporary workspace.

Remaining manual checks before declaring the milestone release ready:

- Run the supplied Windows/Linux/macOS CI matrix from a standalone repository.
- Native New/Open/Save/Save As/Quit dialogs and Save/Discard/Cancel, including
  failed-save retry, cancelled file pickers and edits made while opening.
- OS/browser drops, import placeholders/errors, pointer edge trims, cross-track
  dragging, scrolling/zoom while playing, keyboard focus and accessibility tools.
- Window closure during a real long import, unavailable-output recovery and
  audible hardware playback on each target platform.

The timeline canvas is capped at ten million pixels. Its time mapping remains in
seconds; arrangements beyond that display limit require a tiled viewport. Track
and master gains are bounded to 0–2. These limits are documented rather than hidden.

## Live C++ UI follow-up

The optional `dev` preset splits a shared JUCE/model/storage/audio runtime from the
`OpentoneUI` module. Ordinary Debug/Release builds keep the UI in the application.
A versioned function table lets the host construct/replace the editor without
including `Editor.h`. Retained module mappings keep older undo/async callback code
valid until process exit. The watcher publishes a token atomically only after a
successful UI build; host/runtime edits stop watching and require a restart.

Local checks: the dev build and CTest module-swap/backend checks passed; four
Python watcher checks cover changes/removal, atomic publication, failed-build
publication suppression and core-change restart handling. A real editor implementation
edit rebuilt one object and relinked only `OpentoneUI`, then reloaded in the same
app process in approximately 4.7 seconds. Restoring the source also reloaded.
Windows/Linux dev jobs are configured but have not been executed here.

Module-swap checks preserve live rendering progress and master gain, project/revision/dirty state
and undo/redo through an action created in an older UI module, verify gesture
blocking, and reject a deliberately incompatible ABI fixture. Module retention
and focus reset are documented development limits. No safe unload,
full-runtime C++ hot patching or crash isolation is claimed.

## Presenter/layer refactor

Source is grouped under app, domain, application, infrastructure, ui and shared;
checks are grouped under core, ui and development. ProjectSession mutations and
undo actions are compiled in the runtime. Views call EditorPresenter, which owns
application workflows, pending jobs and presentation state. UI ABI version 2
receives a presenter instead of three services.

New presenter checks cover an import across view replacement, replacement dialog
attachment, snapshot-specific saves, retained presentation state, cancellation and
failed-load preservation. The module-swap check also verifies selection, zoom,
snap and scroll survive rebuilding the component tree. Core workflows and the
isolated development host are checked without replacing the pre-refactor running
host, whose quit request was declined. See architecture.md for the layer contract.

## Audit regression checks

Focused checks now cover a save failing after an earlier audio copy, a failed
manifest replacement with conflicting destination audio, successful conflict
renaming and reuse on subsequent saves. Existing media stays unchanged and failed
saves remove only their newly copied files.

Playback checks cover tempo changes followed by a seek, continued cursor progress,
live edits after a seek between buffer boundaries, and Play pressed before the
first arrangement is prepared. The watcher also checks shared-header changes
during a UI build and prevents publication when a restart is needed.

## Clip/time selection, clipboard and pinch zoom

Model/presenter checks cover selection toggling, duplicate/stale ID filtering,
selection without edits, atomic batch validation, group movement/cancellation,
and deletion with one undo step. Partial-clip checks verify split source offsets,
remainder preservation, invalid ranges, duplication, copying, pasting onto other
tracks, track creation and single-step undo/redo. Copy does not edit the project,
Duplicate preserves the clipboard, and New clears it.

The live UI smoke retains clip/time selections and the audio clipboard across
module swaps. It drives the real timeline handlers through header selection,
waveform time selection, nearest-grid clicks, partial splice/move, cancellation,
group edits, clipboard commands and text-focus guards. Synthetic pinch events
verify zoom anchoring and finer grid snapping. Physical trackpad testing and
Windows/Linux pointer/device testing remain pending.

Selection playback checks verify that Play seeks to the selected time, a clicked
marker or the earliest selected clip, and that Play after Pause restarts at that
same selection. With no selection it starts at zero. Render checks keep the
insertion line inside one track; pointer checks resize clips from header ends
while waveform-edge drags select time without changing the clip.
Hover checks distinguish the `[` and `]` header-end cursors, restore the normal
cursor over waveform/header centres and on exit, and ensure header ends still
resize when a time selection covers the clip.

Overlap checks cover middle/left/right/full coverage, touching boundaries, source
offsets, other tracks, multiple placements, import, undo/redo, cancelled drags and
dragging across a clip without dropping on it. Overlap cuts happen on gesture commit.
Track resize checks drive the header border, verify hit testing and marker height,
cancel with Escape and retain heights through UI reloads without editing project
audio. The CPU/UI lag label is checked at the top right; CPU uses JUCE's audio
callback load. A deliberately blocked message thread verifies the UI lag display
and warning colour.

Metronome checks render offline audio to verify beat timing at 120 and 60 BPM,
the 4/4 downbeat accent, silence between beats and while paused, selected-time
seeks, master volume, and playback in an empty arrangement. The UI check toggles
the transport button and verifies engine state. Speaker/headphone listening remains pending.

## Zoom and slicing redraw regression

A 120-second stereo WAV at 400 pixels/second, sliced at 0.5 and 1 second, reproduced
the permanent drawing slowdown. JUCE's thumbnail drawing cache was shared between
all clips referencing a file, while each draw requested the full clip width.
At high zoom this repeatedly read raw audio for offscreen portions of the file.

On macOS, a Debug-module benchmark averaged 20 full editor snapshots at
1200×760 after warming up. At 400 pixels/second, sliced redraws fell from
396.6 ms to 3.0 ms; unsliced redraws fell from 5.0 ms to 2.7 ms. These are
synthetic drawing times, not display latency measurements.

Waveform drawing now crops the horizontal source range to the viewport and caches
each visible clip image. Clip/zoom/scroll/source changes invalidate the image;
offscreen images are discarded. Raster resolution follows the display pixel scale.
Regression checks verify three initial waveform renders, widths bounded by the
viewport, no further waveform renders on repeated redraws, and correct cache
invalidation after zoom. Hardware interaction and Windows/Linux checks remain pending.

## MIDI track and sampler checks

Focused checks verify sampler source/root settings and MIDI notes round-trip in
version 2 manifests, legacy version 1 audio projects open, and invalid note/schema
data is rejected. Offline rendering verifies note start/end timing, source-region
trimming, octave-up/down resampling, velocity, silent gaps, arrangement end and
seeking inside a note, without callback allocations or destruction. Sampler file
imports, source changes, note edits, splitting and duplication retain undo behavior.

Native UI checks create a MIDI track, drop browser audio and an existing clip into
its sampler, open a MIDI clip, add/move/resize/delete notes, and cancel a note drag
with Escape. A sampler/piano-roll snapshot is written as `midi-smoke.png` beside
the live runtime. Hardware MIDI input is outside this implementation; speaker
listening, very dense MIDI arrangements and Windows/Linux interaction remain unchecked.

Docked-panel checks cover the searchable instrument dropdown, case-insensitive
live filtering, staying open through refreshes, selecting Sampler, and the audio
clip dropdown remaining open. They verify header-only opening for existing MIDI
clips, backtick/tilde open/close, resizing from the top divider, fully dragging down
to close while keeping the instrument visible, and text-entry guards. Computer
keyboard Z/Q handling leaves the model revision unchanged. Offline audition checks
verify sound while paused, unchanged playhead/revision, two simultaneous held notes,
key release, automatic edit-preview ending, and no callback allocation/destruction.

Sampler/roll checks verify the waveform and source/volume controls, source-range
undo, double-click insertion with single-click selection, distinct shared bracket
cursors, both note edges, Shift-selected group moves and stretches, marquee
selection/deletion, singular selection, track-colour note fills, mouse/computer
key highlights and release, coloured track-name selection/highlighting, and full
roll expansion up to the app header. Offline MIDI checks verify activity publication
for played notes, gaps, preview chords and releases, including MIDI without a
loaded sample. Physical keyboard/trackpad feel and speaker listening remain unchecked.
