# Task 07: Timeline and clip editing

## Objective

Deliver the first arrangement workflow with waveform clips and editing during playback.

## Dependencies

- [Task 05: Live arrangement updates](05-live-arrangement-updates.md)
- [Task 06: Application shell and project browser](06-application-shell-and-project-browser.md)

## Implementation requirements

- Add separate timeline and track-header components with a shared vertical scroll position. Support horizontal scrolling, vertical scrolling and timeline zoom.
- Display track names, volume/mute controls, selected clips, original-source waveform ranges and a continuously updated playhead.
- Generate waveforms using JUCE thumbnails/cache in the background. Paint only visible tracks/clips; repaint playhead and affected regions rather than rebuilding the whole UI each frame.
- Default to 120 BPM, fixed 4/4 and quarter-note snapping. Provide a snap toggle. Zoom changes pixel mapping, not project timing; changing BPM changes only the grid.
- Allow adding/renaming/deleting tracks and selecting/moving/trimming/deleting clips. Dragging a clip vertically changes track; horizontally changes time.
- Clamp starts to zero and trims to valid source bounds. Trimming the left edge updates timeline start, source offset and duration together; trimming the right edge changes duration.
- Accept WAV/AIFF/FLAC from the OS and assets from the project browser. Drop onto an existing track to insert there; dropping below tracks creates a track. Multiple files create separate tracks at the same snapped drop position in drop order.
- Display external imports as loading placeholders until task 03 completes. Do not permit editing an incomplete placeholder; remove failed placeholders and report the error.
- Send drag previews through the session's coalesced revision path. Release commits one undo action; Escape cancels the gesture. Delete/Backspace acts on the selected clip unless text editing has focus.
- Keep the playhead moving while clips/tracks are edited. Pending playback changes may be indicated subtly until acknowledged; final released edits must eventually become active.
- Clicking the ruler seeks using the engine's asynchronous seek path. Provide keyboard undo/redo and conventional selection/focus behavior.
- Removing clips/tracks affects only arrangement references and never deletes asset files.
- Use single-clip selection for this release; no multi-selection or grouping is required.

## Acceptance criteria

- Import, arrange, trim, play and undo work end to end without structural edits stopping playback.
- Waveforms follow source offsets after trimming and remain aligned across scrolling and zoom.
- Snap toggle, grid and clip placement remain correct at different BPM and zoom levels.
- Multiple-file drops create predictable tracks without filename collisions or unintended asset duplication.
- Keyboard operations and readable selection/error indicators work without relying solely on pointer input or color.

## Checks

- Check timeline coordinate conversion, snapping, left/right trim bounds and gesture undo/cancellation.
- Manually exercise cross-track moves, OS/browser drops, loading/failure states, scrolling and zoom during playback.
- Verify moving/deleting audible clips preserves unaffected tracks and the playhead, including undo/redo.

## Exclusions

No clip launching, stretch/warp, fades editor, MIDI, multi-selection, grouping, automation or recording. Transition gain ramps are an engine behavior, not editable clip fades.
