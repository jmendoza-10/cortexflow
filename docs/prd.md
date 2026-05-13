# PRD — CortexFlow v1

**Target tracker labels:** `ready-for-agent`
**Companion design reference:** [`architecture.md`](./architecture.md)

---

## Problem Statement

Engineers building embedded control-plane systems re-implement the same scaffolding on every project: an event loop, inter-module messaging, lifecycle management, timer scheduling, and OS abstraction. Each re-implementation picks up the same family of subtle bugs — races in the message queue, allocator misuse on bare-metal, brittle init ordering, string-based message IDs that drift out of sync between sender and receiver, and side effects that make tests slow and flaky.

Even when the scaffolding works for one target, porting between bare-metal MCUs, RTOS environments, and POSIX devices is painful because OS primitives (threads, locks, file descriptors, allocators) leak through module boundaries. The result is per-target forks of the same control-plane logic, divergent in ways that only show up in integration.

There is no off-the-shelf C++ framework that prioritizes all of the following together: deep modules with a simple interface, deterministic single-threaded run-to-completion, RAII-managed resource lifetimes, type-derived identity, and embedded-class portability across host, RTOS, and bare-metal — without RTTI, without exceptions in the core, and without hidden allocations.

## Solution

A C++17 (C++20-where-available) framework providing three composable primitives plus a typed data cache:

1. **Runtime** owns the event loop, the single FIFO message queue, the data cache, the timer service, and every module instance. It exposes a blocking `run()` for production and a `run_one()` for tests.
2. **Modules** are typed handler classes. One instance per type. Compile-time addressing (`send<TargetModule>(args...)`). Modules expose a small message surface; their internal complexity is hidden behind a single `handle(Envelope&)` entry point.
3. **Flowcharts** are re-entrant state machines owned by a module. State functions return a `StateDirective` indicating stay, transition, immediate transition (reusing the current envelope), or done. Per-state local data has framework-managed RAII lifetime, so subscriptions and timers held as state-locals are released automatically on transition.
4. **Smart data cache** is a typed key-value store with dynamic RAII subscriptions. When a key changes (per `operator==`), the framework posts `KeyChanged<K>` to all subscribers, who route it into their flow.

Composition is a single type-level declaration listing modules, cache keys, ownership, and pool sizes — so mis-wirings are compile errors at the composition site. Platform-specific implementations (allocator, timer backend, trace sink) are swapped at build time via typedefs. Tests drive the system tick-by-tick with a `ManualClock`; no sleeps, no real I/O.

## User Stories

### Composing and running a system

1. As an embedded engineer, I want to declare my system's modules and cache keys in a single type, so that the entire system shape is visible at one location.
2. As an embedded engineer, I want mis-wirings (duplicate modules, dangling owners, missing handlers) to be compile errors, so that I catch them before the firmware boots.
3. As an embedded engineer, I want my `main()` to be just `start(); run(); shutdown();`, so that production code is trivial.
4. As an embedded engineer, I want the runtime to construct all modules before any `on_start()` runs, so that modules can freely interact during start without ordering bugs.
5. As an embedded engineer, I want a graceful shutdown that drains queued messages, so that in-flight work isn't lost on `stop()`.
6. As an embedded engineer, I want a configurable drain budget on shutdown, so that a misbehaving module can't stall teardown indefinitely.

### Messaging

7. As an embedded engineer, I want to send a typed message to another module by its type, so that I never have to maintain a central registry of message IDs.
8. As an embedded engineer, I want the framework to reject at compile time a send to a module that does not handle the given message type, so that wiring bugs surface immediately.
9. As an embedded engineer, I want every envelope to carry a `from` sender ID, so that any module can choose to reply.
10. As an embedded engineer, I want a `reply_to(envelope, ...)` helper, so that I don't manually re-address the response.
11. As an embedded engineer, I want messages to be moved (not copied) through the queue, so that move-only payloads (`unique_ptr` to OS-supplied buffers, for example) are supported.
12. As an embedded engineer, I want the queue to be a single FIFO with no priority lanes, so that ordering is deterministic.
13. As an embedded engineer, I want the framework to abort if the message allocator overflows on bare-metal, so that I learn about under-provisioning instead of fighting silent failures.

