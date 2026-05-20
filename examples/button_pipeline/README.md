# button_pipeline

A multi-stage control-plane example that walks raw hardware-style input
all the way through to a UI mode. Four modules, two cache keys, three
Flows (one of them four-state), three gesture branches — the smallest
composition that exercises every framework primitive at least once.

## What's here

```
examples/button_pipeline/
├── CMakeLists.txt              builds `button_pipeline_lib` (composition) + `button_pipeline` (binary)
├── main.cpp                    host driver: reader thread → app.post → Debouncer
├── app.hpp / app.cpp           the App wrapper around Runtime + the global wiring
├── keys.hpp                    cache keys (DebouncedButtonState, UiMode_Key)
└── modules/
    ├── button_reader.hpp       Boundary module — owns no flow; labels the input edge
    ├── debouncer.hpp/.cpp      Debouncer — owns DebouncedButtonState; 2-state lockout flow
    ├── click_classifier.hpp/.cpp ClickClassifier — gesture detection; 4-state flow
    └── ui_controller.hpp/.cpp  UiController — owns UiMode_Key; 3-state UI flow
```

The matching integration tests live at
`tests/integration/test_button_pipeline.cpp`.

## How it runs

```
                                ┌─ message ─┐    ┌─ cache write + KeyChanged fanout ─┐    ┌─ message ─┐
                                │           │    │                                   │    │           │

  stdin ──┐
          │   RawTransition{p}  ┌──────────┐    ┌──────────────────────┐            ┌──────────────────┐    ┌──────────────┐
  app.post├────────────────────▶│ Debouncer├───▶│ DebouncedButtonState │───────────▶│  ClickClassifier │───▶│ UiController │
  (foreign│   (boundary post)   │ Settled  │set │      (cache key,     │ KeyChanged │ Idle/Pressed/    │ ↳  │ Idle/Active/ │
   thread)│                     │ Cooling  │    │       owner=         │            │ Awaiting/Second  │    │ Configuring  │
          │                     │  Down    │    │       Debouncer)     │            │                  │    │              │
ButtonReader (label only)       └──────────┘    └──────────────────────┘            └──────────────────┘    └──────────────┘
                                                                                              │      ▲              ▲
                                                                                              │      │ UiMode_Key   │ Click / DoubleClick / LongPress
                                                                                              │      │ (owner=      │ (typed messages, send<UiController>)
                                                                                              │      │  UiController)
                                                                                              └──────┘
```

The flow, end-to-end:

1. The host binary's stdin reader thread maps each character to a
   `Debouncer::RawTransition` envelope and calls `app.post(...)`. The
   `post` is thread-safe (see `runtime.hpp`) so the foreign thread hands
   the envelope to the loop without any extra synchronisation.
   `ButtonReader` is registered in `ModuleList` but does nothing — it is
   the *Boundary module* label, marking the edge across which external
   events cross into the runtime queue.
2. **`Debouncer`** runs a two-state lockout flow. In `Settled`, the
   first `RawTransition` whose `pressed` disagrees with what
   `DebouncedButtonState` holds is committed to the cache and the flow
   transitions to `CoolingDown`. `CoolingDown`'s `Locals` arms a Timer
   for `kDebounceWindow`; further `RawTransition` envelopes inside the
   window are ignored. When the timer fires (`DebounceExpired`), the
   flow returns to `Settled` — the Locals destructor releases the Timer
   on the way out.
3. The cache write fans out a `KeyChanged<DebouncedButtonState>`
   envelope to every subscriber. **`ClickClassifier`** holds one
   subscription, kept alive by whichever state's `Locals` is current
   (`Idle`, `Pressed`, `AwaitingSecondClick`, `SecondPressed`). The
   subscription is reconstructed on every transition — the outgoing
   `Locals` destructor releases the slot synchronously and the incoming
   `Locals` constructor takes a fresh one, so `subscriber_count()`
   stays at exactly 1 across the lifetime of the flow.
4. On a clean press + release, the Classifier walks
   `Idle → Pressed → AwaitingSecondClick`, arms a `DoubleClickExpired`
   timer in `AwaitingSecondClick`, and when the timer fires the
   module-level `handle` posts `UiController::Click{}` to UiController
   on its way through `flow.step`. A second press within the window
   takes the `AwaitingSecondClick → SecondPressed → Idle` branch,
   posting `UiController::DoubleClick{}` on the trailing release. A
   press held past `kLongPressThreshold` takes the
   `Pressed → Idle` branch, posting `UiController::LongPress{}` on
   the way through.
5. **`UiController`** receives one of `Click`, `DoubleClick`, or
   `LongPress` and walks its three-state flow. Each state's `Locals`
   constructor writes the corresponding `UiMode` to `UiMode_Key` —
   the "side-effect on entry" pattern from the PRD. `DoubleClick` is
   a no-op in every state (the PRD reserves it for a future
   side-effect).

## Messages vs cache: what travels where

The example is built around a single rule of thumb: **messages carry
events; the cache carries state**.

- A message is an `Envelope` posted to a specific module. It is
  transient — the envelope is allocated, dispatched once, and destroyed.
  Use messages when *something happened* and a downstream module must
  react to that occurrence.
