# Changelog

All notable changes to CortexFlow are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
under the v0.x posture defined in [ADR-0023](docs/adr/0023-release-packaging-strategy.md):
in v0.x, minor bumps may break consumers and patch bumps are bugfix-only.

The release surface (Core API, Platform, Build) is defined in
[CONTEXT.md](CONTEXT.md#release-surface). Changes outside the release
surface are not breaking and do not require a version bump.

## [Unreleased]

### Added

### Changed

### Deprecated

### Removed

### Fixed

### Security

## [0.1.0] - YYYY-MM-DD

First tagged release. The framework is consumable as a CMake sub-project
via `FetchContent` (or vendoring as a drop-in fallback), in v0.x posture
pending the architectural-spine ADRs (001–019).

### Added

- Apache-2.0 license declared in top-level `LICENSE`; `AUTHORS` file
  enumerates copyright holders under the collective string
  "The CortexFlow Authors"; two-line SPDX headers on every `.hpp` and
  `.cpp` in the tree.
- `FetchContent` consumption contract: a consumer pins a `vX.Y.Z` tag,
  sets `CORTEXFLOW_TARGET` as a cache variable before
  `FetchContent_MakeAvailable`, then links the `cortexflow` target.
  Smoke-tested by an in-tree consumer at
  `tests/integration/fetchcontent_consumer/` that runs on every CI build.
- `CORTEXFLOW_BUILD_EXAMPLES` cache option, defaulting `ON` when
  CortexFlow is the top-level project and `OFF` when consumed as a
  sub-project, so `examples/minimal_app` no longer leaks into a
  consumer's build by default.
- Release surface vocabulary (Core API, Platform, Build) documented in
  `CONTEXT.md`, defining what CortexFlow promises to keep stable within
  a major version.
- `CHANGELOG.md` (this file), `docs/releasing.md` runbook, and
  `.github/workflows/release.yml` tag-triggered workflow: cutting a
  release is now a documented manual process plus an automated matrix
  run and GitHub Release publication.

### Changed

- Sub-project hygiene: `-fno-rtti` and `-fno-exceptions` are now
  PRIVATE compile options on the `cortexflow` target (see
  [ADR-0022](docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md)),
  so consumers that use exceptions or RTTI in their own translation
  units are no longer forced to disable them. The per-target CMake
  files under `cmake/targets/` resolve paths against
  `${PROJECT_SOURCE_DIR}` rather than `${CMAKE_SOURCE_DIR}`, so
  CortexFlow's tree is found correctly under sub-project consumption.
- `project(cortexflow ...)` declares `VERSION 0.1.0`, making the
  top-level `CMakeLists.txt` the single source of truth for the
  release version. Consumers can read `${cortexflow_VERSION}` after
  `FetchContent_MakeAvailable`.

### Supported platforms

- `host` (development-host backend) and `posix` (POSIX-conforming
  systems) are in-tree and exercised in CI on Linux with both `gcc`
  and `clang`.
- `freertos` and `bare_metal` are reserved placeholders — the
  directories exist but contain no implementation. Their absence is a
  documented gap, not a release blocker.

[Unreleased]: https://github.com/jmendoza-10/cortexflow/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/jmendoza-10/cortexflow/releases/tag/v0.1.0
