# CortexFlow — Design Reference

**Status:** Design v1, drafted 2026-05-12
**Scope:** C++ framework for embedded control-plane state management.
**Language:** C++17 minimum, C++20 features where available.
**Targets:** Host (development), POSIX/Linux, FreeRTOS/Zephyr, bare-metal MCUs.

---

## 1. Mission

A C++ framework for building **control-plane** systems whose job is to manage state, not move bulk data. Three composable primitives plus a shared data cache provide the entire programming surface:

1. **Runtime** — owns the event loop, the message queue, the data cache, the timer service, and every module instance.
2. **Modules** — typed classes that handle messages and (optionally) host a flowchart. One instance per type.
3. **Flowcharts** — re-entrant state machines built from function pointers that return the next state to enter.
4. **Smart data cache** — typed key-value store with RAII subscriptions; the third communication channel between modules.

Workload characteristics: low-throughput, state-driven, latency-tolerant. Not a data plane.

---

## 2. Architectural principles

These principles override convenience when they conflict with it.

1. **Deep modules, simple interfaces.** Modules hold significant internal complexity (the whole flowchart, its state-locals, its subscriptions, its timers) behind a small message surface. If a decision splits work across module boundaries, prefer to push it inside one module instead.
2. **Single-threaded run-to-completion.** One thread runs the loop. Handlers run to completion or to a flowchart yield. No locks inside modules.
3. **Type-derived identity.** No central enums for message IDs, cache keys, or module IDs. The C++ type *is* the identity. `type_name<T>()` generates the human-readable name.
4. **Static composition.** The system's shape — which modules exist, which cache keys exist, who owns what — is expressed as a single type at compile time. Mis-wirings are compile errors at the composition site.
5. **Portability without runtime polymorphism.** Platform-specific implementations are swapped at build time via typedefs and conditional compilation. No vtables across the platform seam.
6. **RAII for every framework-managed resource.** Subscriptions, timers, state-locals. Construct = acquire; destruct = release. Lifetime is the design.
7. **No exceptions in the core. No RTTI.** Both incompatible with our target toolchains.
8. **Determinism on demand.** Production runs on a real clock; tests run on a `ManualClock`. The loop exposes `run_one()` so tests can step the system one dispatch at a time. No sleeps in tests.
9. **Flooding is failure.** No rate-limiting machinery; if a queue or pool overflows, the system is broken. The framework asserts and stops; recovery is the system designer's choice via the fault handler.

---

## 3. System overview

```
              ┌─────────────────────────────────────┐
              │              Runtime                │
              │  ┌──────────┐    ┌────────────┐     │
              │  │  Queue   │    │   Cache    │     │
              │  │ (FIFO)   │    │  (typed)   │     │
              │  └────┬─────┘    └──────┬─────┘     │
              │       │                 │           │
              │  ┌────▼──── Dispatch ───▼────┐      │
              │  │                            │     │
              │  │   Module A   Module B  ... │     │
              │  │   (flow)     (flow)        │     │
              │  └────────────────────────────┘     │
              │           Timer Service             │
              └──────┬───────────────────────┬──────┘
                     │ post()                ▲ wake (CV)
                     ▼                       │
              ┌─────────────────────────────────────┐
              │      Boundary Modules (own threads) │
              │   I/O, OS callbacks, hardware ints  │
              └─────────────────────────────────────┘
```

Everything inside the runtime box runs on the loop thread. The boundary modules are the only places threading lives.

---

## 4. Runtime and event loop

### 4.1 Loop model

- Single thread of execution.
- One FIFO message queue.
- `while (!stopped) { wait(); run_one(); }`
- `run_one()` drains the queue once, dispatches each `Envelope` to its target module's `handle()`, returns when empty.

### 4.2 Wake primitive

The queue is guarded by a `std::mutex` and a `std::condition_variable`. Foreign threads acquire the lock, push, and `notify_one()`. The loop's `wait()` is `cv.wait(lock, [&]{ return !q.empty() || stop_; })`.

