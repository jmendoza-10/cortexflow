# Trace infrastructure: six levels, compile-time selection, pluggable sink

Status: ready-for-human
PRD: `docs/prd.md` — Trace subsystem; user stories 17, 18, 19, 56

## What to build

A six-level trace hierarchy (`OFF` / `ERROR` / `WARN` / `INFO` / `DISPATCH` / `FULL`) selected at compile time via a build-flag-driven constant. Trace points use `if constexpr` against that constant so calls above the selected level compile to no-ops the optimizer drops. A pluggable sink interface receives formatted trace records; the default host sink writes to stderr. The CMake flag `FRAMEWORK_TRACE_LEVEL` selects the level.

CI runs every test with `FRAMEWORK_TRACE_LEVEL=FULL` so that all trace points stay syntactically valid regardless of the default build level. Default coverage by level: `DISPATCH` traces each dispatched envelope (`from → to`, type name); `FULL` adds cache writes, state transitions, and timer arm/cancel/fire.

## Acceptance criteria

- [ ] Six-level enum and a compile-time `kTraceLevel` constant driven by the CMake flag
- [ ] `TRACE_*(level, ...)` macros use `if constexpr (level <= kTraceLevel)`; below-threshold calls expand to nothing
- [ ] Pluggable sink interface; default host sink to stderr; one-line format documented in code comments (timestamp, level, kind, from, to, type name, key fields)
- [ ] `FRAMEWORK_TRACE_LEVEL={OFF,ERROR,WARN,INFO,DISPATCH,FULL}` CMake option wired up
- [ ] CI job runs the full test suite with `TRACE_FULL` enabled
- [ ] Unit test that `TRACE_FULL(...)` at level `OFF` produces no symbol references in optimized output

## Blocked by

None — can start immediately.

## Comments

### 2026-05-13 — from sandcastle agent

**What was built:**

All six acceptance criteria are satisfied:

1. **Six-level enum + kTraceLevel constant** (`include/framework/trace.hpp`): `TraceLevel` enum (`Off=0` through `Full=5`) with `inline constexpr kTraceLevel` driven by the `FRAMEWORK_TRACE_LEVEL_VALUE` compile definition. Falls back to `DISPATCH` (4) if no define is provided.

2. **FRAMEWORK_TRACE macros with if constexpr** (`include/framework/trace.hpp`): Generic `FRAMEWORK_TRACE(lvl, kind, from, to, type_name, msg)` macro plus per-level convenience macros (`FRAMEWORK_TRACE_ERROR` through `FRAMEWORK_TRACE_FULL`). The `if constexpr` check compiles out below-threshold calls entirely.

3. **Pluggable sink interface** (`include/framework/trace.hpp` + `src/framework/trace.cpp`): `extern "C" platform_trace_sink()` with weak-linked default that formats one-line records to stderr — same override pattern as `platform_fault_handler`. Format: `[<elapsed_s>.<ms>] <LEVEL> <kind> <from> -> <to> <type_name> <key_fields>`.

4. **FRAMEWORK_TRACE_LEVEL CMake option** (`CMakeLists.txt`): String-to-integer mapping, passed as `FRAMEWORK_TRACE_LEVEL_VALUE` compile definition. Default: `DISPATCH`.

5. **CI workflow** (`.github/workflows/ci.yml`): Builds and tests with `FRAMEWORK_TRACE_LEVEL=FULL` on both GCC and Clang.

6. **Trace elision test** (`tests/unit/trace_elision_check.cpp`): Compiles a file using all TRACE macros at level OFF with `-O2`, then verifies via `nm` that no `trace_emit` symbol references survive in the object output.

**Design decisions:**
- Followed the existing weak-linkage pattern from `platform_fault_handler` for the pluggable sink (consistent with ADR-015 and the bare-metal override story).
- The sink receives structured parameters (level, kind, from, to, type_name, message) rather than a pre-formatted string, giving platform sinks full control over formatting and timestamps.
- Default host sink uses `std::chrono::steady_clock` for elapsed-time timestamps.

**Nothing was skipped or deferred.**

**Reviewer notes:**
- The `if constexpr` in non-template context relies on optimizer dead-code elimination (not template-discarded-statement semantics). The elision test validates this at `-O2`. At `-O0`, the branch is still dead code but the compiler may leave symbol references — this matches the spec's "optimized output" qualifier.
- Tests use `CHECK` (not `REQUIRE`) throughout because doctest's `REQUIRE` is unavailable under `-fno-exceptions`.
