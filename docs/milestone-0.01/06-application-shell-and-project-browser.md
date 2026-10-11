# Task 06: Application shell and project browser

## Objective

Connect project lifecycle, assets and transport through an accessible desktop interface.

## Dependencies

- [Task 03: Project storage and import](03-project-storage-and-import.md)
- [Task 04: Audio playback engine](04-audio-playback-engine.md)

## Implementation requirements

- Use an original compact dark JUCE interface with a project asset browser on the left, transport above and a timeline area reserved for task 07.
- Keep browser, transport and main application components separate. Views read session state and issue explicit operations; they do not own another arrangement model.
- Provide New, Open, Save and Save As through desktop menus and conventional platform shortcuts. Space toggles the engine transport when a text editor/dialog does not have focus.
- Prompt Save/Discard/Cancel when New, Open or Quit would abandon dirty work. A failed or cancelled save leaves the current project open.
- Stop playback for New/Open; saving and Save As do not stop playback.
- Display project assets with filenames and missing/loading/error state. Browser items carry asset IDs for timeline drag/drop; dragging an existing asset does not copy its file again.
- Provide an import action for files not yet in the project. Imports show progress and errors; a file imported only to the browser creates no clip until placed.
- Show play/stop, cursor position, editable BPM, master volume, clipping and underrun indication. Refresh telemetry at approximately 30 Hz using atomics, not audio-thread UI callbacks.
- Provide the JUCE output-device/rate/buffer settings panel and surface device failures.
- Keep device settings local and restore them on launch when the device still exists; otherwise use the default output.
- Label controls for accessibility, support keyboard focus and avoid relying solely on color for errors or selection.
- Ensure closing the app invalidates pending jobs and follows engine shutdown ownership rules.

## Acceptance criteria

- Project commands work from menus and shortcuts, including unsaved-change and failed-save paths.
- Saving while playing preserves playback; closing/replacing a project cancels stale work safely.
- Browser imports and asset drags use the same storage/session operations as external timeline drops.
- Device settings, transport and status are usable by keyboard and expose accessible labels.

## Checks

- Manually check commands, focus/shortcut interactions, Save/Discard/Cancel and import errors.
- Exercise window closure while importing and unavailable output-device recovery.
- Verify UI telemetry remains responsive during playback and background copies.

## Exclusions

No global filesystem browser, sample preview player, plugin browser, custom settings framework or auto-updater.