### Identity and tracing

14. As an embedded engineer, I want each message and cache key to be its own C++ type with no separate enum declaration, so that the type system is the single source of truth.
15. As an embedded engineer, I want `type_name<T>()` to give me a fully qualified, stable name for each type, so that trace output is human-readable.
16. As an embedded engineer, I want CI to verify that `type_name<T>()` produces the same canonical form on every supported toolchain, so that compiler upgrades don't silently shift identity.
17. As an embedded engineer, I want a six-level trace hierarchy from `OFF` to `FULL`, so that I can dial verbosity per build (debug verbose, release minimal).
18. As an embedded engineer, I want the trace level to be selected at compile time so that higher-level trace points compile to nothing in release builds.
19. As an embedded engineer, I want a pluggable trace sink with sensible defaults per target, so that bare-metal builds can route traces to UART/SWO/RTT without modifying the framework.

### Cache

20. As an embedded engineer, I want each cache key to be a typed class declaring its `value_type`, so that reads return a strongly-typed `optional` and writes are checked.
21. As an embedded engineer, I want the cache to fire notifications only when the value actually changes per `operator==`, so that idempotent writes don't cause notification storms.
22. As an embedded engineer, I want to read whether a key has ever been written, so that I can distinguish "unset" from a real value.
23. As an embedded engineer, I want to declare which module owns each cache key in the composition, so that the producer of any value is documented in one place.
24. As an embedded engineer, I want to subscribe to a key's changes dynamically and have the subscription auto-release on RAII destruction, so that I never leak subscription slots.
25. As an embedded engineer, I want to hold a subscription as a member of my flow's state-locals, so that the subscription's lifetime aligns with that state automatically.
26. As an embedded engineer, I want the subscription pool to assert on overflow, so that under-provisioning is caught loudly.
27. As an embedded engineer, I want subscribers to receive `KeyChanged<K>` as a normal message routed via the queue, so that there are no synchronous callbacks during a writer's handler.
28. As an embedded engineer, I want subscriptions created during a writer's handler to not see that write, so that re-entrancy stays clean under RTC.

### Flowcharts

29. As an embedded engineer, I want my module to own a single flowchart whose internal state is opaque to the module, so that the module's interface stays small even when the behavior is complex.
30. As an embedded engineer, I want my state functions to return one of `stay`, `transition_to(next)`, `transition_to_now(next)`, or `done`, so that the control flow is explicit and self-documenting.
31. As an embedded engineer, I want `transition_to_now` to reuse the current envelope when invoking the next state, so that chained logic doesn't require a fresh trigger.
32. As an embedded engineer, I want each state to declare a typed state-locals struct, so that all data tied to that state has scoped lifetime.
33. As an embedded engineer, I want state-locals to be destructed on transition and the new state's locals constructed in place, so that RAII members (subscriptions, timers) follow state lifetime.
34. As an embedded engineer, I want the framework to allocate the state-locals buffer at compile time, sized to the largest state's locals across the flow, so that there's no runtime allocation per transition.
35. As an embedded engineer, I want the framework to dispatch a synthetic init envelope into the initial state once on `flow.start()`, so that initial subscriptions/timers can be set up immediately.
36. As an embedded engineer, I want `done()` to destruct locals and synchronously call `on_flow_done()` on the owning module, so that I can react to completion before any other dispatch.
37. As an embedded engineer, I want my module to call `flow.terminate()` from outside a `step()`, so that I can force-end a flow when a higher-level condition demands it.
38. As an embedded engineer, I want `flow.restart()` to begin a fresh run from the initial state, so that flows are reusable across episodes.
39. As an embedded engineer, I want any state function to be a legal "next" for any other state function, so that flow shapes can be dynamic and not constrained to a static graph.

