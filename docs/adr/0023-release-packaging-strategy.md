# ADR-0023: Release-packaging strategy for v0.1.0+

**Status:** Accepted
**Date:** 2026-05-18

## Context

CortexFlow needs to be consumable as a dependency in other C++ codebases — initially one specific pure-CMake codebase that wants to incorporate the state-machine and runtime primitives into a production application. Until now the project has had no license file, no git tags, no install rules, no changelog, no published consumption contract, and a CMake tree that quietly assumes it is the top-level project (unconditional `add_subdirectory(examples/minimal_app)`, `${CMAKE_SOURCE_DIR}`-rooted paths in the per-target files).

The framework is small, header-mostly, template-heavy, and uses a typedef-swap platform layer (see CONTEXT.md → *Platform backend*) selected at CMake configure time via `CORTEXFLOW_TARGET=<target>`. There are no shared-library considerations — the only artifact is a static lib whose ABI is dominated by template instantiations the consumer re-generates against their own module and cache-key lists on every build.

Many of the decisions in this ADR could have been deferred indefinitely. They are being made now, as a bundle, because (a) the consumer is real, (b) the choices are interrelated (license affects channel affects versioning affects what's stable), and (c) the v1 architectural spine (ADRs 001–019, currently unwritten — see [docs/adr/README.md](README.md)) is itself in flight, and the release strategy needs to be honest about that.

## Decision

CortexFlow ships as a **source release on the GitHub repository**, consumed via CMake's `FetchContent` against a tagged commit. No prebuilt binaries, no `find_package` config-mode install in v0.1.0, no Conan or vcpkg port.

### Consumption channel

- **Primary:** `FetchContent_Declare(cortexflow GIT_REPOSITORY ... GIT_TAG vX.Y.Z)` followed by `FetchContent_MakeAvailable(cortexflow)`. The consumer picks the platform backend at their own configure step by setting `CORTEXFLOW_TARGET` as a cache variable *before* `FetchContent_MakeAvailable`:

  ```cmake
  set(CORTEXFLOW_TARGET posix CACHE STRING "" FORCE)
  FetchContent_MakeAvailable(cortexflow)
  target_link_libraries(my_app PRIVATE cortexflow)
  ```

- **Fallback:** vendoring (drop the source tree into `third_party/cortexflow/`, then `add_subdirectory(third_party/cortexflow)`). Same configure-time backend selection. Identical contract to FetchContent.

- **Deferred:** `cmake --install` / `find_package(cortexflow CONFIG)`. Not shipped in v0.1.0. Reason: a single install can only contain one platform backend (the per-target `<cortexflow/platform.hpp>` occupies one slot on the include path), which forces a target-baked-in install layout that is surprising for consumers and answers a question no real user has asked yet. Adding install rules later is non-breaking.

### License

Apache-2.0, declared in a top-level `LICENSE` file. Every source and header file carries a two-line SPDX header:

```
// Copyright YYYY The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
```

A top-level `AUTHORS` file lists contributors, starting with the original author. The Apache patent grant is the load-bearing reason this is Apache-2.0 rather than MIT — it removes a class of legal friction when CortexFlow is adopted inside an employer's codebase.

### Versioning posture

Tags are `vMAJOR.MINOR.PATCH` (semver). The project enters at **`v0.1.0`** and remains in **v0.x mode** until the architectural spine is locked. v0.x mode means: any minor bump (`v0.1.0 → v0.2.0`) may break consumers; patch bumps are bugfix-only. Consumers pin a specific tag and accept that minor upgrades may require code changes.

**Graduation criterion to `v1.0.0`:** every reserved slot in [docs/adr/README.md](README.md#planned-initial-adrs) — ADRs 001–019 — is a written, Accepted ADR. At that point the v1 architectural spine is documented and the framework's load-bearing decisions are explicit enough to commit to stability.

### Stability promise

CortexFlow promises **source-level API compatibility** within a major version (once at v1.0.0+). It does **not** promise binary ABI — the static lib's ABI is template-dominated and re-instantiated on every consumer build. There is no SO-versioning to maintain.

The stable surface is the **Release surface** defined in CONTEXT.md, with three components:

- **Core API surface** — `include/cortexflow/*.hpp`. Source-level changes here are breaking.
- **Platform surface** — `platform/<target>/cortexflow/platform.hpp` for each shipped target. Per-target. Adding a new backend is non-breaking; removing an existing backend is breaking for consumers using that backend.
- **Build surface** — the `cortexflow` CMake target name, the include directories it propagates, the compile features it requires, the link dependencies it pulls in. The cache variable `CORTEXFLOW_TARGET` (and its accepted values) is part of the build surface; internals like `CORTEXFLOW_TRACE_LEVEL_VALUE` are not.

Everything else (`src/cortexflow/` internals, other files under `platform/<target>/`, `examples/`, `tests/`, `scripts/`, `.scratch/`, directory layout, cache variable names other than `CORTEXFLOW_TARGET`) is **not** on the release surface and may change between any two tagged releases without a version bump implication.

### Sub-project consumption contract

When CortexFlow is consumed as a sub-project (FetchContent or vendoring), it must not contaminate the parent build:

- `examples/` is gated behind `option(CORTEXFLOW_BUILD_EXAMPLES "Build examples" <default>)`, default `ON` only when CortexFlow is the top-level project, `OFF` otherwise.
- `tests/` remains opt-in via `CORTEXFLOW_BUILD_TESTS=ON` (unchanged from before this ADR).
- Per-target CMake files (`cmake/targets/*.cmake`) use `${PROJECT_SOURCE_DIR}` and not `${CMAKE_SOURCE_DIR}`, so paths resolve to CortexFlow's tree rather than the parent project's.

`-fno-rtti` and `-fno-exceptions` are PRIVATE compile options on the `cortexflow` target — see [ADR-0022](0022-private-compile-flags-for-rtti-and-exceptions.md).

### Cut-a-release mechanics

- **Version source of truth:** the top-level `project(cortexflow VERSION X.Y.Z LANGUAGES CXX)` call. Consumers can read it as `${cortexflow_VERSION}`.
- **Changelog:** `CHANGELOG.md` at the repo root, [Keep-a-Changelog](https://keepachangelog.com/) format, maintained by hand. The `[Unreleased]` section accumulates entries between releases and is renamed to `[X.Y.Z] - YYYY-MM-DD` at tag time.
- **Runbook:** `docs/releasing.md` documents the manual steps (bump `project()` version, update CHANGELOG, commit, tag, push).
- **Automation:** a tag-triggered GitHub Actions workflow runs the existing build/test matrix (host + posix, gcc + clang) against the tagged commit and populates the GitHub Release with notes extracted from CHANGELOG.md. A red tag does not auto-yank — the workflow surfaces the failure and the human re-tags after a fix. GitHub auto-attaches the source tarball; no other artifacts are published.

## Consequences

**Enables:**

- A consumer can adopt CortexFlow with five lines of CMake (`FetchContent_Declare`, `set(CORTEXFLOW_TARGET ...)`, `FetchContent_MakeAvailable`, `target_link_libraries`) and pin a specific version.
- The framework's public surface is finally written down (in CONTEXT.md as *Release surface*) and the headers under `include/cortexflow/` are the authoritative declaration of it — no separate "list of public headers" to drift from reality.
- A v0.x posture buys honesty: the spine ADRs are not written yet, and pretending the framework is stable at v0.1.0 would be a lie that costs us reputation later.

**Costs:**

- Consumers must rebuild from source every time. Acceptable for a small, template-heavy library; would not be acceptable for a large C++ codebase.
- The "no install" choice means a future consumer who wants a prebuilt or a system-package-style installation has to wait for that work to be done. Adding install rules later is non-breaking, but doing it well requires answering the per-target install layout question we deferred here.
- Every public header change is a `v0.x` minor bump until v1.0.0. This is fine because the framework is small; it would be painful if the public API grew to dozens of headers.

**Forbids:**

- Shipping prebuilt binaries from the CortexFlow repository. If a consumer wants that, they build and host the binary themselves.
- Breaking the Release surface within a patch version (once at v1.0.0+). A `vX.Y.Z → vX.Y.(Z+1)` bump may only fix bugs and add internal-only changes.
- Adding to `include/cortexflow/` without owning the stability promise that comes with it. A new public header is a new piece of the Core API surface.

## Alternatives considered

- **Ship `find_package` install rules in v0.1.0.** Add `install(TARGETS cortexflow EXPORT ...)` plus a generated `cortexflow-config.cmake`. Rejected for v0.1.0: the install layout question for a typedef-swap framework is non-trivial (per-target prefix vs target baked into one config file vs reworking the include scheme to be target-disambiguated), no real consumer has asked for it, and committing to a layout now risks shipping the wrong one. Reconsider when a second consumer with prebuilt-binary needs appears.

- **MIT license instead of Apache-2.0.** Rejected: Apache's explicit patent grant removes friction for adoption inside employer codebases; the cost is one extra file (`NOTICE`-style attribution) and a slightly longer SPDX header convention, both negligible. The patent grant is the load-bearing differentiator for the consumption context this ADR exists to enable.

- **Skip semver, use a single rolling tag (`latest`) or date-based tags.** Rejected: a consumer needs to pin a version that won't move under them. A floating tag breaks the "I want to upgrade deliberately" contract. Date-based tags work but obscure the breaking-vs-non-breaking distinction that semver makes visible.

- **Go straight to v1.0.0.** Rejected as dishonest: the v1 architectural spine (ADRs 001–019) is reserved-but-unwritten, and committing to API stability before those decisions are documented would either freeze choices prematurely or force a `v2.0.0` shortly after `v1.0.0`. The v0.x → v1.0 graduation criterion ties the version-number promise to a documentation milestone we actually control.

- **Conan or vcpkg recipe.** Rejected for v0.1.0: adds an external dependency on a package manager and a recipe to maintain; offers nothing FetchContent does not already give us for this consumer. A third-party recipe in the upstream Conan or vcpkg index could happen later without our involvement.

- **Make `-fno-rtti -fno-exceptions` PUBLIC and treat "no exceptions" as a hard cross-codebase contract.** See [ADR-0022](0022-private-compile-flags-for-rtti-and-exceptions.md) for the dedicated rejection rationale.
