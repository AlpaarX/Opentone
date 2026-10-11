# Task 03: Project storage and audio import

## Objective

Make projects portable and protect existing projects and original audio from failed imports, loads and saves.

## Dependencies

- [Task 02: Project model and editing](02-project-model-and-editing.md)

## Implementation requirements

- Store a project as a directory containing a JSON `project.opentone` manifest and an `Audio/` directory. Use format version 1 for the initial schema.
- Persist project BPM, ordered track IDs/names/gain/mute, asset IDs/relative paths and clip IDs/track/asset/start/source-offset/duration. Read authoritative media metadata from the assets when opening.
- Keep output-device preferences outside project files. Do not serialize readers, waveform caches, workers or undo history.
- Implement New, Open, Save and Save As. Keep unsaved-project imports in an application-owned temporary workspace until the user chooses a project location.
- Accept mono/stereo WAV, AIFF and FLAC using JUCE readers. Reject unsupported, unreadable, empty or higher-channel-count media with a clear error.
- Copy external audio in the background under unique destination names. Validate and complete the copy before committing a playable asset and clip to the session.
- Reuse an existing project asset when it is dragged from the project browser. Different external files with the same name must not collide.
- Tag jobs with project generation and operation identity; closing/replacing the project invalidates pending work. Cancelled or failed jobs do not publish partial assets.
- Save a captured project snapshot without stopping playback. Write a temporary manifest in the destination directory and replace the manifest atomically only after all referenced asset copies succeed.
- Save As copies referenced media into a new project directory and switches the session location only on success. Do not replace an unrelated existing project without explicit overwrite confirmation.
- Treat each completed save as a revision-specific result so newer edits remain dirty.
- Validate schema/version, finite timing values, ID uniqueness and references before replacing the active project. Reject paths escaping the project directory, including symlink escapes.
- Missing or unreadable media preserves its clips as visible silent placeholders with a warning. Malformed manifests and unsupported versions leave the current project intact.
- Clip/track deletion never deletes asset files. Closing an unsaved temporary project only removes files owned by that temporary workspace.

## Acceptance criteria

- A saved project reopens after its directory is moved, with no dependency on original import locations.
- Save and Save As preserve arrangement timing, controls and asset references.
- Failed saves preserve the previous manifest and session location. Failed loads preserve the current project.
- Missing audio does not prevent the rest of a project from opening.
- Saving during playback neither stops the transport nor clears newer unsaved edits.

## Checks

- Check round trips, relative paths, duplicate filenames, missing media and unsupported versions.
- Check invalid references, path traversal, failed copies and failed manifest replacement.
- Check cancelled/stale imports and changes made while a save is in progress.

## Exclusions

No archive format, cloud storage, destructive media cleanup or automatic relinking. Version migrations are introduced when a real format change requires them.
