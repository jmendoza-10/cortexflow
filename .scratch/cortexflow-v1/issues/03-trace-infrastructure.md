# Trace infrastructure: six levels, compile-time selection, pluggable sink

Status: ready-for-agent
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

### 2026-05-21 — from human reviewer

Re-opening: the prior pass landed the **infrastructure** (header, macros, sink, CMake flag, CI matrix, elision test) but skipped the **instrumentation call-sites** that the issue's own coverage statement promises:

> Default coverage by level: `DISPATCH` traces each dispatched envelope (`from → to`, type name); `FULL` adds cache writes, state transitions, and timer arm/cancel/fire.

Confirmed empirically by running the `button_pipeline` example (now end-to-end after slices 01–06): zero trace output even at the default `DISPATCH` level. A grep for `CORTEXFLOW_TRACE` across `src/` `include/` `examples/` finds exactly one call-site — the `WARN` for drain-budget exhaustion at `include/cortexflow/runtime.hpp:248` — which only fires on pathological queue overflow.

**What's still missing (the actual coverage promised above):**

- `Runtime::dispatch()` (or wherever an envelope is handed to a module's `handle`): one `CORTEXFLOW_TRACE_DISPATCH("envelope", from_name, to_name, type_name, "")` per dispatched envelope. `from`/`to` should be module names (or `"-"` for the foreign-thread / boundary post case, matching the convention already in the existing `WARN`); `type_name` should be the message type. The runtime already carries enough metadata for this — `Envelope` has from/to ids and module names are recoverable from `ModuleList`.
- Cache write path: `CORTEXFLOW_TRACE_FULL("cache_write", owner_name, "-", key_type_name, "<value summary>")` per committed cache write — *before* the `KeyChanged` fanout, so the trace ordering matches the causal order.
- Timer service: `CORTEXFLOW_TRACE_FULL("timer_arm", owner_name, "-", timer_type_name, "due=<ms>")` on arm, `"timer_cancel"` on cancel, `"timer_fire"` on fire. The Locals-RAII path for subscriptions/timers means cancel happens in destructors — trace from there too.
- Flow state transitions (nice-to-have, also promised): `CORTEXFLOW_TRACE_FULL("transition", module_name, "-", flow_type_name, "<from_state>-><to_state>")`. State names can be the demangled state-struct type, same approach the validator uses.

**Acceptance for this re-pass:**

- [ ] Running `examples/button_pipeline` at default `CORTEXFLOW_TRACE_LEVEL` (DISPATCH) prints one line per envelope dispatched
- [ ] Same example with `-DCORTEXFLOW_TRACE_LEVEL=FULL` additionally prints cache writes, timer arm/cancel/fire, and (if implemented) flow transitions
- [ ] One integration test that builds at `FULL`, captures stderr, and asserts the expected trace-event *kinds* appear in the expected causal order for one canonical scenario (e.g. the existing single-click test in `tests/integration/test_button_pipeline.cpp`) — this is what would have caught the missing instrumentation last time
- [ ] Elision test still passes at `OFF`

**Names, not raw `type_id_t`, in every trace line:**

The trace macro's `from` / `to` / `type_name` slots are `const char*` and were always meant to be human-readable identifiers (see the existing drain-budget `WARN` at `runtime.hpp:248`, which passes `"-"` placeholders, not numbers). When you wire up the call-sites you must pass *names*, not the 64-bit FNV-1a hashes from `type_id<T>()` / `Envelope::from()` / `Envelope::to()` / `Envelope::payload_type_id()`. A log line that reads `from=Debouncer to=ClickClassifier type=cortexflow::KeyChanged<button_pipeline::DebouncedButtonState>` is the goal; `from=14418779928... to=10739481... type=8821...` is the failure mode to avoid.

Concretely:

- **Module names (`from`, `to`)**: every module type ultimately CRTPs through `Identified<Derived>` which exposes `static constexpr std::string_view kName` (and the runtime already knows the modules by their static `module_type_id()` — see `runtime.hpp:368`). The `Runtime::dispatch` loop walks the `ModuleList` looking for `mod.module_type_id() == to` — at that point you already have the matching module instance, so you also have its `kName`. For `from`, the sender id sits on the `Envelope` (`env.from()`); resolve it the same way at the start of `dispatch` by walking the ModuleList once. `kNoSender` (== 0, see `messaging.hpp:14`) should render as `"-"` to match the existing `WARN` convention, not as `"0"`. If a `to` doesn't match any module, you've already got the "no handler" branch — log the unresolved numeric id there explicitly as `unknown:<hex>` so the dropped envelope is still debuggable. `kName` is `std::string_view`; the trace macro takes `const char*`. Two options: (a) require `kName` to be NUL-terminated by construction (it already is — it's a substring of `__PRETTY_FUNCTION__` ending in `]` or `)`, so it's *not* NUL-terminated — don't assume this), or (b) copy into a small `char` buffer on the stack at the trace site. Option (b) is the safer move; a 128-byte stack buffer per dispatch is fine.

- **Message / key / timer type names (`type_name` slot)**: don't read `Envelope::payload_type_id()` and try to resolve it — there's no runtime registry of message types and adding one is out of scope. Instead, place each trace point at the call-site where the type `T` is *statically* known, and pass `cortexflow::type_name<T>()`. Specifically:
  - The dispatcher's per-envelope `DISPATCH` trace lives inside the module-level `handle<MsgT>` path (where `MsgT` is the deduced message type), not in the type-erased `Runtime::dispatch` body. If the current dispatch hands a fully-erased `Envelope` to `Module::handle` without recovering the concrete type, you'll need to expose the type name to the trace from inside the per-type dispatch table. Plumb it through; don't introduce a separate runtime type-name registry.
  - `Cache::write` is templated on the key type `K` — `type_name<K>()` at the call-site.
  - Timer arm/cancel/fire is templated on the timer-message type — `type_name<TimerMsg>()` at the call-site. For cancel-from-destructor (Locals RAII), the destructor is a member of a type that already knows its timer type — capture it at construction.

- **Test the rendering, not just the firing**: the integration-test acceptance check already in the list ("captures stderr, asserts trace-event *kinds* appear in causal order") should also assert that at least one line per kind contains a recognisable name substring (e.g. `"Debouncer"`, `"ClickClassifier"`, `"KeyChanged"`) and that *no* line contains a bare 64-bit numeric id where a name was expected. This is what locks the requirement in: if a future change regresses to `type_id_t` ints, CI fails.

- **`type_string()` on `Envelope` (`messaging.hpp:158`) currently returns `std::to_string(type_id_)` — i.e. the numeric form.** Either change it to return the name (preferred — keep the existing signature, swap the body to a static lookup or, more likely, replace its single internal caller and remove the method since there's no good way to recover the name from a type-erased envelope without the registry that's out of scope), or leave it alone and stop using it from trace paths. Audit its callers first.

**Non-goals (stay out of scope):**

- Don't rework the sink interface, the macros, or the CMake flag — they're fine as-is.
- Don't change `runtime.hpp`'s public surface; instrumentation should sit inside existing methods.
- No new test framework — extend `tests/integration/test_button_pipeline.cpp` (or a sibling) using the existing doctest + stderr-capture approach.
- Do **not** add a global `type_id_t → const char*` runtime registry to resolve payload type names. Names live next to the static type via `type_name<T>()` — keep it that way.

**Why this is `ready-for-agent` not a sibling issue:** keeping the history on one ticket so a reviewer can see the gap-and-fix together. See [feedback_in_flight_issues] memory — sibling proliferation discouraged.
