# Task 04: Audio playback engine

## Objective

Play multi-track audio arrangements with bounded memory and a shared sample clock, independently of the UI.

## Dependencies

- [Task 02: Project model and editing](02-project-model-and-editing.md)
- Task 03 supplies project assets for end-to-end integration; engine checks can use generated fixtures before it is complete.

## Implementation requirements

- Use JUCE device management and select the default stereo output initially. Expose output device, sample rate and buffer size settings.
- `AudioEngine` owns transport, devices and active playback resources. Its public controls cover play, stop, seek and gain/mute targets, with atomic telemetry for the UI.
- Use one device-sample clock to schedule all tracks. Compile clip timing into sample positions outside the callback and render half-open clip intervals against each output block.
- Mix overlapping clips additively, duplicate mono into stereo, preserve stereo channels and correct differing source sample rates without changing source pitch or speed.
- Reuse JUCE read-ahead buffering and resampling. Prepare readers, scratch buffers and resource ownership off the callback; do not load entire long files into memory.
- Keep application allocation, file I/O, decoding, UI operations, resource destruction and waits for background jobs outside the callback. Audit the chosen JUCE paths for callback behavior rather than assuming every framework operation is safe there.
- If source data is unavailable, render silence for the unavailable region and increment an underrun counter; never wait for disk.
- Smooth track/master gain and mute transitions. Master output remains an additive mix with a clipping indicator, not an implicit limiter.
- Play starts/resumes at the cursor. Stop holds the cursor; playback stops at the arrangement end. Space is a UI binding for the same transport operation.
- Seek while playing prepares source positions/buffers asynchronously, fades out, and resumes at the requested position once ready. Seeking can interrupt sound briefly; ordinary editing must not stop the clock.
- Device changes stop playback and reprepare for the actual device rate/block size. Report device errors and allow selection of another output.
- Stop callbacks before destroying voices/readers, then stop background workers. Document the ownership and thread contract at the engine interface.
- Keep block rendering callable without a physical device for deterministic checks and future offline export.

## Acceptance criteria

- Multiple tracks remain synchronized across blocks, including clip starts/ends inside a block.
- Trimmed clips play from their source offsets at original speed.
- Stop/resume, seek and arrangement-end behavior are deterministic.
- Long-file memory usage depends on buffering and waveform caches, not complete decoded file length.
- Read failures or device changes do not crash or leave dangling resources.

## Checks

- Generate fixtures for gaps, overlaps, trims, mono/stereo mapping, gains, mute and mixed sample rates.
- Check exact block-boundary scheduling and allow a stated numerical tolerance for resampling.
- Exercise source starvation, repeated transport commands and resource shutdown.

## Exclusions

No recording, plugin hosting, MIDI, export, loop playback or universal low-latency guarantee. Task 05 adds structural edits during playback.
