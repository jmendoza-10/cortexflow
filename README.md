# CortexFlow

A C++ framework for embedded control-plane state management.

Three composable primitives plus a shared data cache:

- **Runtime** — owns the event loop, message queue, data cache, timer service, and every module instance.
- **Modules** — typed classes that handle messages and (optionally) host a Flow. One instance per type.
- **Flows** — re-entrant state machines built from function pointers that return the next state to enter.
- **Smart data cache** — typed key-value store with RAII subscriptions.

## Status

Pre-implementation. The full design is locked and lives in [`docs/architecture.md`](docs/architecture.md). Architecture decision records will accumulate under [`docs/adr/`](docs/adr/) as CortexFlow is built out.

## Targets

- Host (development & tests)
- POSIX / embedded Linux
- FreeRTOS / Zephyr
- Bare-metal MCUs

## Where to start

1. Read [`docs/architecture.md`](docs/architecture.md) — the complete design reference.
2. Skim [`docs/adr/`](docs/adr/) — short rationale documents for each major decision.
3. The repo skeleton mirrors section 15 of the architecture doc.

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
