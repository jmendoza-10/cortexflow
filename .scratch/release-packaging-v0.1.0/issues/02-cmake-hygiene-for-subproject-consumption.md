# CMake hygiene: make CortexFlow safe to consume as a sub-project

Status: ready-for-agent
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 3, 4, 5, 9, 20, 21, 22, 23, 24
ADRs: `docs/adr/0022-private-compile-flags-for-rtti-and-exceptions.md`, `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

Five targeted edits to the CMake tree so CortexFlow can be consumed via `FetchContent_MakeAvailable` (or `add_subdirectory` for vendoring) without contaminating the consumer's build.

1. **Flip `-fno-rtti -fno-exceptions` from PUBLIC to PRIVATE** on the `cortexflow` target. After this change, those flags apply only when compiling CortexFlow's own translation units; a consumer linking the `cortexflow` target compiles its own code with whatever exception/RTTI posture it chooses. `-Wall -Wextra -Wpedantic` remain PUBLIC (they are warning-flags, not behavior-changing flags). See ADR-0022 for the full rationale.

2. **Add `VERSION 0.1.0` to the top-level `project()` declaration**, making it `project(cortexflow VERSION 0.1.0 LANGUAGES CXX)`. This makes `${cortexflow_VERSION}` available to consumers after `FetchContent_MakeAvailable` and is the single source of truth for the version number going forward.

3. **Gate `add_subdirectory(examples/minimal_app)` behind a `CORTEXFLOW_BUILD_EXAMPLES` option** declared with `option(CORTEXFLOW_BUILD_EXAMPLES "Build examples" <default>)`. The default is computed: `ON` when CortexFlow is the top-level project (detected via `if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)`), `OFF` otherwise. A consumer can still force-enable by setting `CORTEXFLOW_BUILD_EXAMPLES=ON` explicitly, and a top-level developer can opt out by setting `=OFF` explicitly.

4. **Replace `${CMAKE_SOURCE_DIR}` with `${PROJECT_SOURCE_DIR}` in `cmake/targets/host.cmake` and `cmake/targets/posix.cmake`.** Both files currently reference `${CMAKE_SOURCE_DIR}/platform/<target>/...` for `target_sources` and `target_include_directories`. `CMAKE_SOURCE_DIR` resolves to the *top-level* project's source dir, which is the consumer's tree when CortexFlow is a sub-project. `PROJECT_SOURCE_DIR` is set by the `project(cortexflow ...)` call to CortexFlow's own tree, which is what these paths need to resolve to in both top-level and sub-project consumption.

5. **No change to `CORTEXFLOW_BUILD_TESTS`** — it already defaults `OFF` and is opt-in. Confirm this is the case and that `tests/` is not added unconditionally.

After this slice, CortexFlow can be `add_subdirectory`'d into an arbitrary CMake parent project, the parent picks `CORTEXFLOW_TARGET` via `set(... CACHE STRING "" FORCE)` before the add_subdirectory call, and only the `cortexflow` static library target gets produced — no examples, no tests, no leaked compile flags.

## Acceptance criteria

- [ ] `-fno-rtti -fno-exceptions` are PRIVATE compile options on the `cortexflow` target; `-Wall -Wextra -Wpedantic` remain PUBLIC
- [ ] The top-level `project()` declaration includes `VERSION 0.1.0`
- [ ] `add_subdirectory(examples/minimal_app)` is gated behind `option(CORTEXFLOW_BUILD_EXAMPLES "Build examples" <computed default>)`, where the computed default evaluates to `ON` at top-level and `OFF` as a sub-project
- [ ] `cmake/targets/host.cmake` and `cmake/targets/posix.cmake` use `${PROJECT_SOURCE_DIR}` instead of `${CMAKE_SOURCE_DIR}` for paths to `platform/<target>/...`
- [ ] `CORTEXFLOW_BUILD_TESTS` continues to default `OFF` and remains the only gate for `tests/`
- [ ] Top-level configure with no options still builds the `cortexflow` library and the `minimal_app` example (proving the default-`ON`-at-top-level behavior)
- [ ] Top-level configure with `-DCORTEXFLOW_BUILD_EXAMPLES=OFF` builds the library but not the example
- [ ] The existing CI build/test matrix (host + posix, gcc + clang, `CORTEXFLOW_BUILD_TESTS=ON`, `CORTEXFLOW_TRACE_LEVEL=FULL`) passes end-to-end
- [ ] No source file under `src/cortexflow/` or `platform/host/` or `platform/posix/` requires a code change — this is a CMake-only slice

## Blocked by

None — can start immediately. Slice 3 (FetchContent smoke test) verifies these invariants from a consumer's perspective and is the natural acceptance gate, but can be done independently and in parallel.

## Notes

- The `if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)` test is the conventional CMake idiom for "am I the top-level project?" — works on CMake 3.20+ which is already the project's minimum.
- After this slice, `${cortexflow_VERSION}` will be `0.1.0` even before slice 6 cuts the actual tag. That is intentional and harmless — the version is what the source tree claims to be, regardless of whether a tag exists yet. Slice 6 only changes the value when bumping to 0.1.1 / 0.2.0 / etc.
- If you discover any *other* `${CMAKE_SOURCE_DIR}` reference in the CMake tree (outside the two target files), evaluate whether it has the same sub-project problem and fix it. Search before assuming the two known ones are exhaustive.
- The PRIVATE-flags flip is irreversible without a major-version bump once shipped (a consumer's build will silently rely on exceptions being available), so confirm via the smoke test in Slice 3 before tagging.