| Target       | Implementation                                  |
|--------------|--------------------------------------------------|
| Host/POSIX   | `std::mutex` + `std::condition_variable`        |
| FreeRTOS     | `xQueueReceive` with infinite timeout           |
| Zephyr       | `k_msgq_get` with `K_FOREVER`                   |
| Bare-metal   | critical section + ring buffer + WFI            |

No eventfd, no epoll integration. I/O readiness on POSIX is translated to messages by a boundary module's internal thread.

### 4.3 Loop entry points

```cpp
class Runtime {
public:
  void start();      // construct + on_start() all modules
  void run_one();    // drain queue once; no waiting
  void run();        // start + (wait + run_one) loop until stop()
  void stop();       // signal exit; loop drains then returns
  void shutdown();   // on_stop() reverse order + destruct
};
```

Production: `start(); run(); shutdown();`. Tests: `start(); run_one(); ...; shutdown();`.

### 4.4 Shutdown semantics

`stop()` sets a flag. The currently-dispatching handler completes; the loop continues draining envelopes, bounded by a **drain budget** (default `SIZE_MAX` = drain fully). Once drained or budget exhausted, `on_stop()` runs in reverse declaration order, then modules destruct.

---

## 5. Module model

### 5.1 Identity and addressing

- **One instance per module type.** Enforced by the type-list composition.
- **Compile-time addressing.** `send<TargetModule>(args...)` resolves at the call site; `static_assert` checks that `TargetModule` declares a handler for the message type.
- **No string IDs in the dispatch path.** Names exist only for tracing.

### 5.2 Module interface

```cpp
class Module {
public:
  virtual ~Module() = default;

  // Phase 1: construct your members only. No system access.
  Module();

  // Phase 2: subscribe, post initial messages, start your flow.
  virtual void on_start() {}

  // Pre-teardown: cancel work, terminate flow.
  virtual void on_stop() {}

  // Single dispatch entry.
  virtual void handle(Envelope&) = 0;

  // Hooks
  virtual void on_flow_done(Flow&) {}
};
```

### 5.3 Boundary modules

Boundary modules implement the *same* `Module` interface. By convention (not enforcement), only boundary modules spawn threads. An I/O thread:

- Does foreign work (epoll, library callbacks, hardware register polling).
- Translates results to internal messages.
- Calls `runtime.post(envelope)` to enqueue.

`on_start()` spawns the thread; `on_stop()` joins it. The allocator must be thread-safe for foreign use (locked slab on bare-metal).

Platform-specific boundary modules are swapped at build time:

```cpp
// platform/posix/can_adapter.hpp
namespace platform { using CanAdapter = PosixCanAdapter; }

// platform/freertos/can_adapter.hpp
namespace platform { using CanAdapter = FreeRtosCanAdapter; }
```

The composition lists `platform::CanAdapter`; the build chooses which header is included.

---

## 6. Messages and envelopes

### 6.1 Envelope

```cpp
struct Envelope {
  ModuleId to;
  ModuleId from;          // ModuleId::none() for platform-injected / fire-and-forget
  MessagePtr payload;     // owning smart ptr; pluggable allocator
};
```

`from` is mandatory but nullable. Replying to `ModuleId::none()` is silently dropped or asserted, build-configurable.

### 6.2 Message identity

Every message is a C++ class. A CRTP base attaches:

- `static constexpr std::string_view kName = type_name<T>();`
- `static constexpr type_id_t kTypeId = constexpr_hash(kName);`

No central enum. Identity follows the class.

### 6.3 The `type_name<T>()` utility

A constexpr function parses `__PRETTY_FUNCTION__` (GCC/Clang) or `__FUNCSIG__` (MSVC) to produce a fully-qualified type name string. Used by:

- Message identity (`kName`, `kTypeId`)
- Cache key identity
- Module identity (for tracing)

**Required CI check:** a fixture verifying that `type_name<T>()` produces the expected canonical form on every supported toolchain. A compiler upgrade that shifts `__PRETTY_FUNCTION__` formatting must be caught before runtime.

