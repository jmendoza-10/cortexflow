# Scaffold `examples/button_pipeline/` with a ButtonReader-only composition

Status: ready-for-agent

## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Create the new example directory `examples/button_pipeline/` alongside the existing `examples/minimal_app/`, with the smallest possible composition that proves the end-to-end wiring works: one **Boundary module** (`ButtonReader`) with no Flow and no behavior, the `Runtime` composition declaration, the `App` lifecycle wrapper, a stub `main.cpp`, and a single integration test case that builds the App, runs an empty queue, and shuts down cleanly.

This slice intentionally adds zero application behavior — no Debouncer, no Flows, no cache keys. The goal is to make all later slices uncontroversial: every CMake target, namespace, file layout, SPDX header, and CI-guard scan is in place before any logic lands.

`ButtonReader` is the **Boundary module** in the CONTEXT.md sense (commented as such in the header). It declares `using Inbox = std::tuple<>;` and inherits a no-op `handle()` from `cortexflow::Module`. Posting envelopes addressed *to* it would assert, but no one does — `ButtonReader`'s job is only to be present in `ModuleList` and to label the boundary.

The example's `App` wrapper mirrors `minimal_app::App` exactly: default constructor uses `SteadyClock`; a `Clock&`-injection constructor takes a `ManualClock` for tests. The static `g_runtime` pointer pattern and the `cache()` / `timers()` helper functions are reproduced so later slices can use them from state-local Locals constructors.

The integration test is a single `TEST_CASE` that constructs the App with a `ManualClock`, calls `start()`, `run_one()` (no-op since the queue is empty), then `shutdown()` — asserting `queue_size() == 0` and that the round-trip does not assert. A compile-time `static_assert` verifies `Runtime::kNumModules == 1` and `Keys::size == 0`, so any later change to the composition shape that bypasses the PRD trips at compile time.

CMake is wired so the example builds whenever `CORTEXFLOW_BUILD_EXAMPLES=ON` (the default for top-level builds), the test runs whenever `CORTEXFLOW_BUILD_TESTS=ON`, and the `no_target_ifdefs` guard's grep list is extended to scan the new directory.

## Acceptance criteria

- [ ] Folder `examples/button_pipeline/` exists with: `CMakeLists.txt`, `app.hpp`, `app.cpp`, `keys.hpp` (empty namespace block — placeholder for later slices), `main.cpp`, `modules/button_reader.hpp`, `modules/button_reader.cpp`.
- [ ] Namespace is `button_pipeline`; every file under the directory uses it.
- [ ] CMake targets `button_pipeline_lib` (STATIC, sources: `app.cpp`, `modules/button_reader.cpp`) and `button_pipeline` (executable, source: `main.cpp`, links `button_pipeline_lib`) are defined and build cleanly.
- [ ] Root `CMakeLists.txt` invokes `add_subdirectory(examples/button_pipeline)` under the existing `CORTEXFLOW_BUILD_EXAMPLES` guard, in addition to `minimal_app`.
- [ ] `app.hpp` declares `using Modules = cortexflow::ModuleList<ButtonReader>;`, an empty `cortexflow::CacheKeyList<>` aliased as `Keys`, `using AppConfig = cortexflow::Config<cortexflow::MaxSubscriptions<8>>;`, `using Runtime = cortexflow::Runtime<Modules, Keys, AppConfig>;`, and the `AppCache` typedef.
- [ ] `app.hpp` and `app.cpp` reproduce the `detail::g_runtime` static-pointer pattern and `cache()` / `timers()` free-function helpers from `minimal_app`, including the assertion that two `App` instances cannot coexist.
- [ ] `App` class has both default-clock and `Clock&`-injection constructors, the same lifecycle surface (`start`, `run`, `run_one`, `stop`, `shutdown`, `post`, `queue_size`, `get<M>`, `cache_ref`, `timers_ref`, `runtime`) as `minimal_app::App`.
- [ ] `ButtonReader` declares `using Inbox = std::tuple<>;`, has no Flow, and a header comment labels it a Boundary module per CONTEXT.md.
- [ ] `main.cpp` mirrors `minimal_app/main.cpp` (constructs App, `start()`, `run()`, `shutdown()`) — stdin driving comes in a later slice.
- [ ] Test fixture at `tests/integration/test_button_pipeline.cpp` registered with CTest as test name `button_pipeline`, linking `button_pipeline_lib`. Single `TEST_CASE` asserts the App lifecycle round-trip on an empty queue.
- [ ] Compile-time `static_assert` in the test pins `Runtime::kNumModules == 1` and `Keys::size == 0`.
- [ ] `tests/CMakeLists.txt` `no_target_ifdefs` test extended to scan `examples/button_pipeline/` in addition to `examples/minimal_app/`.
- [ ] Every new `.hpp` and `.cpp` carries the two-line SPDX header; the existing `spdx_headers` test passes.
- [ ] Builds clean and `ctest --test-dir build -R "button_pipeline|no_target_ifdefs|spdx_headers"` passes under both `CORTEXFLOW_TARGET=host` and `CORTEXFLOW_TARGET=posix`.
- [ ] `tests/integration/test_minimal_app.cpp` and every other existing test remains green.

## Blocked by

None — can start immediately.
