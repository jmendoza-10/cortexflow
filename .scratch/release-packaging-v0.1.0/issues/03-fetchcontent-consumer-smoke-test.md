# FetchContent consumer smoke test: prove the v0.1.0 consumption contract end-to-end

Status: ready-for-agent
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 1, 3, 4, 5, 9, 20, 21, 22
ADRs: `docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md`, `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

A standalone CMake project living inside the CortexFlow repository that exercises the full **FetchContent consumption contract** as documented in ADR-0023, treating CortexFlow as an external dependency from a consumer's perspective. Its purpose is to verify the sub-project hygiene from Slice 2 end-to-end, and to act as a regression guard against future drift.

Proposed location: `tests/integration/fetchcontent_consumer/` — a sibling to the existing integration tests, but structured as its own top-level CMake project (with its own `CMakeLists.txt` that calls `project(...)`) rather than as a `target_sources` addition to the main `cortexflow` build.

The smoke test must do the following from its own `CMakeLists.txt`, structurally identical to what a real consumer would write:

1. `include(FetchContent)`.
2. `FetchContent_Declare(cortexflow SOURCE_DIR <path to CortexFlow repo root>)` — using a local `SOURCE_DIR` rather than `GIT_REPOSITORY`/`GIT_TAG`, so the test runs offline and against the *current* commit on every CI invocation, not the most-recently-tagged commit. The path is resolved relative to the smoke test's own location.
3. `set(CORTEXFLOW_TARGET posix CACHE STRING "" FORCE)` *before* `FetchContent_MakeAvailable`.
4. `FetchContent_MakeAvailable(cortexflow)`.
5. Declare a tiny consumer executable (a single `main.cpp` with `int main() { return 0; }` is sufficient — the test is about the build, not the runtime) that does `target_link_libraries(consumer_main PRIVATE cortexflow)`.
6. Declare a **second** target compiled *with* exceptions enabled (no `-fno-exceptions`) and *with* RTTI enabled, that also links `cortexflow` and contains a translation unit using `throw` and `dynamic_cast` inside a non-instantiated template (or a `#if 0`-guarded body — the point is to verify the *compile flags* the consumer sees, not to execute the code). If the PRIVATE-flags posture from Slice 2 is correct, this target compiles. If the flags are PUBLIC, this target fails to compile.
7. Assert `${cortexflow_VERSION}` is set to `0.1.0` (via `if(NOT cortexflow_VERSION STREQUAL "0.1.0") message(FATAL_ERROR ...)` in CMake, evaluated at configure time).
8. Assert `examples/minimal_app` is *not* a buildable target in this consumer build, by checking that no target named `minimal_app` exists (`if(TARGET minimal_app) message(FATAL_ERROR ...)`).

Wire the smoke test into CI as a CTest case in the main CortexFlow build by adding an `add_test(...)` invocation that invokes `cmake` and `cmake --build` against the smoke-test project in a temporary build directory. The CTest case fails if either the configure step or the build step fails. The simplest mechanism is `tests/CMakeLists.txt` adding a custom test that runs `${CMAKE_COMMAND}` against the smoke-test source dir with a fresh build dir under `${CMAKE_BINARY_DIR}/fetchcontent_consumer_build/`.

The smoke test is opt-in via `CORTEXFLOW_BUILD_TESTS=ON` (same flag as the rest of the test suite); it is enabled in the CI matrix by the existing `-DCORTEXFLOW_BUILD_TESTS=ON` configure step.

## Acceptance criteria

- [ ] A new `tests/integration/fetchcontent_consumer/` directory exists with its own top-level `CMakeLists.txt` (calling `project(fetchcontent_consumer ...)`)
- [ ] The smoke test's `CMakeLists.txt` uses `FetchContent_Declare(... SOURCE_DIR ...)` against the local CortexFlow tree (not a remote URL), so the test is offline-clean
- [ ] The smoke test sets `CORTEXFLOW_TARGET posix CACHE STRING "" FORCE` *before* `FetchContent_MakeAvailable(cortexflow)`
- [ ] The smoke test defines an `exceptions_consumer` (or similarly-named) target compiled with exceptions and RTTI enabled, linking `cortexflow`, and that target compiles successfully — proving the PRIVATE flags from Slice 2 are correctly scoped
- [ ] The smoke test's `CMakeLists.txt` includes a configure-time assertion that `cortexflow_VERSION` equals `0.1.0`
- [ ] The smoke test's `CMakeLists.txt` includes a configure-time assertion that no target named `minimal_app` exists
- [ ] The smoke test is wired into CTest in the main CortexFlow build (when `CORTEXFLOW_BUILD_TESTS=ON`) as a single test case that invokes `cmake` configure + build against the smoke-test source in a temp dir
- [ ] The CI matrix (host + posix backends, gcc + clang compilers, with `CORTEXFLOW_BUILD_TESTS=ON`) passes with the new test case enabled — including under TSan if the parent build enables it
- [ ] The smoke test takes less than 30 seconds wall-clock to run on a CI runner (it's a configure + tiny-link, no heavy compilation)
- [ ] A deliberately-broken regression on Slice 2's invariants (e.g. reverting `${PROJECT_SOURCE_DIR}` back to `${CMAKE_SOURCE_DIR}` in one of the target files) causes the smoke test to fail — verifiable by toggling and observing the failure before submitting

## Blocked by

Slice 2 (`02-cmake-hygiene-for-subproject-consumption.md`). The smoke test can be written in parallel, but it cannot pass until Slice 2 has shipped.

## Notes

- The PRD calls this the *most consequential piece of new code* in the release-packaging effort. Treat it accordingly: it is the executable form of the consumption contract.
- The `exceptions_consumer` target's body can be `#if 0`-guarded code that uses `throw` / `dynamic_cast` — what matters is that the *compile flags* the consumer sees do not include `-fno-exceptions` / `-fno-rtti`. The simplest verification is a translation unit whose only purpose is to be compiled with the inherited flags; any compile failure there reveals a leak from the `cortexflow` target's interface.
- Do **not** test the framework's runtime behavior here. The smoke test verifies the *consumption contract*; runtime semantics are exhaustively covered by the existing `tests/integration/` suite.
- If a future slice adds install rules (deferred per ADR-0023), a sibling smoke test exercising `find_package(cortexflow CONFIG)` would mirror this one. Not in scope for v0.1.0.
- If `FetchContent_Declare(... SOURCE_DIR ...)` proves awkward for the parent-build-runs-child-build pattern, an acceptable alternative is for the CTest case to set up a `SOURCE_DIR` pointing at `${PROJECT_SOURCE_DIR}` (the CortexFlow root, computed at parent configure time) and pass it to the child configure via `-D`. Either approach satisfies the contract; pick whichever is cleaner.