### Time

40. As an embedded engineer, I want to arm a `Timer` as a state-local member with a duration and a message, so that timers' lifetimes follow the state.
41. As an embedded engineer, I want timer expiry to post a normal message into the queue, so that the timer's effect is dispatched on the same thread as everything else.
42. As an embedded engineer, I want my clock to be injected into the runtime at construction, so that I can swap a `SteadyClock` for production and a `ManualClock` for tests.
43. As a test author, I want `ManualClock::advance(duration)` to fire any due timers as posted messages, so that I can step through time deterministically.

### Platform portability

44. As an embedded engineer, I want to select my platform target at build time (`host`, `posix`, `freertos`, `bare_metal`), so that the same module code runs on every supported environment without `#ifdef` walls in module code.
45. As an embedded engineer, I want platform-specific backends (allocator, timer service, trace sink) to be typedef-swapped, so that the composition references a single `platform::Backend` name that resolves to the right implementation.
46. As an embedded engineer, I want boundary modules to be conventional modules that own internal threads for foreign work, so that the boundary between in-loop and foreign code is named clearly.
47. As an embedded engineer, I want `runtime.post(envelope)` to be thread-safe so that boundary threads enqueue work without bespoke synchronization.
48. As a contributor, I want to port the framework to a new platform by writing new backends and a CMake target file, so that adding a target doesn't require core changes.

### Errors and faults

49. As an embedded engineer, I want a single `CORTEXFLOW_ASSERT(cond, reason)` primitive used by both the framework and my modules, so that assertions surface through one channel.
50. As an embedded engineer, I want fault handling to dispatch through a weak-linked platform handler, so that bare-metal builds can override behavior (typically: disable interrupts, log, reset).
51. As an embedded engineer, I want every framework-level error to be a system invariant violation that aborts cleanly, so that I never need to thread error codes through send/post/subscribe APIs.

### Testing

52. As a test author, I want a `run_one()` entry point that drains the queue once and returns, so that I can step a test scenario message-by-message.
53. As a test author, I want all integration tests to use `ManualClock` and `run_one()`, so that tests are deterministic and finish in milliseconds.
54. As a test author, I want to replace platform backends in tests via typedef-swap, so that I can substitute mocks for OS APIs without modifying production composition.
55. As a test author, I want compile-fail tests for mis-wirings (duplicate modules, dangling owners, missing handlers), so that the composition validator's failure modes are themselves under test.
56. As a contributor, I want CI to run every test with `TRACE_FULL` enabled, so that trace points are syntax-checked regardless of the default level.

### Documentation

57. As a contributor, I want each major design decision recorded as a short ADR, so that future contributors can understand why the framework is shaped the way it is without re-litigating.
58. As an onboarding engineer, I want a minimal example app in the repo, so that I have a working reference for composition, modules, and a flowchart out of the box.

## Implementation Decisions

The companion `architecture.md` is the source of truth for design rationale. The decisions below are the ones that directly shape the v1 build.

### Subsystem boundaries (consolidated)

The v1 implementation is organized around the following subsystems. Each is folded with its supporting types rather than split across many narrow components; refactor out only if a concept grows large enough to justify its own surface.

