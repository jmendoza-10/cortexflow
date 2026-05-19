# CortexFlow

A C++ framework for embedded control-plane state management.

Three composable primitives plus a shared data cache:

- **Runtime** — owns the event loop, message queue, data cache, timer service, and every module instance.
- **Modules** — typed classes that handle messages and (optionally) host a Flow. One instance per type.
- **Flows** — re-entrant state machines built from function pointers that return the next state to enter.
- **Smart data cache** — typed key-value store with RAII subscriptions.

## Status

CortexFlow is in **v0.x development** — the library, an example app, and the host + posix backends are implemented and exercised by a passing CI matrix (host + posix × gcc + clang), while the architectural spine (ADRs 001–019, reserved in [`docs/adr/`](docs/adr/)) is still being written out before graduation to `v1.0.0`. See [`docs/architecture.md`](docs/architecture.md) for the full design, [ADR-0023](docs/adr/0023-release-packaging-strategy.md) for the release-packaging contract, and [CONTEXT.md → *Release surface*](CONTEXT.md#release-surface) for what is and isn't stable.

## Targets

- Host (development & tests)
- POSIX / embedded Linux
- FreeRTOS / Zephyr
- Bare-metal MCUs

## Where to start

1. Read [`docs/architecture.md`](docs/architecture.md) — the complete design reference.
2. Skim [`docs/adr/`](docs/adr/) — short rationale documents for each major decision.
3. The repo skeleton mirrors section 15 of the architecture doc.

## Consuming CortexFlow

CortexFlow is consumed as a source dependency via CMake's `FetchContent` against a tagged release. Vendoring (`add_subdirectory` against a copied source tree) is supported as a drop-in fallback. Prebuilt binaries and `find_package(cortexflow CONFIG)` installs are not currently provided — see [ADR-0023](docs/adr/0023-release-packaging-strategy.md).

### FetchContent

Replace `<owner>` with the GitHub user or organization hosting the repository:

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

`CORTEXFLOW_TARGET` selects the **Platform backend** (the per-target tree under `platform/<target>/` that provides the typedef-swapped `<cortexflow/platform.hpp>`; see [CONTEXT.md → *Platform backend*](CONTEXT.md)). The accepted values in v0.1.0:

| Value         | Status                                          |
|---------------|-------------------------------------------------|
| `host`        | Implemented; default for top-level development. |
| `posix`       | Implemented.                                    |
| `freertos`    | Placeholder; not implemented in v0.1.0.         |
| `bare_metal`  | Placeholder; not implemented in v0.1.0.         |

The `set(... CACHE STRING "" FORCE)` form is required: CMake cache variables set without `FORCE` after a previous configure do not override a stale value, and the `FORCE` makes the consumer's choice of Platform backend authoritative regardless of any prior configure state.

### Vendoring (fallback)

For consumers whose policy forbids `FetchContent` against external repositories, drop the source tree under `third_party/cortexflow/` and pull it in with `add_subdirectory`:

```cmake
set(CORTEXFLOW_TARGET posix CACHE STRING "" FORCE)
add_subdirectory(third_party/cortexflow)

target_link_libraries(my_app PRIVATE cortexflow)
```

The Platform backend selection idiom is identical to the FetchContent path.

### Exceptions and RTTI

CortexFlow's library code is compiled with `-fno-rtti -fno-exceptions` (PRIVATE), but those flags do not propagate to consumers — your own code is free to use exceptions and RTTI. See [ADR-0022](docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md).

### Stability

The set of files, types, and CMake targets CortexFlow promises to keep stable is the **Release surface** — see [CONTEXT.md → *Release surface*](CONTEXT.md#release-surface) for the Core API / Platform / Build surface breakdown and [ADR-0023](docs/adr/0023-release-packaging-strategy.md) for the full v0.x posture, semver rules, and graduation criterion to `v1.0.0`.

## Build and test

Requirements: CMake ≥ 3.20, a C++17 compiler (GCC or Clang), Ninja or Make. Python 3 with `pytest` is required to run the diagram-tooling tests.

### Host build (default)

```sh
cmake -S . -B build -DCORTEXFLOW_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This builds the `cortexflow` static library, the `minimal_app` example, and the doctest-based unit + integration tests, then runs every test registered with CTest (including the compile-fail suite).

### Cross-target builds

Select a platform backend with `-DCORTEXFLOW_TARGET=<name>` (`host`, `posix`, `freertos`, `bare_metal`):

```sh
cmake -S . -B build-posix -DCORTEXFLOW_BUILD_TESTS=ON -DCORTEXFLOW_TARGET=posix
cmake --build build-posix -j
ctest --test-dir build-posix --output-on-failure
```

### ThreadSanitizer build (host only)

```sh
cmake -S . -B build_tsan -DCORTEXFLOW_BUILD_TESTS=ON -DCORTEXFLOW_ENABLE_TSAN=ON
cmake --build build_tsan -j
ctest --test-dir build_tsan --output-on-failure
```

### Trace verbosity

`-DCORTEXFLOW_TRACE_LEVEL=<OFF|ERROR|WARN|INFO|DISPATCH|FULL>` controls compile-time trace filtering (default `DISPATCH`). Use `FULL` for the noisiest output when debugging tests.

### Diagram-tooling tests (Python)

The flow-diagram extractor under `scripts/` has its own pytest suite:

```sh
pytest
```

Run from the repo root; `pytest.ini` points it at `scripts/tests/`.

### Run everything CI runs

```sh
cmake -S . -B build -DCORTEXFLOW_BUILD_TESTS=ON -DCORTEXFLOW_TRACE_LEVEL=FULL
cmake --build build -j
ctest --test-dir build --output-on-failure
pytest
python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp && git diff --exit-code docs/diagrams/
```

The last line is the drift guard CI enforces — it must report no diff.

## Layout

```
include/cortexflow/   public API (header-only core)
src/cortexflow/       non-template implementations
platform/<target>/    per-target backends (typedef-swapped at build time)
cmake/targets/        per-target CMake configurations
tests/                doctest-based unit and integration tests
examples/minimal_app/ reference application
docs/                 architecture and ADRs
```
