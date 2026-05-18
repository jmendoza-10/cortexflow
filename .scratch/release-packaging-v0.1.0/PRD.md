# Release packaging: ship CortexFlow v0.1.0 as a FetchContent-consumable dependency

Status: ready-for-agent
ADRs: [`docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md`](../../docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md), [`docs/adr/0023-release-packaging-strategy.md`](../../docs/adr/0023-release-packaging-strategy.md)

## Problem Statement

CortexFlow is not consumable as a dependency by another C++ codebase today. There is no license file, so a consumer's legal team cannot approve the dependency. There are no git tags, so a consumer cannot pin a deliberate version in their build. There is no published changelog, so a consumer cannot tell a bugfix release from a breaking one. The top-level CMake build assumes it is the parent project — `examples/minimal_app` is added unconditionally, the per-target files resolve paths against `${CMAKE_SOURCE_DIR}` (which becomes the *consumer's* top-level when CortexFlow is included as a sub-project), and `-fno-rtti -fno-exceptions` are propagated as PUBLIC compile flags that contaminate every target in the consumer's build that links the cortexflow library. The README still describes the project as "Pre-implementation."

The concrete consequence: the framework author wants to use CortexFlow's state-machine and runtime primitives in a separate pure-CMake application codebase ("$WORK"), and there is no clean path. Either $WORK can't legally use the code, or it can but inherits CortexFlow's compile flags across its entire build, or the build silently breaks because sub-project paths resolve wrong. Each of those failure modes blocks adoption.

## Solution

Tag a `v0.1.0` source release on the GitHub repository. Make it consumable via CMake's `FetchContent` against that tag, with vendoring (a copied source tree plus `add_subdirectory`) as a drop-in fallback. The consumer selects the **Platform backend** (CONTEXT.md) at their own configure step by setting `CORTEXFLOW_TARGET` as a cache variable before `FetchContent_MakeAvailable`:

```cmake
include(FetchContent)
FetchContent_Declare(
  cortexflow
  GIT_REPOSITORY https://github.com/<owner>/cortexflow.git
  GIT_TAG        v0.1.0
)
set(CORTEXFLOW_TARGET posix CACHE STRING "" FORCE)
FetchContent_MakeAvailable(cortexflow)

target_link_libraries(my_app PRIVATE cortexflow)
```

That is the full consumption contract.

The release is licensed Apache-2.0, with a top-level `LICENSE` file, an `AUTHORS` file naming "The CortexFlow Authors," and a two-line SPDX header (`Copyright YYYY The CortexFlow Authors` + `SPDX-License-Identifier: Apache-2.0`) on every `.hpp`/`.cpp` in the tree. Versioning follows semver with a documented **v0.x posture**: minor bumps may break consumers; patch bumps are bugfix-only; graduation to `v1.0.0` happens when the architectural spine (ADRs 001–019, currently reserved-but-unwritten) is fully documented.

The release surface — the union of files, types, and CMake targets that CortexFlow promises to keep stable — is captured in CONTEXT.md as **Core API surface** (`include/cortexflow/*.hpp`), **Platform surface** (`platform/<target>/cortexflow/platform.hpp`), and **Build surface** (the `cortexflow` CMake target and the `CORTEXFLOW_TARGET` cache variable). Everything else (`src/cortexflow/` internals, examples, tests, scripts, directory layout, other cache variables) is explicitly *not* on the release surface and may change between any two tagged releases.

