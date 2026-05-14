# Runtime skeleton: composition, FIFO queue, two-phase init, `run_one`/`run`/`stop`

Status: ready-for-human
PRD: `docs/prd.md` — Runtime subsystem; user stories 1, 3, 4, 5, 6, 52, 53

## What to build

The `Runtime<ModuleList, CacheKeyList, Config>` template that owns the event loop, the single FIFO queue, module tuple storage, and lifecycle. This slice excludes the cache and timer surfaces (placeholder/empty until later slices) but wires up enough composition for end-to-end send/receive between two toy modules.

Two-phase init: construct all modules in declaration order, then call `on_start()` on each in declaration order. Modules may not interact during construction. `run()` blocks pumping the queue. `run_one()` drains the queue once and returns. `stop()` initiates graceful shutdown with a bounded drain budget; remaining handlers are skipped after budget exhaustion. `on_stop()` runs in reverse declaration order; destruction follows reverse order.

A `main()` against this slice is just `App app{…}; app.start(); app.run(); app.shutdown();` for production, or `app.start(); while(!done) app.run_one(); app.shutdown();` for tests.

## Acceptance criteria

- [ ] `Runtime<ModuleList, CacheKeyList, Config>` template with the type-level composition shape
- [ ] `start()` constructs modules in declaration order, then calls `on_start()` in declaration order
- [ ] `run()` pumps the queue blocking on a host CV when idle
- [ ] `run_one()` drains the queue exactly once and returns (correct behavior on empty queue: returns without blocking)
- [ ] `stop()` sets a stopping flag; `shutdown()` drains queue (bounded by configurable drain budget), then `on_stop()` reverse order, then destruct reverse order
- [ ] Drain budget exhaustion logs a `WARN` trace and aborts remaining drain (does not assert)
- [ ] Integration test: two toy modules exchange a message via `send`, observed via `run_one`
- [ ] Integration test: `on_start` / `on_stop` invocation order verified across three toy modules

## Blocked by

- `05-module-base-dispatch.md`

## Comments

### 2026-05-14 — from sandcastle agent

**What was built:**

- `include/cortexflow/runtime.hpp` — header-only `Runtime<ModuleList, CacheKeyList, Config>` template and composition primitives:
  - `ModuleList<Modules...>`, `CacheKeyList<Keys...>`, `Owned<Key, Owner>` (documentation-only in v1 per ADR-018), `MaxSubscriptions<N>`, `DrainBudget<N>`, and `Config<Opts...>` with a SFINAE-based linear option lookup (`detail::find_option`). Defaults: `kMaxSubscriptions = 16`, `kDrainBudget = SIZE_MAX`.
  - `Runtime` owns `std::optional<modules_tuple>` for the module pack, a `std::mutex` + `std::condition_variable` + `std::deque<Envelope>` for the FIFO queue, and a `std::atomic<bool>` stop flag. Public API: `start`, `run_one`, `run`, `stop`, `shutdown`, thread-safe `post`, plus a `get<Module>()` test accessor and `queue_size()` introspection.
  - Two-phase init: `start()` constructs all modules (in-place tuple construction = declaration order), binds each module's post sink to the runtime's post trampoline, then calls `on_start()` on each in declaration order via index-sequence fold.
  - `run_one()` drains the queue until empty (no blocking on empty queue); messages posted during dispatch are picked up in the same call. `run()` waits on the CV when idle, exits cleanly when `stop()` is signaled and the queue is empty.
  - `shutdown()` drains up to `kDrainBudget` envelopes; if the queue still has items after the budget, emits `CORTEXFLOW_TRACE_WARN("drain", ...)` and clears the remaining queue (does not assert). Then `on_stop()` reverse declaration order, then `modules_.reset()` destructs in reverse via the tuple destructor.

- `include/cortexflow/module.hpp` — added `Module<Derived>::send<Target>(msg)`. Constructs an envelope addressed by `type_id<Target>()` and routes through the module's bound post sink. Matches the `reply_to` style (msg passed by value, perfect-forwarded into `make_message`). Compile-time validation that `Target` is in `ModuleList` is deferred to issue 08 (composition validator).

- `tests/integration/test_runtime.cpp` — eleven integration tests covering:
  - Two-module `send` round-trip observed by `run_one` (the spec's primary integration test).
  - `run_one` on empty queue returns without blocking.
  - `run_one` drains messages posted during dispatch.
  - `on_start` / `on_stop` invocation order across three modules (declaration / reverse declaration).
  - `shutdown` drains the queue before `on_stop`.
  - `DrainBudget<2>` exhaustion emits the WARN trace and aborts the remaining drain (does not assert).
  - Foreign-thread `post()` is thread-safe.
  - `run()` returns cleanly after `stop()` with queue drained (background-thread test).
  - Double `start()` asserts.
  - Envelope addressed to a module outside the `ModuleList` asserts during dispatch.

- `tests/CMakeLists.txt` — wired `test_runtime` integration target.

**Verification:**

- `ctest --output-on-failure`: 9/9 pass (`type_name`, `assert`, `trace`, `trace_elision`, `messaging`, `module`, `runtime`, `copy_envelope_fails`, `copy_message_ptr_fails`).
- Built and tested at both `CORTEXFLOW_TRACE_LEVEL=DISPATCH` (default) and `CORTEXFLOW_TRACE_LEVEL=FULL`. Trace points stay syntactically valid in both modes.

**Design notes for reviewer:**

- The "no interaction during construction" rule is enforced at runtime by `ModuleBase::post`'s assertion that `post_fn_ != nullptr`. The runtime binds post sinks *after* all modules have been constructed but *before* any `on_start()` runs, so any attempt to `send` or `post` from a module constructor will fault.
- `dispatch()` does a linear `module_type_id() == to` scan over the module tuple. With one instance per module type and a small module count (single digits in v1 compositions), this is acceptable. A type-id → handler pre-computed table is a future optimization but not needed yet.
- The `Config` option search uses two member-detection traits (`drain_budget_of`, `max_subs_of`) plus a recursive `find_option` template. This keeps the per-option types simple (`DrainBudget<N>` is just a struct with one constexpr member) while supporting an arbitrary option order in the `Config<...>` list.
- `Runtime` is non-copyable and non-movable (its mutex and condition variable forbid it). Modules are owned by value in a `std::optional<std::tuple<...>>` so that we can control the *timing* of construction (in `start()`) and destruction (in `shutdown()`), independently of `Runtime`'s own lifetime.
- `shutdown()` clears any envelopes remaining after drain-budget exhaustion. Their destructors release the message memory; no leak. The `WARN` trace is the only externally observable signal.
- `run()` calls `run_one()` once after the CV wake; the post-stop path then drains once more in case messages were posted between the CV check and the actual drain. This avoids losing the final batch when `stop()` and a foreign `post()` race.

**Nothing skipped or deferred** within the slice's scope. Cache, timers, flow, and the composition validator are explicitly later slices (09–15) — `CacheKeyList<>` is accepted as empty in this slice.
