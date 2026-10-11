# Task 08: Validation and release readiness

## Objective

Demonstrate that milestone 0.01 works across the target platforms and document measured performance and remaining limitations.

## Dependencies

- Tasks [01](01-project-foundation.md), [02](02-project-model-and-editing.md), [03](03-project-storage-and-import.md), [04](04-audio-playback-engine.md), [05](05-live-arrangement-updates.md), [06](06-application-shell-and-project-browser.md) and [07](07-timeline-and-clip-editing.md).
- Focused checks belong to their implementation tasks; this task integrates them and validates complete workflows.

## Implementation requirements

- Run focused checks and application builds on macOS, Windows and Linux through CI. Distinguish successful compilation from hardware playback verification.
- Generate small deterministic audio fixtures for scheduling/mixing checks rather than committing large external media.
- Validate project round trips, portable paths, malformed manifests, missing media, asset name collisions, failed saves and edits made during saves.
- Validate overlap mixing, gaps, trims, block-interior boundaries, mono/stereo mapping, volume/mute and differing source/device sample rates.
- Stress playback edits: move/trim/delete, track removal, undo/redo, rapid dragging, repeated imports, missed preparation deadlines and queue saturation.
- Verify project replacement, device reconfiguration and shutdown while work is pending do not apply stale results or leak/destroy callback-owned resources.
- Run supported sanitizer checks and instrument application callback paths for unexpected allocation/destruction. Do not assume passing audible smoke checks establishes thread safety.
- Measure a Release build with 16 simultaneously playing stereo tracks, 48 kHz and a 256-sample buffer for ten minutes while repeatedly editing clips.
- Record OS, hardware, audio device/backend, storage, actual device settings, CPU, memory, underrun count and live-edit activation latency. Measure latency from session revision submission to engine acknowledgement.
- Target zero underruns on the measured machine and bounded memory/queued work. If the target fails, profile and fix the relevant path before describing the release as meeting it.
- Perform hardware playback smoke checks on each available platform. Explicitly list platforms that have only CI build/check coverage.
- Update README with build/run instructions, project portability, edit/import behavior, first-release limitations and measured results. Include license/dependency notices.
- Produce ordinary build artifacts for evaluation; do not publish a release or claim signing/notarization without the corresponding work and authorization.

## Milestone acceptance scenario

1. Create a new project and import multiple supported files, including mixed sample rates.
2. Arrange several tracks and start playback.
3. Move, trim and delete audible clips; change mute/volume; undo and redo without resetting transport or disrupting unaffected tracks.
4. Import another file while playing and verify its loading/readiness behavior.
5. Save and Save As while playing, including a newer edit made during a save.
6. Stop, close, move the saved project directory and reopen it with timing, controls and media preserved.
7. Remove one saved media file and reopen; verify the missing clip is visible/silent while other tracks remain usable.

## Completion evidence

- Green platform builds/checks, with any unavailable hardware checks clearly identified.
- Recorded end-to-end workflow results and the ten-minute performance measurement.
- No known callback waits for background work, application callback allocations, unsafe resource retirement or unbounded edit queue.
- Reproducible issue descriptions for unresolved failures; do not mark the milestone complete while required acceptance behavior is broken.

## Exclusions and risks

Recording, MIDI, Session-style launching, stretching, automation, export and VST3 hosting are outside milestone 0.01. VST3 effects are a separate follow-up with discovery, editors, bypass and saved state; plugin crash isolation requires its own design.

Live updates can be delayed by preparation, and seeks can interrupt sound briefly. Performance measurements apply to the tested hardware, not every device or platform.