When consumed as a sub-project, CortexFlow no longer leaks targets or compile flags into the parent build. `examples/` is gated behind a new `CORTEXFLOW_BUILD_EXAMPLES` option that defaults `ON` only when CortexFlow is the top-level project and `OFF` otherwise. `-fno-rtti -fno-exceptions` become PRIVATE (see ADR-0022) so the consumer's other code is free to use exceptions and RTTI. The per-target CMake files resolve paths against `${PROJECT_SOURCE_DIR}` (CortexFlow's tree) rather than `${CMAKE_SOURCE_DIR}` (the consumer's tree).

Cutting a release is a documented manual process plus a tag-triggered CI workflow. The human bumps the version in the `project()` declaration, finalizes the `[X.Y.Z] - YYYY-MM-DD` section in `CHANGELOG.md` (Keep-a-Changelog format), commits, tags `vX.Y.Z`, and pushes. The workflow runs the existing build/test matrix against the tagged commit and populates the GitHub Release with notes extracted from `CHANGELOG.md`. No prebuilt binaries are published — the source release is the artifact.

The work ships as six independently-grabbable slices and concludes with the actual `v0.1.0` tag.

## User Stories

### Consuming CortexFlow

1. As a consumer integrating CortexFlow into a pure-CMake codebase, I want to pull the library into my build with a single `FetchContent_Declare` stanza, so adoption is one CMake block instead of a manual vendor-copy.
2. As a consumer, I want to pin my dependency to a specific tag (e.g. `v0.1.0`), so my build is reproducible and upgrades are deliberate.
3. As a consumer, I want to pick the **Platform backend** at my own configure step by setting `CORTEXFLOW_TARGET` before `FetchContent_MakeAvailable`, so I don't have to fork CortexFlow to swap targets.
4. As a consumer whose application uses exceptions and RTTI freely, I want CortexFlow's compile flags to apply only to CortexFlow's own translation units, so adopting the framework doesn't force me to disable exceptions in unrelated code.
5. As a consumer, I want CortexFlow's `examples/minimal_app` target *not* to appear in my build by default, so my IDE index, generator output, and `cmake --build` invocations are not cluttered with code I do not ship.
6. As a consumer, I want CortexFlow's tests not to compile by default, so my CI does not run the framework's own test suite unless I opt in.
7. As a consumer whose corporate policy forbids `FetchContent` against external repos, I want vendoring (a copied source tree consumed via `add_subdirectory`) to work as a drop-in fallback, so I can adopt CortexFlow even in locked-down build environments.
8. As a consumer, I want exactly one canonical README section that documents the consumption contract — the FetchContent stanza, the `CORTEXFLOW_TARGET` cache-variable idiom, the target name to link — so I don't have to reverse-engineer the integration from the CMake source.
9. As a consumer, I want CortexFlow's version to be available as `${cortexflow_VERSION}` after `FetchContent_MakeAvailable`, so my build can branch on or assert against the version it linked.

### Versioning and stability

10. As a consumer pinned at `v0.1.0`, I want to know that any `v0.1.Z` patch release is bugfix-only, so I can take patch upgrades automatically without re-reading the CHANGELOG.
11. As a consumer pinned at `v0.1.0`, I want to know that a `v0.2.0` minor release may break my integration, so I read the CHANGELOG before bumping.
12. As a consumer, I want CortexFlow's release surface — the public headers, the per-target platform header, the CMake target — to be explicitly documented, so I know which kinds of upgrades require code changes.
13. As a consumer, I want a written CHANGELOG that says what changed between releases, so I do not have to read `git log` to understand a version bump.
14. As the framework maintainer, I want to operate in v0.x mode (minor bumps may break) until the architectural spine ADRs (001–019) are written, so I do not pretend the API is stable before it is.
15. As the framework maintainer, I want a clear graduation criterion to `v1.0.0` — all reserved-spine ADRs written — so the v0.x → v1.0 transition is signal-based rather than mood-based.

### Licensing and compliance

16. As $WORK's legal counsel, I want CortexFlow declared under Apache-2.0 with the full license text in a top-level `LICENSE` file, so adoption clears procurement and the patent-grant clause protects the company.
17. As $WORK's compliance team, I want every CortexFlow source and header file to carry an `SPDX-License-Identifier: Apache-2.0` line, so our existing license-scanning tooling reports accurate attribution.
18. As $WORK's compliance team, I want an `AUTHORS` file at the repo root listing copyright holders, so the attribution chain is documented for vendored copies.
19. As a future contributor, I want a single copyright holder string ("The CortexFlow Authors") referenced from all SPDX headers, so adding myself to the project is one line in `AUTHORS` rather than a sweep of every file.

### Sub-project hygiene

20. As a consumer doing `FetchContent_MakeAvailable(cortexflow)`, I want CortexFlow's per-target CMake files to resolve their paths against CortexFlow's tree, not my top-level project's tree, so the build does not silently look for sources in the wrong place.
21. As a consumer, I want CortexFlow's `-fno-rtti -fno-exceptions` flags to apply only to CortexFlow's own translation units (see ADR-0022), so the flags do not propagate into my own targets via target inheritance.
22. As a consumer doing top-level CortexFlow development, I want `examples/minimal_app` to still build by default when I configure CortexFlow as the top-level project, so the example is exercised in the framework's own workflow.
23. As a consumer doing top-level CortexFlow development, I want a `CORTEXFLOW_BUILD_EXAMPLES` cache option, so I can opt out of building the example even at top level if I'm only iterating on framework internals.

### Release mechanics

24. As the framework maintainer, I want the version number to live in the top-level `project(cortexflow VERSION X.Y.Z LANGUAGES CXX)` declaration, so there is exactly one source of truth visible to CMake, consumers, and tooling.
25. As the framework maintainer, I want a `CHANGELOG.md` in Keep-a-Changelog format with an `[Unreleased]` section that accumulates entries between releases, so the cut-a-release step is a rename rather than a write-everything-from-scratch.
26. As the framework maintainer, I want a `docs/releasing.md` runbook with the manual steps (bump `project()` version, finalize CHANGELOG, commit, tag, push), so cutting `v0.2.0` in three months does not depend on remembered tribal knowledge.
27. As the framework maintainer, I want a tag-triggered GitHub Actions workflow that runs the existing build/test matrix against the tagged commit and populates the GitHub Release with notes extracted from `CHANGELOG.md`, so a tag means "matrix is green, Release is published, notes match the source of truth."
28. As the framework maintainer, I want to verify the release workflow against a pre-release tag (`v0.1.0-rc1`) before cutting the real `v0.1.0`, so the first real Release isn't the first time the workflow runs.

### Documentation

29. As a future contributor reading `docs/adr/`, I want the release-packaging and PRIVATE-flags decisions captured as ADRs (0022, 0023), so the rationale outlives the author's memory.
30. As a framework reader, I want CONTEXT.md to define the **Release surface** vocabulary (Core API, Platform, Build), so "is this a breaking change?" has a documented answer.
31. As a framework reader, I want the README's stale "Pre-implementation" status line removed, so the project's documented status reflects its actual state.

## Implementation Decisions

The work is six independently-grabbable slices. Each issue under `issues/` is self-contained with its own acceptance criteria.

### Slice 1 — License + SPDX rollout

Apply Apache-2.0 across the repo: top-level `LICENSE` file with the canonical Apache-2.0 text, a top-level `AUTHORS` file with the project author as the first line under the copyright holder string "The CortexFlow Authors," and a two-line SPDX header inserted at the top of every `.hpp` and `.cpp` file in the tree:

```
// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
```

Existing copyright-style banners (if any) are replaced; license attribution is conveyed exclusively via SPDX. No file carries the full Apache-2.0 boilerplate — that lives once in `LICENSE`.

### Slice 2 — CMake hygiene for sub-project consumption

Five targeted edits to the CMake tree, all driven by ADR-0023:

- Flip `-fno-rtti -fno-exceptions` from PUBLIC to PRIVATE on the `cortexflow` target (ADR-0022).
- Add `VERSION 0.1.0` to the top-level `project(cortexflow ...)` declaration, making it the single source of truth.
- Gate `add_subdirectory(examples/minimal_app)` behind `option(CORTEXFLOW_BUILD_EXAMPLES "Build examples" <default>)`, where the default is `ON` only when CortexFlow is the top-level project (detected via `CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR`) and `OFF` otherwise.
- In `cmake/targets/host.cmake` and `cmake/targets/posix.cmake`, replace `${CMAKE_SOURCE_DIR}` with `${PROJECT_SOURCE_DIR}` so paths resolve to CortexFlow's tree under sub-project consumption.
- The existing `CORTEXFLOW_BUILD_TESTS` option remains opt-in (no change).

### Slice 3 — FetchContent consumption smoke test

A standalone CMake project that exercises the full FetchContent contract from a consumer's perspective. Lives under the repo (proposed: `tests/integration/fetchcontent_consumer/`) as a small, self-contained CMake tree whose top-level `CMakeLists.txt` does a `FetchContent_Declare` against the CortexFlow tree itself (using a local path or the current commit, not a remote tag — so the test runs offline and on every commit), sets `CORTEXFLOW_TARGET=posix` via the documented `set(... CACHE STRING "" FORCE)` idiom, calls `FetchContent_MakeAvailable`, defines a tiny consumer target that links `cortexflow`, and *also* defines a sibling target compiled *with* exceptions enabled to prove the PRIVATE compile-flag posture works end-to-end.

The smoke test is the acceptance gate for Slice 2's invariants and is wired into CI so a regression in sub-project hygiene fails before it ships.

### Slice 4 — Release process artifacts

Three new files plus one new workflow:

- `CHANGELOG.md` at the repo root in [Keep-a-Changelog](https://keepachangelog.com/) format, with an `[Unreleased]` section and a populated `[0.1.0]` skeleton ready for the cut.
- `docs/releasing.md` documenting the manual cut-a-release runbook (bump `project()` version, move `[Unreleased]` entries under `[X.Y.Z] - YYYY-MM-DD`, commit, tag, push, verify workflow).
- `.github/workflows/release.yml` triggered on tag push (`v*.*.*`), running the existing build/test matrix from `.github/workflows/ci.yml` against the tagged commit, then creating/updating the GitHub Release with notes extracted from `CHANGELOG.md` for the matching version. A failed matrix run surfaces the failure but does *not* auto-yank the tag — the human cuts a new tag after fixing.

Verification of this slice is a pre-release `v0.1.0-rc1` tag pushed against the implemented workflow before Slice 6 cuts the real `v0.1.0`.

### Slice 5 — README consumer-facing documentation

Add a "Consuming CortexFlow" section to the README documenting the FetchContent stanza, the `CORTEXFLOW_TARGET` cache-variable idiom, the target name to link, the available platform backends, and the vendoring-as-fallback path. Drop the stale "Pre-implementation" status line at the top — replace with a one-sentence status that matches reality (the framework is implemented, tested under CI on host + posix with gcc + clang, in v0.x posture pending the spine ADRs). Link to ADR-0023 for the formal release-packaging contract and to CONTEXT.md → *Release surface* for the stability definition.

### Slice 6 — Cut v0.1.0

Final step. Finalize the `[0.1.0] - YYYY-MM-DD` entry in `CHANGELOG.md`, commit the changelog and any final touch-ups, tag `v0.1.0` with an annotated tag, push the tag, and verify the release workflow produced a green matrix run and a GitHub Release whose notes match the CHANGELOG entry.

### Cross-cutting constraints

- No work in this PRD adds, modifies, or removes anything in `include/cortexflow/*.hpp`. The **Core API surface** is unchanged by this release-packaging effort.
- No work in this PRD adds, modifies, or removes the `platform.hpp` of any in-tree backend. The **Platform surface** is unchanged.
- The `cortexflow` CMake target name does not change. The **Build surface** is unchanged.

Anything that would touch the release surface is out of scope for this PRD.

## Testing Decisions

A good test in this PRD verifies the **external consumption contract**, not the framework's internal behavior (which is exhaustively covered by the existing test suite under `tests/`). External behavior here means: from the perspective of a consumer doing `FetchContent_MakeAvailable(cortexflow)`, do the documented guarantees hold?

Two test surfaces are introduced:

### FetchContent consumer smoke test (Slice 3)

A standalone CMake project that *is* the test. It is structurally identical to what a real consumer would write, and it asserts the documented contract by attempting to compile and link. Specific behaviors it must exercise:

- A consumer can select `CORTEXFLOW_TARGET=posix` via `set(... CACHE STRING "" FORCE)` before `FetchContent_MakeAvailable` and the build picks the posix backend.
- A consumer's own target can be compiled with exceptions enabled and link `cortexflow` without conflict — proving the PRIVATE compile-flag posture (ADR-0022).
- `examples/minimal_app` does *not* appear as a buildable target in the consumer's build (proving sub-project gating works).
- The `cortexflow_VERSION` CMake variable is set and matches the version in the `project()` declaration.

Prior art: the existing integration tests live under `tests/integration/` and are wired into CTest via `tests/CMakeLists.txt`. The new smoke test lives as a sibling under `tests/integration/fetchcontent_consumer/` and runs as a CTest case that itself invokes a nested CMake configure+build. This pattern (nested CMake build as a CTest case) is the standard way to test CMake-export-and-consume contracts and is documented in CMake's own test suite.

### Release workflow dry-run (Slice 4 acceptance)

Before Slice 6 cuts the real `v0.1.0`, Slice 4 must be verified by pushing a `v0.1.0-rc1` pre-release tag against the implemented workflow. Acceptance is observational, not assertion-based: the workflow runs, the build matrix is green against the tagged commit, the GitHub Release for `v0.1.0-rc1` is created, and its notes contain the entries from the `[Unreleased]` (soon-to-be `[0.1.0]`) CHANGELOG section. The `-rc1` Release can be deleted afterwards or kept as a public pre-release marker — either is fine, this is verification of the workflow, not a shipped artifact.

### Explicitly not tested

- Vendoring (the fallback consumption path) is the same mechanism as FetchContent from `add_subdirectory`'s perspective and is not separately exercised. If FetchContent works, vendoring works.
- License-file content correctness is verified by `git diff` review at PR time, not by a runtime test. Tooling like REUSE-lint could be added later; out of scope for v0.1.0.
- SPDX-header presence on every source file is checked by a single shell-script test added as a CTest case in Slice 1 (a one-liner grep / find combination). Cheap regression guard.

## Out of Scope

- `install(TARGETS ...)` / `find_package(cortexflow CONFIG)` support. Deferred per ADR-0023; adding install rules later is non-breaking.
- Prebuilt binary distribution. CortexFlow ships only as source; consumers compile.
- Conan or vcpkg recipes. Third parties may publish recipes; no first-party recipe is shipped.
- `freertos` and `bare_metal` platform backends. The directories exist as placeholders but contain no implementation; they remain placeholders for v0.1.0. Their absence is a documented gap, not a release blocker.
- Backfilling ADRs 001–019 (the architectural spine). v0.1.0 ships in v0.x posture *because* those ADRs are not written; writing them is the gate for v1.0.0 and is its own multi-PRD effort.
- `cortexflow_VERSION` runtime introspection (a preprocessor macro readable from C++ code). Not required by any user story; the CMake variable suffices for the consumption contract. Easy to add later.
- Any change to `include/cortexflow/*.hpp`, `platform/<target>/cortexflow/platform.hpp`, or the `cortexflow` CMake target name. The release surface is held fixed across this PRD.
- Conventional-commits or commit-message-based changelog automation. CHANGELOG is maintained by hand per Slice 4.
- Code-of-conduct, contribution guide, issue-template, PR-template, security-policy files. Standard open-source repository hygiene that can land in a follow-up PRD without affecting consumption.

## Further Notes

**Graduation to v1.0.0** is governed by a documented criterion in ADR-0023: every reserved ADR slot in `docs/adr/README.md` (001–019) is a written, Accepted ADR. At that point the framework's architectural spine is documented in stable form and the source-API stability promise can be safely committed to across major versions. Until then, consumers operate in v0.x mode and pin specific tags. The CHANGELOG is the canonical place to surface the "is this breaking?" question to consumers between releases.

**The Release surface vocabulary** (CONTEXT.md → *Release surface*, *Core API surface*, *Platform surface*, *Build surface*) is the load-bearing concept that ties this PRD's stability story together. Any future work that proposes to change anything inside that surface should reference the vocabulary and explicitly characterize whether the change is patch-safe, minor-safe, or major-breaking. This PRD itself is patch-safe — none of the slices touch the release surface.

**The FetchContent smoke test (Slice 3)** is the most consequential piece of new code in this effort. It is also the closest thing this PRD has to a deep, testable module: its interface is "the documented consumption contract," it rarely changes, and it encapsulates the entire sub-project-consumption story in one place. Future regressions in CMake hygiene — a new option that leaks targets, a path that re-introduces `${CMAKE_SOURCE_DIR}`, a propagated flag — should manifest as this test failing.
