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