### 6.4 Dispatch

The queue holds `Envelope`s. The loop pops an envelope, looks up `envelope.to`, and calls that module's `handle(envelope)`. Inside the module, dispatch is via a **per-module dispatch table** built at startup from the module's declared inbox:

```cpp
class IgnitionMonitor : public Module {
public:
  using Inbox = MessageList<IgnitionOn, IgnitionOff, KeyChanged<VehicleSpeed>>;

  void handle(IgnitionOn&);
  void handle(IgnitionOff&);
  void handle(KeyChanged<VehicleSpeed>&);
};
```

The framework generates a `type_id → handler` table at module construction. Dispatch cost is one indirect call after one hash lookup. No RTTI, no `dynamic_cast`, no `any_cast`.

### 6.5 Memory: pluggable allocator

`MessagePtr<T>` is an owning smart pointer over an allocator interface. Defaults per build target:

| Target       | Allocator                                 |
|--------------|-------------------------------------------|
| Host/POSIX   | `new`/`delete`                            |
| Bare-metal   | Per-type slab pool, compile-time sized    |
| FreeRTOS     | Per-type slab or RTOS heap, configurable  |

Pool overflow on bare-metal = `FRAMEWORK_ASSERT` (system failure). The allocator API includes a `lock()`/`unlock()` pair for foreign-thread safety.

`make_message<T>(args...)` is the only factory. Modules never see `new` or `delete`.

### 6.6 Reply semantics

```cpp
template <class Target, class Msg, class... Args> void send(Args&&...);
template <class Msg, class... Args>                void reply_to(const Envelope& in, Args&&...);
```

`reply_to` sets `to = in.from`. No futures, no correlation IDs, no synchronous wait. If a module needs to wait for a reply, its flowchart enters a state whose only action is handling the reply message.

---

## 7. Smart data cache

### 7.1 Schema

Each key is a C++ type declaring its value type:

```cpp
struct VehicleSpeed {
  using value_type = float;
};

struct ChargingActive {
  using value_type = bool;
};
```

Storage is a `std::tuple<Slot<Keys>...>`, statically allocated, sized at composition.

### 7.2 Read / write API

```cpp
cache.set<VehicleSpeed>(42.0f);             // overwrites slot
auto v = cache.get<VehicleSpeed>();         // returns std::optional<float>
```

Slot starts "unset"; first write is always a change.

### 7.3 Change semantics

Notifications fire when `new_value != old_value` (per `operator==`). A per-key comparator override is permitted but not required in v1. Same-value writes are silent.

### 7.4 Ownership

The composition declares one writer per key:

```cpp
CacheKeyList<
  Owned<VehicleSpeed,   SpeedSensor>,
  Owned<BatteryLevel,   BatteryManager>,
  Owned<ChargingActive, ChargeController>
>
```

**Convention only in v1.** Any module *can* call `cache.set<K>(v)`. Code review enforces the convention. A `static_assert` enforcement is a one-line addition if needed later.

### 7.5 Subscriptions

Dynamic, RAII-managed:

```cpp
struct ChargingActiveLocals {
  Subscription speed_sub = cache.subscribe<VehicleSpeed, /*subscriber*/ ChargeController>();
  Timer        timeout   = make_timer(30s, MakeMessage<ChargeTimeout>{});
};
```

- **Storage:** per-cache compile-time pool, sized to maximum *concurrent* subscriptions.
- **Identification:** RAII `Subscription` handle. Move-only. Drop releases the slot.
- **Fanout order:** registration order. Documented as "do not depend on this."
- **Subscribe-during-write:** new subscriptions made inside a writer's handler do **not** see this write; they see subsequent ones.
- **Overflow:** subscription pool exhaustion = `FRAMEWORK_ASSERT` (system failure).

### 7.6 Notification payload

When a key changes, the framework posts `KeyChanged<K> { old_value, new_value }` to each subscriber. The subscriber's `handle(KeyChanged<K>&)` does what it wants — typically forwards to its flowchart's `step()`.