- **Runtime** — top-level container template `Runtime<ModuleList, CacheKeyList, Config>`. Subsumes composition validation (static_assert), module tuple storage, queue ownership, post-from-foreign-thread ingress, lifecycle (`start` / `run` / `run_one` / `stop` / `shutdown`), accessors for cache and timers.
- **Module** — base interface (`handle`, `on_start`, `on_stop`, `on_flow_done`) plus the per-module dispatch table generated from the declared `Inbox`.
- **Messaging** — `Envelope`, `MessagePtr`, and the pluggable allocator. Single `make_message<T>(args...)` factory. Thread-safe for foreign posts.
- **Cache** — typed key-value slots, subscription registry, RAII `Subscription` handle. Fires `KeyChanged<K>` to subscribers in registration order on real change.
- **Flow** — state-function execution, state-locals aligned-storage buffer (constructed/destructed on transition), directive interpretation, synthetic init envelope dispatch, lifecycle (`start` / `step` / `terminate` / `restart`), `done` → `on_flow_done` synchronous callback path.
- **Time** — `Clock` interface, `SteadyClock`, `ManualClock`, `TimerService` runtime-level facility, `Timer` value type for state-locals.
- **Identity** — `type_name<T>()` constexpr utility parsing compiler-specific function-signature macros; produces fully-qualified canonical names used as message/key/module identity and trace strings; constexpr-hashed to `type_id_t`.
- **Trace** — six-level hierarchy (`OFF` / `ERROR` / `WARN` / `INFO` / `DISPATCH` / `FULL`); compile-time selection via `if constexpr`; pluggable sink with per-target defaults; weak-linked override on bare-metal.
- **Fault** — `FRAMEWORK_ASSERT(cond, reason)` macro → `[[noreturn]] fault(...)` → platform-pluggable handler.
- **Platform backends** — selected at build time via typedef-swap. Each target ships allocator, timer backend, trace sink. Boundary modules (CAN, socket, etc.) are also typedef-swapped per target.

### Concurrency model

- Single-threaded run-to-completion. One thread runs the loop. Handlers complete or yield from a flow before another dispatch starts. No locks inside modules.
- Foreign threads enqueue via `runtime.post(envelope)`. Queue is guarded by `std::mutex` + `std::condition_variable`. RTOS targets map to native queue primitives with the same semantics. Bare-metal without RTOS uses critical-section + ring buffer + WFI.
- Allocator must be thread-safe for foreign use (locked slab pool on bare-metal).

### Composition shape

```cpp
using App = cortexflow::Runtime<
  cortexflow::ModuleList<
    IgnitionMonitor,
    ChargeController,
    BatteryManager,
    platform::CanAdapter,
    platform::TimerBackend
  >,
  cortexflow::CacheKeyList<
    cortexflow::Owned<VehicleSpeed,   SpeedSensor>,
    cortexflow::Owned<BatteryLevel,   BatteryManager>,
    cortexflow::Owned<ChargingActive, ChargeController>
  >,
  cortexflow::Config<
    cortexflow::MaxSubscriptions<32>
  >
>;
```

This shape is locked. `Owned<K, M>` is documentation-only in v1 (no `static_assert`); a compile-time enforcement is a one-line addition reserved for if/when convention violations appear.

### State directive encoding

State functions return one of four directives. The encoding is:

```cpp
struct StateDirective {
  enum class Kind { Stay, Transition, TransitionNow, Done };
  Kind kind;
  StateFn next;   // valid when kind == Transition or TransitionNow
};

StateDirective stay();
StateDirective transition_to(StateFn next);
StateDirective transition_to_now(StateFn next);
StateDirective done();
```

`transition_to_now` re-enters `next` immediately with the same envelope; the next state may use or ignore it. No chain-depth limit is enforced — flow designers are responsible for avoiding infinite chains.

### Message identity

Every message inherits from a CRTP base that attaches `static constexpr std::string_view kName = type_name<Self>();` and a constexpr `kTypeId` derived from a hash of `kName`. There is no central enum file. Cross-toolchain stability of `type_name<T>()` output is verified by a CI fixture.

### Allocation strategy

Pluggable allocator behind `make_message<T>(args...)` and `MessagePtr<T>`. Defaults per target: host = heap; bare-metal = per-type compile-time slab pool; FreeRTOS = configurable. Pool overflow on bare-metal is a `FRAMEWORK_ASSERT` (system failure). The allocator interface includes locking primitives for thread-safe use from foreign threads.

### Reply semantics