- A cache value is a typed key-value entry that lives in the runtime's
  cache. It is durable — every subsequent reader sees the same value
  until the owner writes a new one. Use the cache when *something is*,
  regardless of whether anyone is currently looking.

The **Debouncer** is the canonical illustration of the split:

| What                         | Channel                    | Why                                             |
|------------------------------|----------------------------|-------------------------------------------------|
| Raw transition from hardware | message (`RawTransition`)  | An edge is an *event*; a missed edge is wrong.  |
| Debounced level              | cache (`DebouncedButtonState`) | "The button is pressed" is a *state*; latecomers must read the current value, not the last delta. |

`ClickClassifier` then consumes the cache key (via `KeyChanged<>`
fanout — itself a message, generated by the cache on `set`) and emits
gestures back into the message channel (`Click`, `LongPress`,
`DoubleClick`) — these are events too, addressed at a specific
receiver, and the receiver itself owns the resulting *state*
(`UiMode_Key`) on the cache.

## Driving the example

Two paths drive the same composition, and both reach the modules
through the public `app.post(...)` surface — there is no internal
back-door.

**From the host binary (`main.cpp`)** — a reader thread reads
characters from `std::cin`, maps them to `RawTransition` envelopes,
and posts them. Each character is one transition:

```
d         → press                       (RawTransition{pressed=true})
' ' / u   → release                     (RawTransition{pressed=false})
^D / EOF  → reader calls app.stop()
Ctrl-C    → SIGINT handler calls app.stop()
```

The reader thread runs alongside the main thread's `app.run()` and is
joined after `run()` returns. SIGINT is blocked on the main thread and
unblocked on the reader so the kernel delivers the signal there —
without that the read syscall would not unblock and `reader.join()`
would deadlock. See the header comment in `main.cpp` for the full
contract.

> **Note on the runnable binary.** Under the default `SteadyClock` v1
> has no real-time backend wired in to fire timers, so the Debouncer's
> lockout timer and the ClickClassifier's long-press / double-click
> timers do not fire. The binary therefore demonstrates the *receive
> path* fully — envelopes arrive, the cache flips, the Classifier
> transitions on the resulting fanout — but timer-fired gesture
> completion (the `Click` / `LongPress` / `DoubleClick` posts to
> `UiController`) is only observable under a future real-time backend
> or in tests under `ManualClock`. Mirrors the analogous note in
> `minimal_app`.

**From a test** — the integration tests under
`tests/integration/test_button_pipeline.cpp` link
`button_pipeline_lib` directly and drive the same App through
`app.post(...)` + `app.run_one()` + `ManualClock::advance(...)`. The
exact pattern:

```cpp
cortexflow::ManualClock clk;
button_pipeline::App app{clk};
app.start();

post_raw_transition(app, true);       // press — drains the leading edge
app.run_one();
clk.advance(button_pipeline::kDebounceWindow);
app.run_one();                        // DebounceExpired → Settled

post_raw_transition(app, false);      // release — same shape
app.run_one();
clk.advance(button_pipeline::kDebounceWindow);
app.run_one();

clk.advance(button_pipeline::kDoubleClickWindow);
app.run_one();                        // DoubleClickExpired → UiController::Click → UiMode::Active

app.shutdown();
```

`ManualClock::advance` is the only way v1 fires timers deterministically,
which is why every timer-driven branch lives in the test suite rather
than the binary.

## Where to look in the framework

| You want to understand…                                      | Read…                                                                         |
|--------------------------------------------------------------|-------------------------------------------------------------------------------|
| how a foreign thread posts into the runtime                  | `include/cortexflow/runtime.hpp` — `Runtime::post`                            |
| how `Flow::step` routes an envelope to the current state     | `include/cortexflow/flow.hpp` — `Flow::step`                                  |
| how a `Subscription` ties a fanout slot to a state's `Locals`| `include/cortexflow/subscription.hpp`, `include/cortexflow/cache.hpp` — `Cache::subscribe` |
| how `Timer::arm` and `ManualClock::advance` interact         | `include/cortexflow/timer.hpp` — `TimerService::arm`, `include/cortexflow/clock.hpp` |
| how `send<Target>(msg)` becomes an envelope                  | `include/cortexflow/module.hpp` — `Module::send`                              |
| the architectural ground truth                               | `docs/architecture.md`                                                        |

## House rules

- **No `#ifdef` walls in modules.** The `no_target_ifdefs` test under
  `tests/CMakeLists.txt` greps this directory and fails CI if anyone
  introduces `CORTEXFLOW_TARGET_*` macros into the example. Platform
  behavior is selected by the typedef-swap layer (see
  `docs/agents/platform-backends.md`), not by preprocessor branches.
  `main.cpp` reaches for POSIX `sigaction` / `pthread_sigmask` directly
  because it is the *host driver*, not module code — the modules
  themselves stay platform-free.
- **Forward-declared state tags must be complete before they enter a
  `Flow<…, StateList<…>>` member.** A forward declaration silently
  sizes the flow's locals buffer to the empty-fallback type. Define
  state structs before the owning module — see `debouncer.hpp` and
  `click_classifier.hpp` for the pattern.