Custom subscription→message mapping is out of scope for v1.

---

## 8. Flowcharts

### 8.1 Properties

- **One flowchart per module** (v1).
- **Re-entrant.** A state function may yield (`stay()`) and resume on the next message.
- **Dynamically shaped.** Any state function may return any other state function as the next. The flow graph is not statically declared.
- **Opaque to the module.** The module knows the flow exists, can start/terminate/restart it, can react to `on_flow_done()`, but never inspects "which state is the flow in."
- **Single-flow ownership.** The flowchart cannot exist outside its module.

### 8.2 State function signature

```cpp
using StateFn = StateDirective (*)(Flow& flow, Envelope& env);

StateDirective charging_active(Flow& flow, Envelope& env) {
  auto& s = flow.locals<ChargingActiveLocals>();
  if (env.is<ChargingDone>()) return transition_to(&charging_idle);
  if (env.is<ChargeTimeout>()) return done();
  return stay();
}
```

### 8.3 Directives

| Directive                     | Effect                                                            |
|-------------------------------|-------------------------------------------------------------------|
| `stay()`                      | No transition. Wait for next dispatched message.                  |
| `transition_to(next)`         | Destruct current state-locals. Construct `next`'s locals. Wait.   |
| `transition_to_now(next)`     | Same, then immediately invoke `next` with the **same** envelope.  |
| `done()`                      | Destruct locals. Mark flow inactive. Call `on_flow_done()`.       |

`transition_to_now` is for chained state work that does not require a fresh trigger. No chain-depth limit — flow designer's responsibility to avoid infinite immediate-transitions.

### 8.4 State-local data and RAII

Each state declares a locals struct. The framework owns a fixed-size aligned buffer per flow, sized at compile time to the maximum of all the flow's locals types. On transition:

1. Call the current locals' destructor.
2. Placement-new the next locals into the same buffer.

This is how `Subscription`, `Timer`, and any other RAII member achieves "lifetime equals state duration."

State function ↔ locals type is established by a registration template (one-line declaration per state).

### 8.5 Lifecycle

```cpp
// Module side
ChargeController::ChargeController() : flow_(initial_state_fn) {}

void ChargeController::on_start() { flow_.start(); }

void ChargeController::handle(Envelope& env) {
  flow_.step(env);                 // most modules' handle() is just this
}

void ChargeController::on_flow_done(Flow& f) {
  // optional: log, restart, escalate, whatever
}
```

- **`start()`** — dispatch a synthetic init envelope (`from = ModuleId::system()`) to the initial state once. This is where initial subscriptions/timers register.
- **`step(env)`** — invoke the current state function with `env`. Apply the returned directive.
- **`terminate()`** — only callable outside a `step()`. Destructs current locals, marks flow inactive, calls `on_flow_done`. Asserts if called during a step.
- **`restart()`** — re-enter initial state with fresh locals.

### 8.6 Done semantics

When a state function returns `done()`:

1. Framework destructs current state-locals (which cancels held subscriptions/timers).
2. Flow enters inactive state.
3. Framework calls `module.on_flow_done(flow)` **synchronously** before the next dispatch.
4. Module's callback can `flow.restart()`, log, or do nothing.

There is no separate `fault(reason)` directive; modules log via the trace system or write a fault state to a cache key they own before returning `done()`.

---

## 9. Timers and clocks

### 9.1 Timer value type

```cpp
Timer timeout{ 30s, MakeMessage<ChargeTimeout>{ /*payload args*/ } };
```

- Construction: registers with `runtime.timers().arm(duration, callback)`. Synchronous, thread-safe, does not require a queue round-trip.
- Destruction: calls `runtime.timers().cancel(handle)`.
- Expiry: timer service posts the user-specified message to the owning module via the normal queue.

### 9.2 Timer service

A runtime-level facility (`runtime.timers()`), alongside the queue and the cache. Not a module. Platform-specific backend chosen at build time:

