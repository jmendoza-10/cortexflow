# README: add "Consuming CortexFlow" section and retire stale status line

Status: merged
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 1, 2, 3, 7, 8, 12, 31
ADR: `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

The README currently describes the project as "Pre-implementation" and contains no documentation for consumers — anyone wanting to use CortexFlow in their own build has to reverse-engineer the contract from the CMake source. Slice 5 fixes both.

### Drop the stale status line

The current README's `## Status` section says CortexFlow is pre-implementation and points at `docs/architecture.md`. The framework now has an implemented library, a passing CI matrix (host + posix × gcc + clang), an example app, an integration test suite, and per-target backend support. Replace the existing status paragraph with a one-sentence statement that matches reality. Concretely: state the current posture (e.g. "CortexFlow is in v0.x development — the architectural spine is being documented as ADRs 001–019 before the v1.0.0 graduation"), keep the link to `docs/architecture.md`, and link to ADR-0023 for the formal release-packaging contract and CONTEXT.md → *Release surface* for the stability promise.

### Add a "Consuming CortexFlow" section

A new top-level section in the README (placement: after "Where to start" and before the existing "Build and test" section so the reading order is "what is this → where to learn → how do *I* use it → how do contributors build it"). It must document the full FetchContent consumption contract concretely.

Required contents:

1. **One-paragraph framing.** "CortexFlow is consumed as a source dependency via CMake's `FetchContent` against a tagged release. Vendoring (`add_subdirectory` against a copied source tree) is supported as a drop-in fallback. Prebuilt binaries and `find_package(cortexflow CONFIG)` installs are not currently provided — see ADR-0023."

2. **The FetchContent stanza** as a fenced `cmake` code block, exactly as it appears in the PRD's Solution section (the `FetchContent_Declare` + `set(CORTEXFLOW_TARGET ... CACHE STRING "" FORCE)` + `FetchContent_MakeAvailable` + `target_link_libraries` block). Use `https://github.com/<owner>/cortexflow.git` as the placeholder URL; do not bake in a specific owner/org name. Use `v0.1.0` as the tag (it is the first real tag this PRD ships).

3. **`CORTEXFLOW_TARGET` reference table.** A short table listing the in-tree backends and their status:

   | Value         | Status                                          |
   |---------------|-------------------------------------------------|
   | `host`        | Implemented; default for top-level development. |
   | `posix`       | Implemented.                                    |
   | `freertos`    | Placeholder; not implemented in v0.1.0.         |
   | `bare_metal`  | Placeholder; not implemented in v0.1.0.         |

4. **A note on why `set(... CACHE STRING "" FORCE)` is required** (one sentence): "CMake cache variables set without `FORCE` after a previous configure do not override a stale value; the `FORCE` makes the consumer's choice of backend authoritative regardless of any prior configure state."

5. **A vendoring fallback example.** A second fenced `cmake` block showing the equivalent flow for consumers who cannot use FetchContent — typically `add_subdirectory(third_party/cortexflow)` after copying the source tree in, with the same `set(CORTEXFLOW_TARGET ...)` step beforehand.

6. **A link to ADR-0023** for the full release-packaging contract, and to CONTEXT.md → *Release surface* for what is and isn't stable.

7. **A note on exceptions and RTTI** (one sentence linking to ADR-0022): "CortexFlow's library code is compiled with `-fno-rtti -fno-exceptions` (PRIVATE), but those flags do not propagate to consumers — your own code is free to use exceptions and RTTI."

### Do not touch

- The "Build and test" section added previously (it documents the framework-developer's workflow, not the consumer's).
- The "Targets" / "Layout" sections (still correct).
- The directory layout, the existing links to architecture.md and adr/.

## Acceptance criteria

