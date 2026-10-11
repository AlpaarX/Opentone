# Architecture

Opentone uses a layered desktop architecture with Model–View–Presenter.

```text
src/
  app/              Application composition, window, UI module ABI and loader
  domain/           Project data, validated edits, undo and timeline calculations
  application/      EditorPresenter: commands, workflows and presentation state
  infrastructure/   AudioEngine and Storage implementations
  ui/               JUCE views, theme and UI module entry point
  shared/           Runtime export declarations
tests/
  core/             Domain, services, presenter and integrated workflow checks
  ui/               Incompatible-module fixture
  development/      Build-watcher checks
```

`app/Main.cpp` owns the session, storage, audio engine and presenter. Views receive
only `EditorPresenter&`. They display read-only project data, translate pointer
coordinates, issue commands and show native dialogs. `EditorPresenter::Dialogs`
is the small port for save/discard/cancel, save locations and overwrite decisions.
The JUCE device selector receives the existing device manager through the presenter.

The presenter coordinates New/Open/Save/import, tracks pending work, connects
session revisions to playback, and retains selection, zoom, snap and scroll state.
Storage completion callbacks are runtime code and refer weakly to the presenter,
so destroying/reloading the view does not cancel those jobs. A new view attaches
to the same presenter; destroying the previous view cannot detach its replacement.

`Project` is plain project/media data. `ProjectSession` owns edit validation, undo,
dirty state, generation and revisions. Its implementation and undo actions live
in the stable runtime, outside the reloadable UI. The domain does not include UI,
storage or device classes. Existing concrete services and constructor wiring are
used; there is no dependency-injection framework or generic event bus.

The model/presenter operate on the JUCE message thread. Immutable snapshots cross
to storage/playback workers. Audio rendering retains its existing callback/worker
ownership contract. Presentation state and native dialog objects are not saved in
project manifests.

MIDI tracks retain a sampler asset, source region and root note. MIDI clips store
notes in source-relative seconds; clip offsets preserve note timing through trims
and slicing. The playback preparer expands notes into pitched streaming voices,
so sample reads, resampler setup and allocations stay outside the audio callback.
Note velocity and a short edge fade apply before track/master gains. Version 2
manifests store MIDI/sampler data, while the loader still accepts version 1 audio projects.

Audition uses 16 streaming voice slots independent of the arrangement clock.
The worker prepares and frees voices; the callback consumes them, applies release
fades, and returns ownership atomically. Track controls are shared with arrangement
playback. Computer-keyboard note ownership stays in the editor; focus, text entry,
track changes and key release stop held voices. Search dropdowns stay owned by the
editor and close on a choice, Escape or an outside press. The bottom sampler and
resizable piano roll share one panel; track/clip context and roll height/open state
live in the presenter for UI reloads.

Playback preparation also captures MIDI note intervals independently of sampler
availability. The callback publishes two atomic activity words per track for key
highlights; the UI combines them with immediate mouse/computer-keyboard state.
The sampler waveform owns its thumbnail cache and source, separate from clip
drawing. Note selection stays in the roll, with grouped move/stretch gestures
using the existing model undo mechanism. Notes are clipped in double precision
before painting and editing visible source regions.

The development runtime contains domain, application and services. `OpentoneUI`
contains only views/theme. UI ABI version 10 constructs the editor from a presenter
whose view state retains clip IDs, a time selection, an insertion marker and track heights. The
presenter retains the project-local audio clipboard through UI reloads. Shared layout changes require
a host/runtime restart.
Reload gates gestures, text entry and native dialogs; background imports/saves can
continue while views are replaced. Focus resets; selection, zoom and scroll survive.
Old module versions remain mapped until process exit for any remaining UI callbacks.

Shutdown destroys the window, presenter, engine, storage and session in that order;
UI module mappings remain valid through queued callback cleanup. Build instructions
and development limits are in README.md.
