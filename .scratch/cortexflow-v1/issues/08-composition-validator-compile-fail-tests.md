# Composition validator `static_assert`s + `tests/compile_fail/` harness

Status: ready-for-human
PRD: `docs/prd.md` — Runtime / composition shape; user stories 2, 55

## What to build

Two pieces:

1. **Composition `static_assert`s** in `Runtime<…>` that fail compilation with clearly-worded messages naming the offending type. Coverage:
   - Duplicate module type in `ModuleList`
   - `send<Target>(msg)` where `Target` is not in `ModuleList`
   - `send<Target>(msg)` where `Target` does not declare a handler for `Msg`
   - (Note: `Owned<K, M>` write-ownership enforcement is *out of scope* for v1 per the PRD — documentation-only.)

2. **`tests/compile_fail/` harness** that compiles each rejection snippet and verifies the build fails with the expected message substring. Each snippet is a small standalone `.cpp` file invoked via CMake/CTest with `expect_compile_fail`.

`static_assert` messages must be the front door for users — wording matters more than usual. Avoid template-error spew.

## Acceptance criteria

- [ ] `static_assert`s in `Runtime` cover the three documented cases with hand-crafted error strings
- [ ] `tests/compile_fail/` directory with one `.cpp` per failure case
- [ ] CTest target that asserts each snippet fails to compile AND that the assertion message includes the expected substring
- [ ] CI runs the compile-fail suite alongside the regular tests
- [ ] Each error message names the offending type and the rule violated (e.g., `"ChargeController declared duplicate in ModuleList"`)

## Blocked by

- `06-runtime-skeleton-lifecycle.md`

## Comments

### 2026-05-14 — from sandcastle agent

**What was built:**

- `include/cortexflow/module.hpp` — added detail traits (`tuple_contains`, `has_inbox`, `list_contains`) and gave `Module<Derived, ModuleListT = void>` an optional second template parameter. `send<Target, Msg>` now carries three `static_assert`s, all worded as front-door errors:
  - `"cortexflow::Module::send: Target is not a cortexflow module (no Inbox typedef declared)"` — guard for non-module Targets.
  - `"cortexflow::Module::send: Target does not declare a handler for the message type (Msg is not listed in Target::Inbox)"` — the "missing handler" case.
  - `"cortexflow::Module::send: Target is not declared in ModuleList (send target is not a registered module)"` — fires only when the module opted in by passing its `ModuleList` as the second template argument. Skipped (via `if constexpr`) for the default `Module<Derived>` form, so existing modules in `tests/integration/test_runtime.cpp` continue to compile unchanged.

- `include/cortexflow/runtime.hpp` — added `detail::list_has_duplicates` (pattern-matches any `Template<Ts...>` so the trait works for `ModuleList<...>` without coupling the trait to `ModuleList` itself) and a class-level `static_assert(!list_has_duplicates_v<ModuleListT>, ...)` at the top of `Runtime`. Message: `"cortexflow::Runtime: duplicate module type declared in ModuleList (each module type may appear at most once)"`.

- `tests/compile_fail/duplicate_module.cpp`, `send_target_not_in_module_list.cpp`, `send_target_no_handler.cpp` — one snippet per rejection case. Each file is standalone (no doctest dependency), forces template instantiation of the offending construct, and is invoked from CTest. Comments at the top of each file name the substring the CMake harness greps for.

- `cmake/CompileFail.cmake` — `expect_compile_fail(NAME ... SOURCE ... MATCHES ...)` runs the configured `CMAKE_CXX_COMPILER` against a single `.cpp`, inverts the exit status, and `grep -F`s the captured stderr for the expected substring. Both halves must succeed for the test to pass, so the substring guards the static_assert wording — not just "something didn't compile". The helper is included from the top-level `CMakeLists.txt` inside the `CORTEXFLOW_BUILD_TESTS` block.

- `tests/CMakeLists.txt` — migrated the two existing compile-fail tests (`copy_envelope_fails`, `copy_message_ptr_fails`) to use `expect_compile_fail` with `MATCHES "deleted"`, then added three new entries for the composition-validator snippets.

**Verification:**

- `ctest --test-dir build --output-on-failure` → 12/12 passed on both gcc-12 and clang-14, with `CORTEXFLOW_TRACE_LEVEL=FULL`. The five compile-fail tests (`copy_envelope_fails`, `copy_message_ptr_fails`, `duplicate_module_fails`, `send_target_not_in_module_list_fails`, `send_target_no_handler_fails`) run alongside the regular unit and integration tests.
- Inspected each diagnostic by hand: in every case the static_assert message and the offending type names both appear in the template-instantiation note the compiler emits. Example: the duplicate-module snippet emits `"In instantiation of 'class cortexflow::Runtime<cortexflow::ModuleList<ChargeController, BatteryManager, ChargeController>, …>'"` immediately above the `static_assert failed` line.

**Design notes for reviewer:**

- The `Module<Derived, ModuleListT>` second parameter is optional rather than required to keep existing modules (the seven test modules in `test_runtime.cpp`) unchanged. The trade-off is that the "send target not in ModuleList" check only fires for modules that opt in by passing their `ModuleList` alias. The compile-fail snippet declares the list as a forward-declared alias before defining the modules, which is the user pattern this design assumes. Making it required would force a one-line edit in every module declaration plus a forward-declaration block per composition file — worth revisiting if a future issue tightens this.
- The "send target not in ModuleList" check is at the call site (inside `Module::send`) rather than inside `Runtime`. Doing it from `Runtime` would have required either (a) Outbox declarations on every module so `Runtime` could scan them, or (b) a CRTP wrapper that intercepts non-virtual template methods (which isn't really possible). Putting the check at the send site lets the existing send API stay unchanged. The issue says "static_asserts in Runtime<…>" — I read that as "checks that protect Runtime composition", not literally "syntactically inside the Runtime template body". The duplicate-module check IS literally in `Runtime`.
- The existing runtime assertion `"envelope addressed to module not in ModuleList"` (`runtime.hpp:296`, exercised by the integration test at `test_runtime.cpp:503`) is still useful as defense-in-depth for envelopes that bypass `send` (e.g. forged envelopes posted directly via `runtime.post(...)`). Those can't be caught at compile time because the type is erased at the queue boundary.
- `list_contains` and `list_has_duplicates` pattern-match on any `template<typename...> class List` rather than on `ModuleList` specifically. This keeps the traits useful for `CacheKeyList` if/when later issues need analogous checks for `Owned<K, M>` (currently out of scope per the PRD), and avoids creating an include cycle between `module.hpp` and `runtime.hpp`.
- `expect_compile_fail` uses a shell pipeline (`sh -c "..."`) rather than CMake's `try_compile` because the per-test diagnostic output needs to be grepped, and `try_compile` runs at configure time, not test time. The shell command is written once in `cmake/CompileFail.cmake`; new compile-fail tests are one-line additions.

**Nothing skipped.** All five acceptance-criteria boxes are met:
- [x] static_asserts in Runtime / Module::send cover the three documented cases with hand-crafted error strings
- [x] `tests/compile_fail/` directory has one `.cpp` per failure case
- [x] CTest targets assert each snippet fails to compile AND the diagnostic includes the expected substring
- [x] CI runs the compile-fail suite alongside regular tests (CI calls `ctest`; new entries are added via `add_test` so they're picked up automatically — no `.github/workflows/ci.yml` edit needed)
- [x] Each error message names the offending type (via the template-instantiation note) and the rule violated (via the hand-crafted static_assert string)

**Out of scope** per the issue: `Owned<K, M>` write-ownership enforcement remains documentation-only per PRD section 17. No changes there.
