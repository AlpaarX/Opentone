# Task 01: Project foundation

## Objective

Establish a native Opentone desktop application and reproducible builds for macOS, Windows and Linux. This milestone delivers an audio arrangement editor with editing during playback.

## Dependencies

None. The workspace currently has no application code.

## Implementation requirements

- Use C++17 and CMake 3.22 or newer. Fetch JUCE 8.0.15 pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`; do not track a moving branch.
- Build a standalone desktop application with JUCE UI and audio modules. Do not add a web frontend or service.
- Publish application source under AGPLv3 and include JUCE and other dependency notices.
- Establish small components for the project model/session, storage/import, playback preparation/engine and UI. Introduce their code as the dependent tasks require it, rather than creating empty abstractions.
- Keep project data independent of UI components and audio devices. The application owns the session and engine; the session coordinates them.
- Define build presets for Debug and Release and ignore downloaded dependencies, local settings and build output.
- Add a CI matrix for macOS, Windows and Linux that configures and builds the application. Add the focused check target to CI when task 02 introduces it.
- Document prerequisites and configure/build/run commands for each platform, including Linux system libraries required by the selected JUCE modules.
- Keep all build and repository changes inside Opentone; the surrounding directory may belong to a separate Git repository.

## Acceptance criteria

- A clean checkout configures and builds an application that opens and closes a desktop window.
- CI builds the same pinned dependency version on all three platforms.
- Build instructions, license and dependency notices are present.
- Audio resources and workers introduced later have explicit owners and shutdown order.

## Checks

- Configure and build Debug and Release locally.
- Verify the platform build jobs on CI; report any jobs that cannot be run yet.

## Exclusions

No plugin hosting, installer signing, auto-updater, generic event bus or dependency injection framework. Performance is not established by the choice of framework.
