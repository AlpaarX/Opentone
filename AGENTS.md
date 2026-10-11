# Working on Opentone

The user tests changes first. Implement the complete request, then hand it over.
Do not turn each request into a long investigation, testing campaign, or series
of rebuilds.

## Implementation steps

1. Read the request and identify every requested behavior. Treat follow-up
   messages as additions or corrections to the current work.
2. Read the code directly involved and its callers. Use `rg` to find existing
   helpers and patterns. Do not audit the whole repository for a focused change.
3. If the relevant code is unclear, investigate until the behavior is understood.
   Refactor the affected code where necessary to make the requested change clear
   and reliable. Keep that refactor within the feature's scope.
4. Implement everything requested in one pass. Update connected model, playback,
   UI, persistence, and documentation code when the change requires it. Reuse
   existing components and keep the solution small.
5. Review the changed code once for completeness and obvious mistakes. Fix what
   that review actually finds; do not keep inventing possible edge cases or adding
   unrelated improvements.
6. When all requested work is ready, build once and launch Opentone for the user
   to test. Briefly describe what changed and how to try it. The user will report
   problems; fix those reported problems directly.

## Testing and rebuilding

- Do not routinely add automated tests or run test suites, smoke tests,
  benchmarks, profilers, or screenshot checks. The default is user testing first.
- Do not build repeatedly during implementation. Finish the requested changes
  before building.
- After completing all requested implementation work, build the required target
  once and launch Opentone. If that build fails, fix the reported compilation or
  linking errors and retry. Do not also build other configurations or run extra
  checks.
- Do not rebuild after documentation-only changes.
- Run automated tests or performance measurements when the user explicitly asks
  for them. If a higher-priority instruction requires a check, run only that check
  and explain the exception briefly.
- Report what was actually checked. Never describe code as tested when the user
  has not tested it and no relevant check was run.

## Scope and communication

- Finish the user's requested behaviors before adding polish. Do not add extra
  features, abstractions, dependencies, configuration, or speculative fixes.
- Preserve project data, undo behavior, and existing unrelated features. Do not
  close a running app or discard unsaved work to simplify a rebuild or launch.
- Make routine implementation choices independently. Ask for clarification only
  when missing information materially changes the requested behavior.
- Keep progress updates short and useful. Avoid long plans, repeated summaries,
  and narrating every internal implementation detail.
- End with a concise handoff: what changed, how to try it, and any known limitation
  that matters to the user. Do not claim completion while requested work remains.
- Do not delegate to other agents unless the user explicitly asks.

## Build location

For a requested normal launch, use the release app:

```sh
cmake --build build/release --parallel 2
open -n build/release/Opentone_artefacts/Release/Opentone.app
```

Use `build/refactor-dev` only when the user requests the live UI development
workflow. Do not build both configurations for a normal feature request.
