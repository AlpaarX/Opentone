# Task 05: Live arrangement updates

## Objective

Allow import, move, trim, delete, undo and redo during playback while preserving the engine clock and unaffected audio.

## Dependencies

- [Task 04: Audio playback engine](04-audio-playback-engine.md)
- [Task 02: Project model and editing](02-project-model-and-editing.md)

## Implementation requirements

- Add `PlaybackPreparer` to consume immutable project snapshots identified by generation and revision. Coalesce requests into a bounded latest-request slot and discard superseded work.
- Separate immutable arrangement scheduling from mutable playback voices. Only the audio thread advances active voice cursors.
- Prepare new readers, read-ahead buffers, resamplers and transition storage outside the callback. Reuse unchanged clip voices, but never let the worker seek or mutate a voice already owned by the callback.
- Give each prepared update an expected base revision and an activation frame on the engine sample clock. Convert timing using the current device configuration generation.
- Publish through a bounded single-producer/single-consumer handoff. The callback checks generation, device configuration, base revision and readiness at a block boundary before adopting an update.
- Schedule activation far enough ahead for changed sources to be buffered at the source positions needed then. If its deadline is missed, reschedule/reprepare outside the callback; do not seek/read synchronously in the callback.
- An incompatible update is rejected and returned for cleanup; the worker prepares against the latest acknowledged active revision. The current arrangement continues playing while an update is pending.
- Preserve the engine clock and voices for unchanged clips. New or repositioned clips begin at the source offset corresponding to their timeline intersection at activation, not automatically from the beginning.
- Preallocate transition resources and use approximately 5 ms gain ramps for entering/leaving voices to reduce clicks. Do not replay clips that have already ended by activation.
- Hand retired/rejected arrangements and voices to a bounded background cleanup queue. Ownership transfer must not trigger callback-side reference-count deletion or large frees.
- Reserve cleanup capacity before adopting an update. If capacity is insufficient, defer adoption and preserve the active arrangement; never block or grow queues.
- Extend the arrangement end when adopting an update that adds later material. If a shortened arrangement ends before the current frame, stop at that adoption boundary.
- Session model edits appear immediately in the UI. Expose pending/applied revision status so loading or preparation is visible without presenting internal queue details.
- Volume/mute use the engine's smoothed targets. Structural edits, undo and redo all follow the same revision preparation path.
- New/Open and device changes stop playback, invalidate pending generations and safely retire their resources. Shutdown drains ownership after the callback has stopped.

## Acceptance criteria

- Moving/trimming/deleting an audible clip does not restart transport or interrupt unaffected tracks.
- Undo/redo during playback follows the same behavior as direct edits.
- Imported clips become audible when ready if they still intersect the playhead; expired clips do not play late.
- Rapid dragging has bounded queued work and memory. The final released position eventually becomes the active revision.
- Slow preparation/cleanup and stale updates preserve continuous playback of the last valid arrangement.

## Checks

- Deterministically exercise adoption at block boundaries, unchanged-voice continuity and changed-clip source positions.
- Stress rapid edits, revision/base mismatches, missed deadlines, generation changes and saturated queues.
- Check transport progress during edits, safe retirement, bounded memory and absence of application callback allocation/destruction.
- Exercise live updates under ThreadSanitizer where supported, alongside normal device-free checks.

## Exclusions

No promise that every intermediate drag position becomes audible or that preparation finishes within a fixed number of blocks. No lock-free framework for unrelated subsystems or plugin isolation.