- [ ] The `## Status` section's "Pre-implementation" wording is removed and replaced with a one-sentence statement matching the framework's actual state (in v0.x posture, spine ADRs in flight)
- [ ] A new `## Consuming CortexFlow` section exists in the README, placed after "Where to start" and before "Build and test"
- [ ] The Consuming section contains the FetchContent `cmake` code block including `FetchContent_Declare`, `set(CORTEXFLOW_TARGET ... CACHE STRING "" FORCE)`, `FetchContent_MakeAvailable`, and `target_link_libraries`
- [ ] The Consuming section contains the four-row `CORTEXFLOW_TARGET` reference table (host, posix, freertos, bare_metal) with accurate status text per the v0.1.0 reality
- [ ] The Consuming section contains a vendoring fallback example (a `cmake` block using `add_subdirectory`)
- [ ] The Consuming section links to ADR-0023 and to CONTEXT.md's *Release surface* terminology
- [ ] The Consuming section notes that PRIVATE compile flags mean consumers are free to use exceptions/RTTI, linking to ADR-0022
- [ ] No code-block placeholder (`<owner>` etc.) is left ambiguous — the FetchContent block contains a clearly-marked placeholder URL the consumer is expected to replace, not a real default owner string
- [ ] No content is added that contradicts ADR-0023 (e.g. claiming `find_package` is supported, claiming a prebuilt binary is available, etc.)
- [ ] The full build/test matrix still passes (no source changes in this slice, but verify the README change does not break any CI step that reads the README)

## Blocked by

Slices 1 ([License + SPDX](01-license-and-spdx-rollout.md)) and 2 ([CMake hygiene](02-cmake-hygiene-for-subproject-consumption.md)) must be merged first — the README change documents a contract that those slices implement. Writing the README before they ship would document behavior that does not yet exist.

Slice 3 (smoke test) is a nice-to-have-first for confidence but not a hard prerequisite for the README copy.

## Notes

- This slice is pure documentation. Do not change any CMake, code, or workflow files. If you find yourself wanting to, that work belongs in a different slice.
- The Consuming section is the consumer's entry point. Optimize it for "I am a consumer and want to integrate CortexFlow into my CMake build in the next 5 minutes." Anything that doesn't serve that goal — internal architecture, framework concepts, why the design is this way — belongs elsewhere in the docs and should be linked, not inlined.
- Use the exact term **Platform backend** when referring to the value of `CORTEXFLOW_TARGET` (this is the term defined in CONTEXT.md). Consistency with the glossary matters more here than English-prose fluidity.

## Comments

Built (2026-05-19, from afk worker):

- Replaced the `## Status` paragraph with a one-sentence v0.x posture statement that keeps the link to `docs/architecture.md` and adds links to ADR-0023 and CONTEXT.md → *Release surface*.
- Added a new `## Consuming CortexFlow` section between "Where to start" and "Build and test" containing:
  - One-paragraph framing that names FetchContent as primary, vendoring as fallback, and the deferred status of `find_package` / prebuilt binaries (referencing ADR-0023).
  - The full FetchContent `cmake` block (`FetchContent_Declare` + `set(CORTEXFLOW_TARGET ... CACHE STRING "" FORCE)` + `FetchContent_MakeAvailable` + `target_link_libraries`), with `https://github.com/<owner>/cortexflow.git` as a marked placeholder and `v0.1.0` as the tag.
  - The four-row `CORTEXFLOW_TARGET` reference table exactly as specified (`host`, `posix` implemented; `freertos`, `bare_metal` placeholders).
  - The one-sentence note on why `set(... CACHE STRING "" FORCE)` is required.
  - A vendoring `cmake` block using `add_subdirectory(third_party/cortexflow)` with the same backend-selection step.
  - An "Exceptions and RTTI" note linking to ADR-0022.
  - A short "Stability" subsection linking to CONTEXT.md → *Release surface* and ADR-0023.
- Used the exact glossary term **Platform backend** consistently when referring to `CORTEXFLOW_TARGET` values.

Notes for reviewer:

- Anchor link to `CONTEXT.md#release-surface` assumes default GitHub auto-anchoring of the `### Release surface` heading; verify it renders if the README is viewed on GitHub.
- The status sentence frames the spine ADRs as "being written out before graduation to `v1.0.0`" rather than asserting they are imminent — this matches ADR-0023's graduation criterion without overcommitting to a timeline.
- No CMake/code/workflow files were touched (slice is documentation-only per the issue's Notes). One commit on `agent/05-readme-consumer-docs`; the issue file's `Status:` flip and this Comments entry are intentionally uncommitted per the runner protocol.

— from afk worker, 2026-05-19