Sender ID lives in the envelope (`from`, nullable via a sentinel). `reply_to(envelope, ...)` sugar sends back to `envelope.from`. There are no futures, correlation IDs, or synchronous waits in v1. Flows wait for replies by entering a state whose handlers only respond to the expected reply type.

### Lifecycle

Two-phase: construct all modules in declaration order, then call `on_start()` on each in declaration order. Modules may not interact with each other during construction. Symmetric on shutdown: drain queue (bounded by drain budget) → `on_stop()` in reverse order → destruct in reverse order.

### Timer service

Runtime-level facility (not a module). `Timer` is a value type held in state-locals; construction calls `runtime.timers().arm(...)` synchronously (thread-safe, does not require a queue round-trip); destruction calls `cancel`. The backend is platform-specific and selected at build time.

### Tracing

Trace points use `if constexpr` against a build-time-selected level constant. `OFF` compiles every trace call to a no-op the optimizer drops. Default coverage by level:

- `DISPATCH`: each envelope dispatched (from → to, message type name)
- `FULL`: + cache writes (key, old, new) + state transitions + timer arm/cancel/fire

CI runs all tests with `FULL` enabled to ensure trace points stay syntactically valid.

## Testing Decisions

### What makes a good test

- **Test external behavior, not implementation details.** A test for the cache asserts on `get` results and on the `KeyChanged<K>` messages observed by a subscriber — not on the internal slot representation.
- **Deterministic.** No real time, no real I/O. All time advances through `ManualClock::advance(duration)`. Boundary modules are replaced by test doubles via typedef-swap.
- **Drives via public API.** Integration tests post envelopes and step `run_one()` rather than poking module internals. Unit tests construct the subsystem under test and exercise its public surface.
- **Self-documenting.** Each test name reads as a behavioral claim ("`charging_timeout_fires_after_30s`"), and the body is a `setup → act → assert` triple.

### Coverage target

The goal is **full coverage across every subsystem**. The list below names what each subsystem's coverage must include at minimum; additional tests are encouraged. No subsystem is excluded from coverage — shallow value types (`Envelope`, `StateDirective`, `Subscription` handle, etc.) are exercised by the tests of the subsystems that consume them, and any standalone behavior they have (e.g., move semantics, sentinel values, equality) gets a focused unit test.

### Coverage by subsystem (folded)

- **Runtime** — two-phase init order, `on_start` / `on_stop` ordering, drain-on-shutdown, `run_one` correctness on empty/non-empty queues, thread-safe `post()` from a foreign thread.
- **Module** — dispatch-table correctness: declared inbox messages reach the right handler overload; messages outside the inbox assert; identity matches across send/receive.
- **Messaging** — queue concurrency (foreign-thread post + main-thread drain), wake semantics under stop, allocator paths per backend (heap and slab), pool-overflow assert on bare-metal allocator.
- **Cache** — `get` returns unset before first write, `set` round-trips, change-detection skips equal writes, fanout posts `KeyChanged<K>` to all subscribers in registration order, subscribe-during-write does not observe current write, subscription RAII drop releases slot, subscription-pool overflow asserts.
- **Flow** — state-locals construct on entry, destruct on transition (verified via RAII probes), `transition_to_now` reuses the current envelope, `done` synchronously calls `on_flow_done`, `restart` begins from initial state with fresh locals, `terminate` inside `step` asserts, init envelope reaches initial state with `from = ModuleId::system()`.
- **Time** — `Timer` arms on construction and cancels on destruction, `ManualClock::advance` fires due timers as posted messages, timers armed during firing of others do not fire in the same `advance` window.
- **type_name** — canonical-form fixture verifying expected output for representative types across every supported toolchain (this is the CI cross-compiler stability check).

### Compile-fail tests

A `tests/compile_fail/` directory contains snippets the build expects to reject. Coverage:

- Duplicate module type in `ModuleList`.
- `Owned<K, M>` where `M` is not in `ModuleList`.
- `send<TargetModule>(msg)` where `TargetModule` is not in `ModuleList`.
- `send<TargetModule>(msg)` where `TargetModule` does not declare a handler for `Msg`.