| Target     | Backend                                          |
|------------|--------------------------------------------------|
| Host/POSIX | `std::thread` + `std::priority_queue` on time   |
| FreeRTOS   | FreeRTOS software timers                         |
| Bare-metal | Hardware timer ISR + sorted list                 |

### 9.3 Clock

```cpp
Runtime rt{ SteadyClock{} };           // production
Runtime rt{ ManualClock{} };           // tests
```

`ManualClock` exposes `advance(duration)`; due timers fire by posting their messages into the queue, which `run_one()` then dispatches.

---

## 10. Lifecycle

Two-phase init, mirror-image teardown.

```
1. App rt{ Clock };
2. rt.start():
   2a. Construct every module (declaration order).
       Modules may NOT interact with each other during construction.
   2b. Call on_start() on every module (declaration order).
       Modules MAY subscribe, post messages, start their flows.
3. rt.run():
   while (!stop_) { wait_on_cv(); run_one(); }
4. rt.stop()         // signal exit
5. rt.shutdown():
   5a. Drain remaining queue (bounded by drain budget).
   5b. Call on_stop() on every module (reverse declaration order).
   5c. Destruct every module (reverse declaration order).
```

---

## 11. Composition API

The entire system shape lives in one file as a single type:

```cpp
// app/system_composition.hpp
#pragma once
#include "framework/runtime.hpp"

#include "modules/ignition_monitor.hpp"
#include "modules/charge_controller.hpp"
#include "modules/battery_manager.hpp"
#include "modules/speed_sensor.hpp"
#include "platform/can_adapter.hpp"    // typedef-swapped per target

using App = framework::Runtime<
  framework::ModuleList<
    IgnitionMonitor,
    ChargeController,
    BatteryManager,
    SpeedSensor,
    platform::CanAdapter,
    platform::TimerBackend
  >,
  framework::CacheKeyList<
    framework::Owned<VehicleSpeed,   SpeedSensor>,
    framework::Owned<BatteryLevel,   BatteryManager>,
    framework::Owned<ChargingActive, ChargeController>
  >,
  framework::Config<
    framework::MaxSubscriptions<32>,
    framework::DrainBudget<SIZE_MAX>
  >
>;
```

```cpp
// app/main.cpp
#include "system_composition.hpp"
#include "framework/clock.hpp"

int main() {
  App rt{ framework::SteadyClock{} };
  rt.start();
  rt.run();
  rt.shutdown();
  return 0;
}
```

Adding a module: one line in `ModuleList`. Adding a key: one line in `CacheKeyList`. Mis-wirings are compile errors. Investment area: `static_assert` messages with clear failure text. This is where every user starts; the error messages must be readable.

---

## 12. Tracing and logging

### 12.1 Levels

| Level      | Includes                                                            |
|------------|---------------------------------------------------------------------|
| `OFF`      | Nothing.                                                            |
| `ERROR`    | System invariant violations.                                        |
| `WARN`     | + recoverable anomalies.                                            |
| `INFO`     | + lifecycle events (start, stop, flow done).                        |
| `DISPATCH` | + every envelope dispatched (from, to, message type).               |
| `FULL`     | + cache writes (key, old, new), state transitions, timer events.    |

### 12.2 Compile-time selection

Selected via build flag (`-DFRAMEWORK_TRACE_LEVEL=DISPATCH`). The level is a `constexpr` constant; trace points use `if constexpr` so unused levels compile out entirely. Trace points are still syntax-checked at the lowest level — no preprocessor `#ifdef` walls.

### 12.3 Sinks

Pluggable. Defaults:

| Target     | Default sink                                            |
|------------|---------------------------------------------------------|
| Host       | `fprintf(stderr, ...)` with timestamps                  |
| POSIX      | Structured single-line records (one per trace event)    |
| Bare metal | `weak`-linked `framework_trace_write(const char*)` no-op; user overrides for UART/SWO/RTT/etc. |

CI builds run with `FULL` enabled so all trace points are validated regardless of the chosen production level.

---

## 13. Error and assertion policy

### 13.1 Two error classes

