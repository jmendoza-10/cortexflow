# minimal_app

A self-contained cortexflow composition that doubles as the integration
fixture for end-to-end tests. Two modules, one cache key, one flow with two
states, one timer — everything you need to see a runtime end-to-end and
nothing you don't.

## What's here

```
examples/minimal_app/
├── CMakeLists.txt        builds `minimal_app_lib` (composition) + `minimal_app` (binary)
├── main.cpp              the canonical three-line entry point
├── app.hpp / app.cpp     the App wrapper around Runtime, plus the global wiring
├── keys.hpp              cache keys (just `Counter`)
└── modules/
    ├── producer.hpp/.cpp Producer — owner of Counter; defines its own `Bump`/`Done`
    └── consumer.hpp/.cpp Consumer — runs the Idle/Processing flow; defines its own `ProcessingTick`
```

The matching integration tests live at `tests/integration/test_minimal_app.cpp`.

## How it runs

```
                     ┌──────────────┐                    ┌──────────────┐
                     │   Producer   │                    │   Consumer   │
                     │              │                    │              │
   Bump (self-post)  │  on(Bump):   │   KeyChanged       │ Idle ───────▶│ Processing
   ─────────────────▶│   counter++  │     <Counter>      │   sub(K)     │   timer(ProcessingTick)
                     │   cache.set  │───────────────────▶│              │
                     │              │                    │              │
                     │  on(Done):   │                    │   ProcessingTick:
                     │   ack++      │     Done           │   send Done ──┐
                     │   send Bump ─┼───── ◀─────────────│   to Producer │
                     └──────────────┘                    └──────────────┘
```

1. **`Producer.on_start`** posts `Bump` to itself to seed the loop.
2. **`Producer.on(Bump)`** increments `counter_` and writes it to the cache:
   `cache().set<Counter>(counter_)`. The cache fans out a
   `KeyChanged<Counter>` envelope to every subscriber of `Counter`.
3. **`Consumer.handle`** routes every envelope through `flow.step`. While the
   flow is in **`Idle`**, the locals hold a `Subscription` to `Counter` —
   the cache fanout therefore reaches Idle's handler. Idle returns
   `transition_to<Processing>()`.
4. The transition destructs Idle's locals (releasing the subscription) and
   constructs **`Processing`**'s locals — which arm a `Timer` for
   `ProcessingTick`, addressed to Consumer.
5. When the timer fires (driven by `ManualClock::advance` in tests, by a
   real-time backend in production), `ProcessingTick` arrives in
   `Consumer.handle`. The handler intercepts it to
   `send<Producer>(Producer::Done{})`, then forwards into `flow.step`.
   Processing returns
   `transition_to<Idle>()` — the cycle resets.
6. **`Producer.on(Done)`** increments `acks_` and posts another `Bump`,
   keeping `app.run()` busy.

## The two-line composition

In `app.hpp`:

```cpp
using Modules    = cortexflow::ModuleList<Producer, Consumer>;
using Keys       = cortexflow::CacheKeyList<cortexflow::Owned<Counter, Producer>>;
using AppConfig  = cortexflow::Config<cortexflow::MaxSubscriptions<4>>;
using Runtime    = cortexflow::Runtime<Modules, Keys, AppConfig>;
```

That single declaration is what every `static_assert` in the runtime checks
against. Adding a duplicate module here, or using `send<X>` to a module not
in `Modules`, fails at compile time — the validators in `runtime.hpp` and
`module.hpp` print the offending type in the diagnostic.

## The three-line entry point

`main.cpp`:

```cpp
int main() {
    minimal_app::App app;
    app.start();
    app.run();
    app.shutdown();
    return 0;
}
```

This is the canonical lifecycle (PRD US 3). `start()` constructs modules and
runs `on_start()` in declaration order; `run()` blocks the calling thread
draining the message queue until `stop()` is called from another thread;
`shutdown()` drains a bounded number of remaining envelopes and tears
modules down in reverse order.

> **Note on the runnable binary.** Under the default `SteadyClock`, v1 has
> no real-time backend wired in to fire timers, so the binary blocks once
> the Consumer transitions into `Processing`. The composition is correct;
> the demonstration of progress lives in the integration tests, which use
> `ManualClock::advance` to fire the timer deterministically. A future
> real-time backend (FreeRTOS / bare-metal) replaces the clock and the same
> binary becomes self-driving. See `docs/architecture.md` §9 for the
> advance-window semantics.

## Where to look in the framework

| You want to understand…                                | Read…                                                                |
|--------------------------------------------------------|----------------------------------------------------------------------|
| how modules and the runtime fit together               | `include/cortexflow/runtime.hpp`                                     |
| how `send<Target>(msg)` becomes an envelope            | `include/cortexflow/module.hpp` — `Module::send`                     |
| how cache writes fan out                               | `include/cortexflow/cache.hpp` — `Cache::set` / `fanout`             |
| how `Subscription` / `Timer` self-release on destruct  | `include/cortexflow/subscription.hpp`, `include/cortexflow/timer.hpp`|
| how `Idle` ↔ `Processing` state-locals are constructed | `include/cortexflow/flow.hpp` — `Flow::step`                         |
| how `ManualClock::advance` fires due timers            | `include/cortexflow/clock.hpp`, `include/cortexflow/timer.hpp`       |
| the architectural ground truth                         | `docs/architecture.md`                                               |

## Driving the example from a test

The integration tests link `minimal_app_lib` directly so they exercise the
exact same module objects and composition the binary uses. The pattern is:

```cpp
cortexflow::ManualClock clk;
minimal_app::App app{clk};
app.start();
app.run_one();                               // drain start-time Bump → first transition
clk.advance(minimal_app::kProcessingDelay);  // fire the Processing timer
app.run_one();                               // drain the resulting cycle
app.shutdown();
```

`app.queue_size()`, `app.cache_ref().subscriber_count()`, and
`app.timers_ref().armed_count()` are the introspection knobs the tests use
to assert pool occupancy after each step. See
`tests/integration/test_minimal_app.cpp` for the full set of scenarios.

## House rules

- **No `#ifdef` walls in modules.** The `no_target_ifdefs` test under
  `tests/CMakeLists.txt` greps this directory and fails CI if anyone adds
  `CORTEXFLOW_TARGET_*` macros into the example. Platform behavior is
  selected by the typedef-swap layer (see `docs/agents/platform-backends.md`),
  not by preprocessor branches in module code.
- **Forward-declared state tags must be complete before they enter a
  `Flow<…, StateList<…>>` member.** A forward declaration silently sizes
  the flow's locals buffer to the empty-fallback type. Define your state
  structs before the owning module — see `consumer.hpp` for the pattern.