### Integration tests

A minimal composed system (also used as `examples/minimal_app`) is the integration fixture: two modules, one cache key, one flow with two states, one timer. End-to-end scenarios drive through `run_one()` + `ManualClock`.

### Test framework

doctest, header-only. No link dependency. Drop-in.

### Prior art

This is a greenfield framework, so there is no in-repo prior art to mirror. Test style and structure should follow the conventions of mature C++ embedded test suites: one top-level `tests/` directory split into `unit/`, `integration/`, and `compile_fail/`; one test target per subsystem; doctest's `TEST_CASE` / `SUBCASE` for grouping; deterministic naming.

## Out of Scope

- **Multiple flowcharts per module.** v1 is strictly one flowchart per module. Splitting concerns into multiple modules is the answer for now.
- **Compile-time enforcement of cache-key write ownership.** `Owned<K, M>` is documentation only in v1; the `static_assert` is reserved for a future addition.
- **Request/response with futures and correlation IDs.** Replies are normal sends to the envelope's `from`. Flows wait by entering a dedicated state.
- **Custom subscription→message mapping.** Subscribers always receive `KeyChanged<K>`.
- **Priority lanes in the dispatch queue.** Single FIFO.
- **`on_idle()` callback hook.** Idle is a CV wait; no user hook.
- **Hierarchical cache-key paths or namespaces.** Keys are flat typed classes.
- **Per-trace-level granularity** (e.g., `FULL` with timer events off). Coarse level only.
- **Backwards-compat shims for evolving message schemas.** v1 assumes the build is the unit of versioning.
- **Dynamic module instantiation.** All modules are statically composed.
- **Cross-process message routing.** The runtime is in-process by definition.
- **Rate-limiting on the queue or pools.** Floods are explicit system failures, not framework-handled.

## Further Notes

### Design reference

The companion file `architecture.md` captures every decision locked during design, including rationale and rejected alternatives. The PRD above is the implementation contract; the design doc is the reasoning. Both should travel together into the repo (suggested location: `docs/architecture.md` for the design doc).

### ADRs

Nineteen ADR stubs are listed in the design doc's decision log. Worth writing the first batch (especially 001-010) early in implementation, while rationale is still fresh. ADRs are short — one page each: context, decision, consequences, alternatives, rejected because.

### Repo structure

The proposed layout in the design doc places public API under `include/cortexflow/`, non-template implementation under `src/cortexflow/`, platform backends in a parallel `platform/<target>/` tree, tests under `tests/{unit,integration,compile_fail}/`, and a minimal example app under `examples/minimal_app/`. The example app is built in CI from day one and doubles as the integration fixture.

### Build flags

Two CMake flags drive build configuration: `CORTEXFLOW_TARGET={host,posix,freertos,bare_metal}` selects the platform backend, and `CORTEXFLOW_TRACE_LEVEL={OFF,ERROR,WARN,INFO,DISPATCH,FULL}` selects the trace verbosity. A third flag `CORTEXFLOW_BUILD_TESTS={ON,OFF}` is `OFF` by default (so cross-compile builds don't pull doctest unnecessarily) and `ON` for host builds.

### Polish areas worth front-loading

- **Static_assert messages.** The composition site is where every user starts. Static_assert failure messages must be clearly worded ("`X` declared as writer of `Y` but `X` is not in `ModuleList`"). Cryptic template errors here are a major friction point.
- **type_name<T>() canonical-form fixture.** Set up the CI stability check before the framework relies on `type_name` for identity in production code.
- **Trace label conventions.** Once trace output exists, decide a one-line format and stick with it (timestamp, level, kind, from, to, type name, key fields). Reusable across sinks.

### Tracker filing

This PRD is written to be filed as a single issue in whichever tracker the project ends up using. Apply the `ready-for-agent` triage label on filing. No further triage required.