| Class                          | Owner       | Examples                                                                              |
|--------------------------------|-------------|---------------------------------------------------------------------------------------|
| **System invariant violation** | Framework   | Post to non-existent module; allocator overflow on bare metal; subscription pool full; double-init; double-write to one-writer key (if/when AA2 enforcement is enabled); cache access before runtime start. |
| **Application fault**          | Module      | Bad CAN frame parse; sensor disagreement; missing config; external API timeout.       |

Application faults are not the framework's concern. Modules write to their cache keys, return `done()` from a flow, escalate via a message — same machinery they use for normal logic.

### 13.2 Single assertion mechanism

```cpp
FRAMEWORK_ASSERT(cond, "reason string");
```

Expands to:

```cpp
((cond) ? (void)0 : ::framework::detail::fault(__FILE__, __LINE__, "reason string"))
```

`fault(...)` is `[[noreturn]]` and dispatches to a platform handler:

| Target     | Default fault handler                                                   |
|------------|-------------------------------------------------------------------------|
| Host       | Log + `std::abort()`                                                    |
| Bare metal | `weak`-linked `platform_fault_handler(file, line, reason)`; user overrides (typical: disable interrupts, log to UART/flash, reset). |

Boundary threads call the same primitive. The handler must be interrupt-safe (no allocations, no module interaction).

### 13.3 No recoverable framework errors in v1

Every framework-level error is a system invariant violation, which by definition cannot be recovered from. Send/post/subscribe/set APIs do not return error codes. They succeed or assert.

---

## 14. Build system

- **CMake 3.20+.**
- **C++17 minimum**, prefer C++20 features where available (concepts for cleaner template errors).
- **`-DFRAMEWORK_TARGET=host|posix|freertos|bare_metal`** selects the platform backend.
- **`-DFRAMEWORK_TRACE_LEVEL=OFF|ERROR|WARN|INFO|DISPATCH|FULL`** sets compile-time trace level.
- **`-DFRAMEWORK_BUILD_TESTS=ON|OFF`** opts into test build (off by default for cross-compiles).

Each platform backend is its own static library; the user's app links the framework lib + the selected backend.

---

## 15. Repo layout

```
cortexflow/
├── CMakeLists.txt
├── cmake/
│   ├── targets/
│   │   ├── host.cmake
│   │   ├── posix.cmake
│   │   ├── freertos.cmake
│   │   └── bare_metal.cmake
│   └── helpers.cmake
├── include/framework/
│   ├── runtime.hpp
│   ├── module.hpp
│   ├── envelope.hpp
│   ├── message.hpp
│   ├── message_ptr.hpp
│   ├── flow.hpp
│   ├── cache.hpp
│   ├── subscription.hpp
│   ├── timer.hpp
│   ├── clock.hpp
│   ├── type_name.hpp
│   ├── trace.hpp
│   ├── assert.hpp
│   ├── allocator.hpp
│   └── config.hpp
├── src/framework/
│   └── (non-template implementations: queue, trace sink dispatch, fault handler)
├── platform/
│   ├── host/
│   ├── posix/
│   ├── freertos/
│   └── bare_metal/
├── tests/
│   ├── unit/
│   └── integration/
├── examples/
│   └── minimal_app/
│       ├── modules/
│       ├── system_composition.hpp
│       └── main.cpp
├── docs/
│   ├── architecture.md          # this document
│   └── adr/                     # architecture decision records
└── README.md
```

- **Public API:** `include/framework/`. Everything outside this path is implementation.
- **`platform/`** is parallel to `src/`, not nested, because backends are independently selectable build targets.
- **`examples/minimal_app/`** exists from day one. Built in CI. Doubles as validation and reference.

---

## 16. Testing

- **doctest**, header-only, no link dependency.
- **All integration tests use `run_one()` + `ManualClock`.** No sleeps. No real I/O. Boundary modules are replaced with test doubles (by typedef-swap, same as platform backends).
- **CI runs every test with `TRACE_FULL`** so all trace points are syntax-checked.
- **Per-component unit tests:** allocator, queue, dispatch, cache (set/get/subscribe/fanout), flow (transitions/locals lifetime), timers.
- **System-level integration tests:** drive realistic scenarios through the composition. Each test is `set up → feed messages → step → assert`.

