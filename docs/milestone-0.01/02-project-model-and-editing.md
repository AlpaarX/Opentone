# Task 02: Project model and editing operations

## Objective

Create the authoritative arrangement model and the operations used by the editor, persistence and playback preparation.

## Dependencies

- [Task 01: Project foundation](01-project-foundation.md)

## Implementation requirements

- Represent projects, ordered tracks, assets and clips with plain data and stable IDs. Clips reference a track and asset by ID; they do not own readers or source files.
- Store project BPM; track name, gain and mute; asset relative path, sample rate, channel count and source length; and clip timeline start, source offset and duration.
- Store timing in seconds. Audio sample positions are derived during playback preparation. Source sample metadata constrains trimming.
- Default to an empty project at 120 BPM in 4/4. Track gain defaults to unity and mute to false.
- Keep the editable model on the message thread. `ProjectSession` owns the model, undo manager, dirty state, project generation and monotonically increasing revision number.
- Expose explicit operations to add/rename/delete tracks, insert/move/trim/delete clips, set BPM and adjust volume/mute. UI components do not mutate project data directly.
- Reject non-finite values, nonpositive durations, negative starts/offsets, invalid references and trims beyond source bounds. Allow overlapping clips.
- Use JUCE undo facilities. One completed drag or control gesture is one undo action. Deleting a track also removes its clips; undo restores the track and clips with their original IDs.
- Continuous clip drags update the visual model and request coalesced playback revisions. Escape restores the pre-gesture state; release commits the final state to undo history.
- Volume/mute update audio targets without rebuilding readers. Structural changes and their undo/redo request new prepared arrangements without stopping playback.
- BPM changes alter the grid only; clip timing and original-speed audio remain unchanged.
- New/Open replace the project generation and reset undo history. Async work from a previous generation must be rejected.
- Successful save clears dirty state only if the saved generation/revision still matches the current project.

## Acceptance criteria

- Every edit passes through a session operation with validation, undo and dirty-state handling.
- Moving/trimming preserves asset references and never changes original audio.
- Undo/redo restores IDs, ordering and timing exactly, including track deletion.
- A newer edit made during a save remains marked unsaved after the older snapshot finishes saving.

## Checks

- Add focused checks for operation validation, clip move/trim, track deletion, undo/redo, gesture cancellation and revision progression.
- Verify BPM changes leave clip timing unchanged and saved-revision tracking preserves newer edits.

## Exclusions

No MIDI, automation, time stretching, variable time signatures or generic command framework beyond the actual operations.
