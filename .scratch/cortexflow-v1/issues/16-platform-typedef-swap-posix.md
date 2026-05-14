# Platform backend typedef-swap formalization + POSIX backend + `CORTEXFLOW_TARGET` selection

Status: ready-for-human
PRD: `docs/prd.md` — Platform portability; user stories 44, 45, 48

## What to build

Formalize the platform typedef-swap mechanism that lets the same module code run on every supported environment without `#ifdef` walls. The composition references a single `platform::Backend` name (or per-facility names: `platform::Allocator`, `platform::TimerBackend`, `platform::TraceSink`, plus boundary-module typedefs like `platform::CanAdapter`) that resolves to the right implementation at build time.

This slice ships:

1. A `platform/<target>/` directory layout per the PRD's repo structure.
2. The host backend, gathered explicitly behind `platform/host/` (it has been implicit until now).
3. A new POSIX backend (`platform/posix/`) that uses POSIX primitives where they differ from raw host: file-descriptor based wake for the queue CV, `clock_gettime(CLOCK_MONOTONIC)` for `SteadyClock`, etc.
4. `CORTEXFLOW_TARGET={host,posix}` CMake option that swaps the active backend.
5. `cmake/targets/host.cmake` and `cmake/targets/posix.cmake`.
6. Documentation in the repo explaining how a contributor adds a new platform — write a new backend tree and a target file; no core changes.

FreeRTOS and bare-metal backends are deferred to slices 18 and 19.

## Acceptance criteria

- [x] `platform/host/` and `platform/posix/` directories with their backend implementations
- [x] Each backend provides the named facility types: `Allocator`, `TimerBackend`, `TraceSink`
- [x] `CORTEXFLOW_TARGET={host,posix}` CMake option implemented; default = `host`
- [x] All existing tests pass under both `host` and `posix` targets in CI
- [x] One module's source has zero `#ifdef`s referring to target type — verified by grep in CI
- [x] Contributor doc in the repo explaining the typedef-swap mechanism and how to add a new backend
- [x] Integration test: same composition built for both targets, sends/receives equivalently

## Blocked by

- `04-messaging-envelope-allocator.md`
- `12-timer-service.md`
- `03-trace-infrastructure.md`

## Comments

### 2026-05-14 — from sandcastle agent

Built and verified under both targets with gcc 12.2 (host + posix, all 19 ctests passing).

**Shape**

- `<cortexflow/platform.hpp>` is the swap point. Each backend ships its own copy at `platform/<target>/cortexflow/platform.hpp`; CMake's `target_include_directories(cortexflow PUBLIC platform/<target>)` puts the right one on the include path.
- Three typedefs per backend: `platform::Allocator` (→ `cortexflow::HeapAllocator`), `platform::TimerBackend` (→ `cortexflow::SteadyClock`), `platform::TraceSink` (tag struct documenting the linked sink).
- Core declares `HeapAllocator` / `SteadyClock`; each backend provides the implementation. `default_allocator()` is now backend-defined, but the test that asserts `&default_allocator() == &HeapAllocator::instance()` still passes because both symbols are defined in the same backend translation unit.
- `src/cortexflow/messaging.cpp` is gone; its contents (renamed by git) now live at `platform/host/heap_allocator.cpp`.

**POSIX flavour vs host**

I deliberately differentiated the POSIX backend's primitives rather than copy-pasting host: `posix_memalign` instead of `::operator new`, `pthread_mutex_t` instead of `std::mutex`, `clock_gettime(CLOCK_MONOTONIC)` instead of `std::chrono::steady_clock`, `write(STDERR_FILENO, ...)` instead of `fprintf(stderr, ...)`. Observable behaviour is identical; the choice demonstrates the swap is real. Reviewer should confirm this level of differentiation is the intent — collapsing POSIX to "same as host" is a small edit if it isn't.

**What I deliberately did NOT do**

- File-descriptor based wake for the queue CV (issue text mentions it as an example, but architecture §4.2 explicitly says "No eventfd, no epoll integration" — host and POSIX both use `std::mutex + std::condition_variable`). Left the Runtime's wake primitive untouched.
- A separate static-library backend target. The simpler model is "one `cortexflow` lib, backend sources added in via the target file". If you want `cortexflow_core` + `cortexflow_platform_<target>` as separate libs later, it's a CMake refactor without touching code.
- Did not move `platform_fault_handler` into the backends. It's identical across host and POSIX and the issue's acceptance criteria don't call it out. Bare-metal backend (slice 19) will likely move it.
- Did not add CORTEXFLOW_TARGET_HOST / POSIX preprocessor defines. Removed them from the target files to avoid tempting future code into `#ifdef` walls — the `no_target_ifdefs` CTest entry would catch it, but easier to never offer the foot-gun.

**Verification**

- `no_target_ifdefs` ctest entry greps `include/cortexflow/`, `src/cortexflow/`, and `tests/integration/test_platform.cpp` for `CORTEXFLOW_TARGET_(HOST|POSIX|FREERTOS|BARE_METAL)`. I sanity-tested it by dropping a sentinel `#ifdef CORTEXFLOW_TARGET_HOST` into `src/cortexflow/_sentinel.cpp` — test failed with the expected diagnostic; reverted.
- CI workflow expanded to a 2×2 matrix (gcc/clang × host/posix). All 19 tests pass across all four cells locally.
- `tests/integration/test_platform.cpp` is the cross-target equivalence fixture: a two-module ping/pong composition that names only `platform::Allocator` / `platform::TimerBackend` / `platform::TraceSink`. Same binary, same assertions, both targets.

**Where to look first on review**

- `docs/agents/platform-backends.md` — the contributor-facing doc. The "Adding a new platform" section is the contract; if it's wrong, slices 18/19 will hit friction.
- `platform/host/cortexflow/platform.hpp` and `platform/posix/cortexflow/platform.hpp` side-by-side — they're the API surface.
- The two target files `cmake/targets/{host,posix}.cmake` — minimal, intentionally. Future targets should not need to grow this pattern.