Test seam:

```cpp
TEST_CASE("charging timeout fires after 30s") {
  ManualClock clock;
  TestApp rt{ clock };
  rt.start();

  rt.post(make_envelope<ChargeController, StartCharge>());
  rt.run_one();
  REQUIRE(rt.cache().get<ChargingActive>() == true);

  clock.advance(30s);
  rt.run_one();
  REQUIRE(rt.cache().get<ChargingActive>() == false);
}
```

---

## 17. Deferred for v1 / explicit non-goals

- Multiple flowcharts per module.
- Compile-time enforcement of cache-key write ownership (`Owned<K,M>` is currently documentation; assert may be added).
- Request/response with futures and correlation IDs.
- Custom subscription→message mapping (always posts `KeyChanged<K>`).
- Per-key fanout ordering controls.
- Priority lanes in the dispatch queue.
- `on_idle()` callback hook.
- Hierarchical cache key paths or namespaces.
- Per-trace-level granularity (e.g., FULL with timer events off).
- Backwards-compat shims for evolving message schemas.
- Dynamic module instantiation (everything is statically composed).
- Cross-process message routing (the runtime is in-process by definition).

These are all add-on layers if/when real use cases demand them. None require architectural changes to the v1 core.

---

## 18. Decision log (planned ADRs)

Each major decision merits a short ADR under `docs/adr/`. Suggested initial set:

| ID  | Decision                                                          |
|-----|-------------------------------------------------------------------|
| 001 | Single-threaded run-to-completion event loop                      |
| 002 | Single FIFO message queue, no priority lanes                      |
| 003 | One-instance-per-type module addressing                           |
| 004 | Envelope = `{ to, from, MessagePtr }`; dispatch at receiver       |
| 005 | Pluggable allocator with platform-default backends                |
| 006 | Type-derived identity via `type_name<T>()`                        |
| 007 | Smart data cache with typed keys and dynamic RAII subscriptions   |
| 008 | One flowchart per module (v1)                                     |
| 009 | State functions return `StateDirective`; `transition_to_now`      |
| 010 | Per-state RAII state-locals with framework-managed lifetime       |
| 011 | Timer service as runtime-level facility                           |
| 012 | Clock injected at runtime construction                            |
| 013 | Two-phase lifecycle                                               |
| 014 | Type-level composition (`Runtime<ModuleList, CacheKeyList, ...>`) |
| 015 | Six-level trace hierarchy with `if constexpr` disable             |
| 016 | Single `FRAMEWORK_ASSERT`; framework errors are unrecoverable     |
| 017 | Boundary modules by convention, no enforced base class            |
| 018 | One declared writer per cache key, convention not compile-time    |
| 019 | Condition-variable wake (no eventfd/epoll dependency)             |

ADRs are short (one page each): context, decision, consequences, alternatives considered, rejected because. Worth writing in the first month of the repo while the rationale is still fresh.

---

## 19. Glossary

- **Envelope** — the queued unit. `{ to, from, MessagePtr }`.
- **Boundary module** — a module that owns a thread crossing into the foreign world (OS APIs, hardware, external libs).
- **Flow / flowchart** — a re-entrant state machine owned by exactly one module.
- **State-locals** — the typed data block whose lifetime is exactly one state's tenure.
- **`type_name<T>()`** — constexpr utility producing a stable, fully-qualified name for any type, used as identity for messages, cache keys, and modules.
- **Cache key** — a C++ type with a `value_type` alias, registered in the composition.
- **Subscription** — a RAII handle representing "this subscriber wants `KeyChanged<K>` for as long as I exist."
- **RTC** — run-to-completion. Each handler runs to completion or to a flowchart yield before any other handler runs.
- **HAL** — hardware/OS abstraction layer; in this framework, the typedef-swapped platform backends.
