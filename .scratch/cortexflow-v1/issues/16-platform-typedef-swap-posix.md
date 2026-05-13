# Platform backend typedef-swap formalization + POSIX backend + `FRAMEWORK_TARGET` selection

Status: ready-for-agent
PRD: `docs/prd.md` — Platform portability; user stories 44, 45, 48

## What to build

Formalize the platform typedef-swap mechanism that lets the same module code run on every supported environment without `#ifdef` walls. The composition references a single `platform::Backend` name (or per-facility names: `platform::Allocator`, `platform::TimerBackend`, `platform::TraceSink`, plus boundary-module typedefs like `platform::CanAdapter`) that resolves to the right implementation at build time.

This slice ships:

1. A `platform/<target>/` directory layout per the PRD's repo structure.
2. The host backend, gathered explicitly behind `platform/host/` (it has been implicit until now).
3. A new POSIX backend (`platform/posix/`) that uses POSIX primitives where they differ from raw host: file-descriptor based wake for the queue CV, `clock_gettime(CLOCK_MONOTONIC)` for `SteadyClock`, etc.
4. `FRAMEWORK_TARGET={host,posix}` CMake option that swaps the active backend.
5. `cmake/targets/host.cmake` and `cmake/targets/posix.cmake`.
6. Documentation in the repo explaining how a contributor adds a new platform — write a new backend tree and a target file; no core changes.

FreeRTOS and bare-metal backends are deferred to slices 18 and 19.

## Acceptance criteria

- [ ] `platform/host/` and `platform/posix/` directories with their backend implementations
- [ ] Each backend provides the named facility types: `Allocator`, `TimerBackend`, `TraceSink`
- [ ] `FRAMEWORK_TARGET={host,posix}` CMake option implemented; default = `host`
- [ ] All existing tests pass under both `host` and `posix` targets in CI
- [ ] One module's source has zero `#ifdef`s referring to target type — verified by grep in CI
- [ ] Contributor doc in the repo explaining the typedef-swap mechanism and how to add a new backend
- [ ] Integration test: same composition built for both targets, sends/receives equivalently

## Blocked by

- `04-messaging-envelope-allocator.md`
- `12-timer-service.md`
- `03-trace-infrastructure.md`
