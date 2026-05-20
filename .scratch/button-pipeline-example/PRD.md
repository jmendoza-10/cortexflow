# button_pipeline: a multi-module hardware-adjacent example

Status: ready-for-agent

## Problem Statement

A framework user reading the CortexFlow repository today has exactly one composed example, `examples/minimal_app/`. It demonstrates the smallest possible **Runtime**: two **Modules** (`Producer`, `Consumer`), one **Cache key** (`Counter`), one **Flow** with two **States** (`Idle`, `Processing`), one **Timer**. That is enough to see the primitives working end-to-end, and the PRD intentionally framed it as the floor of expressiveness.

What it does *not* show is what CortexFlow actually exists for: a control-plane composition where messages flow through a *pipeline* of modules that each own a state machine, where multiple cache keys describe the system's level state, and where a **Boundary module** bridges external events into the runtime queue. A reader who learns from `minimal_app` alone has no exemplar for the patterns they will actually reach for in a real application — multi-stage processing, multiple Flows running concurrently with each other, mixed message/cache communication channels, and a `main` that drives real input across threads.

The gap shows up in onboarding ("how do I structure a real app?"), in documentation searches that find no precedent, and in the **Receiver-owned message** convention (ADR-0020) being shown only against the trivial sender-receiver pair already in `minimal_app`. The framework's audience is embedded control-plane developers; their mental model is a chain of modules that *together* turn raw input into a system-level decision. The repo does not show that.

## Solution

Ship a second example, `examples/button_pipeline/`, that demonstrates a realistic control-plane shape against a familiar embedded scenario: a chain of four modules that turns raw button input into a UI mode change.

```
   (external input)                              (cache fan-out)
         │                                              ▼
         ▼                                       ┌──────────────┐
  ┌──────────────┐  RawTransition   ┌──────────────┐  KeyChanged ┌──────────────┐  Click/Double  ┌──────────────┐
  │ ButtonReader │ ───────────────▶ │  Debouncer   │ ──────────▶│  Classifier  │ ──────────────▶│ UiController │
  │  (boundary)  │                  │   2 states   │            │   4 states   │   /LongPress    │   3 states   │
  └──────────────┘                  └──────────────┘            └──────────────┘                 └──────────────┘
                                          │                                                            │
                                  writes DebouncedButtonState                                   writes UiMode
                                          (cache)                                                    (cache)
```

`minimal_app` is unchanged. The new example sits beside it. The README points a first-time reader at `minimal_app` for the smallest end-to-end picture and at `button_pipeline` once they want to see what a real composition looks like.

`ButtonReader` is a **Boundary module** in the CONTEXT.md sense: it bridges external events (stdin keypresses in the host binary; posted envelopes in integration tests) into the runtime queue. It owns no Flow. `Debouncer`, `ClickClassifier`, and `UiController` each own a Flow and demonstrate distinct Locals patterns: RAII Timer, RAII Subscription + Timer, and side-effect-on-entry. The two cache keys (`DebouncedButtonState`, `UiMode`) have distinct writers and demonstrate the writer/subscriber/inspection roles defined in `CONTEXT.md`.

The host binary is *self-running* via a stdin reader thread that turns key presses into `Debouncer::RawTransition` envelopes posted from a foreign thread through `Runtime::post(...)`. The integration suite uses `ManualClock` and posts envelopes deterministically — the same pattern `tests/integration/test_minimal_app.cpp` already establishes.

## User Stories

1. As a framework user new to CortexFlow, I want a second composed example beyond `minimal_app`, so I can see what a multi-stage composition looks like before writing my own.
2. As a framework user, I want the new example to live at `examples/button_pipeline/`, mirroring `examples/minimal_app/`'s structure, so the two examples sit side-by-side in the repository.
3. As a framework user, I want `minimal_app` to remain unchanged, so the "smallest possible end-to-end" entry point is preserved.
4. As a framework user reading the README, I want a "which example?" cue that points first-time readers at `minimal_app` and intermediate readers at `button_pipeline`, so I can pick the right entry point for my level.
5. As a framework user reading `button_pipeline/README.md`, I want a top-of-file diagram showing the four-module pipeline, what travels between modules (messages vs cache fan-outs), and where the two cache keys are written, so the composition is legible without reading every header.
6. As a framework user, I want `ButtonReader` to be labeled a **Boundary module** in source comments, so the convention from `CONTEXT.md` is demonstrated in a concrete example.
7. As a framework user, I want `ButtonReader` to expose its input surface as externally-postable envelopes of type `Debouncer::RawTransition`, posted via `app.post(...)`, so the boundary is a thin wrapper over `Runtime::post` rather than a bespoke API.
8. As a framework user, I want `ButtonReader` to own no Flow, so the example demonstrates that boundary modules are a label, not an enforced base class.
9. As a framework user, I want raw button transitions to travel as messages (not cache writes), so that a glitch sequence preserves every edge instead of collapsing into a single "most-recent value" write.
10. As a framework user, I want the `RawTransition` message defined as a `public` nested type inside `Debouncer` (the receiver), so the example demonstrates ADR-0020's receiver-owned convention across a real producer/consumer pair.
11. As a framework user, I want `Debouncer` to own a `DebouncedButtonState` cache key whose `value_type` is `bool`, so the example demonstrates a non-`int` cache key with the `Owned<K, M>` marker pointing at the writer.
12. As a framework user, I want `Debouncer`'s Flow to have two states, `Settled` and `CoolingDown`, and to use the *lockout* debouncer pattern (commit immediately on the first edge, ignore further transitions for the lockout window), so the example demonstrates a Flow whose state-locals own a Timer.
13. As a framework user, I want `Debouncer`'s code comment to acknowledge that an alternative "wait-for-silence" debouncer also exists and to explain why the lockout pattern was chosen for this example (it maps cleanly to Locals' construct-on-entry / destruct-on-transition lifetime), so a reader does not assume CortexFlow can only express the chosen pattern.
14. As a framework user, I want `Debouncer.CoolingDown.Locals` to hold a `cortexflow::Timer` armed for the lockout window, so the timer is cancelled automatically on transition out — the same RAII pattern `minimal_app`'s `Processing.Locals` shows.
15. As a framework user, I want `Debouncer` to write to its `DebouncedButtonState` cache key only on the leading edge of an accepted transition, so subscribers see exactly the committed values (no glitches, no redundant writes).
16. As a framework user, I want `ClickClassifier` to subscribe to `DebouncedButtonState` via a state-local Subscription held inside *every* state's Locals (not just one), so the example demonstrates that a Subscription can be present across multiple states in the same Flow.
17. As a framework user, I want `ClickClassifier`'s Flow to have four states — `Idle`, `Pressed`, `AwaitingSecondClick`, `SecondPressed` — so the example demonstrates a non-trivial gesture-classification state machine.
18. As a framework user, I want `ClickClassifier` to emit `UiController::Click {}` after a single short press once the double-click window expires without a second press, so the example demonstrates a timer-fire-as-decision pattern.
19. As a framework user, I want `ClickClassifier` to emit `UiController::DoubleClick {}` when a second press-release sequence completes within the double-click window, so the example demonstrates one gesture being preferred over another based on timing.
20. As a framework user, I want `ClickClassifier` to emit `UiController::LongPress {}` when the press is held longer than the long-press threshold, so the example demonstrates a timer that fires while the button is still held.
21. As a framework user, I want `ClickClassifier.Pressed.Locals` to hold both a Subscription and a long-press Timer, so the example demonstrates a Locals struct with multiple RAII resources.
22. As a framework user, I want `ClickClassifier.AwaitingSecondClick.Locals` to hold both a Subscription and a double-click Timer, so the example demonstrates that different states in the same Flow can hold different Locals shapes.
23. As a framework user, I want gestures (`Click`, `DoubleClick`, `LongPress`) to be defined as `public` nested types inside `UiController`, so the example follows ADR-0020 across the second cross-module hop in the pipeline.
24. As a framework user, I want `UiController`'s Flow to have three states — `Idle`, `Active`, `Configuring` — so the example demonstrates an app-layer state machine on top of the gesture-recognition pipeline.
25. As a framework user, I want each `UiController` state's `Locals` constructor to write the corresponding `UiMode` value to the cache, so the example demonstrates the "side-effect on transition" Locals pattern that `minimal_app` does not.
26. As a framework user, I want `UiMode` to be a cache key whose `value_type` is an `enum class` (`Idle`, `Active`, `Configuring`), so the example demonstrates an enum-typed cache key.
27. As a framework user, I want `UiMode` to be `Owned<UiMode, UiController>`, so the example demonstrates that the writer of a cache key is documented in the `CacheKeyList`.
28. As a framework user, I want `UiMode` to have no in-tree subscribers in v1, so the example demonstrates that a cache key's value is also useful as an *inspection point* — tests sample it via `app.cache_ref().get<UiMode>()` to assert mode transitions, rather than poking module internals.
29. As a framework user, I want the four modules composed in a single `ModuleList<ButtonReader, Debouncer, ClickClassifier, UiController>` and a single `CacheKeyList<Owned<DebouncedButtonState, Debouncer>, Owned<UiMode, UiController>>`, so the example demonstrates the canonical composition shape with multiple modules and multiple cache keys.
30. As a framework user, I want the example's `Config` to declare `MaxSubscriptions<8>`, so the pool is comfortably sized for one classifier Subscription per state plus headroom.
31. As a framework user, I want the three timing constants (`kDebounceWindow`, `kLongPressThreshold`, `kDoubleClickWindow`) declared as `inline constexpr std::chrono::milliseconds` at namespace scope in the example's `app.hpp`, so integration tests can import the symbols and call `ManualClock::advance` by the exact same durations — mirroring `minimal_app::kProcessingDelay`.
32. As a framework user, I want sensible default values for the three timing constants (debounce ≈ 5ms, long-press ≈ 500ms, double-click window ≈ 300ms), so the durations feel "real" without being calibrated to any particular hardware.
33. As a framework user, I want the host binary at `examples/button_pipeline/main.cpp` to be self-running — spawning a stdin-reader thread that translates keypresses into `Debouncer::RawTransition` envelopes posted via `app.post(...)` while `app.run()` blocks the main thread — so the binary is genuinely interactive on host (unlike `minimal_app`, which blocks).
34. As a framework user, I want the stdin reader to terminate cleanly on EOF (Ctrl-D) by calling `app.stop()`, so closing stdin shuts the binary down gracefully.
35. As a framework user, I want the stdin reader thread to be joined before `app.shutdown()` so there is no use-after-free on the runtime, demonstrating the lifecycle discipline a real boundary thread needs.
36. As a framework user, I want the cross-thread post in `main.cpp` to go through `Runtime::post(...)`, relying on `runtime.hpp`'s documented "callable from any thread (including foreign boundary-module threads)" contract, so the example demonstrates correct multi-thread usage of the runtime.
37. As a framework user, I want a `button_pipeline_lib` STATIC library plus a `button_pipeline` binary in the example's `CMakeLists.txt`, mirroring `minimal_app`'s split, so the integration test can link the same composition the binary uses.
38. As a framework user, I want the root `CMakeLists.txt` to `add_subdirectory(examples/button_pipeline)` alongside `minimal_app` under the existing `CORTEXFLOW_BUILD_EXAMPLES` guard, so both examples participate in the same opt-in build flag.
39. As a framework user, I want an integration test fixture at `tests/integration/test_button_pipeline.cpp`, registered with CTest, so the example is exercised end-to-end by the same CI matrix as `minimal_app`.
40. As a framework user, I want the integration test to use `ManualClock` and `app.run_one()` exclusively, with no real time and no real stdin, so the assertions are deterministic.
41. As a framework user, I want the integration test to drive `Debouncer::RawTransition` envelopes via `app.post(cortexflow::Envelope{...})` to model what the stdin thread would do, so the test exercises the same boundary as the binary.
42. As a framework user, I want a scenario verifying a clean press-and-hold causes `DebouncedButtonState` to flip from `unset/false` to `true` after the lockout window elapses, so the Debouncer's happy-path commit is pinned.
43. As a framework user, I want a scenario verifying a high-frequency glitch train (multiple RawTransitions within the lockout window) results in exactly one `DebouncedButtonState` write, so the Debouncer's lockout behaviour is pinned.
44. As a framework user, I want a scenario verifying a press-release sequence followed by clock advance past the double-click window causes `UiMode` to transition to `Active`, so the end-to-end single-click path is pinned.
45. As a framework user, I want a scenario verifying a double press-release-press-release sequence within the double-click window causes `UiMode` to follow the double-click transition path, so the Classifier's two-click branch is pinned.
46. As a framework user, I want a scenario verifying a press held longer than the long-press threshold causes `UiMode` to transition to `Configuring`, so the LongPress timer-fire path is pinned.
47. As a framework user, I want a scenario verifying that across a full gesture sequence the cache subscription pool count and timer-armed count both return to a known baseline between gestures, so the RAII lifetime invariants of Subscription and Timer in state-locals are pinned (mirroring `minimal_app`'s pool-occupancy assertion).
48. As a framework user, I want the integration test to include a compile-time `static_assert`-style sanity check (analogous to `minimal_app`'s "composition shape" test) asserting `Runtime::kNumModules == 4` and `Keys::size == 2`, so silent composition drift fails at compile time.
49. As a framework user, I want `mermaid` Flow diagrams generated by `scripts/gen-diagrams.py` for the new example's modules to be committed under `docs/diagrams/`, so the CI drift guard stays green and the rendered diagrams are part of the documentation.
50. As a framework user reading the repo's `README.md`, I want the "Run everything CI runs" snippet extended to invoke `gen-diagrams.py` against the new example's `app.hpp` in addition to `minimal_app`'s, so the drift guard covers both examples.
51. As a framework contributor, I want the `no_target_ifdefs` test in `tests/CMakeLists.txt` extended to scan `examples/button_pipeline/` for `CORTEXFLOW_TARGET_*` macros, so the typedef-swap discipline is enforced in the new example just as it is in `minimal_app`.
52. As a framework contributor, I want every new `.hpp` and `.cpp` file under the new example to carry the two-line SPDX header, so the existing `spdx_headers` guard passes.
53. As a framework user building under `-DCORTEXFLOW_TARGET=posix`, I want the example to build and the integration test to pass under that target, so the cross-target equivalence guarantee (per `tests/integration/test_platform.cpp`'s precedent) extends to the new example.
54. As a framework contributor reading the new example's source, I want every cross-module `send<X>(...)` call to live in a `.cpp` translation unit per ADR-0020, with the receiver's header included from the `.cpp` only, so the include graph remains acyclic.
55. As a framework user, I want the example's namespace to be `button_pipeline`, matching the directory name, so the symbol prefix is predictable.
56. As a framework user, I want the example's CMake target name to be `button_pipeline` (binary) and `button_pipeline_lib` (library), mirroring the `minimal_app` / `minimal_app_lib` pattern, so the build surface is consistent across examples.
57. As a framework user, I want the per-app `g_runtime` static-pointer pattern used by `minimal_app/app.cpp` reproduced in `button_pipeline/app.cpp`, so state-local constructors (Subscriptions, Timers) and module handlers can reach the cache and timer service the same way.
58. As a framework user, I want `button_pipeline`'s `App` class to support both the default-clock and the `Clock&`-injection constructors that `minimal_app::App` supports, so tests can pass a `ManualClock` while the binary uses the default `SteadyClock`.
59. As a framework user, I want a paragraph in `button_pipeline/README.md` explicitly contrasting messages vs cache as communication channels, citing the Debouncer (raw=message, debounced=cache) as the canonical illustration, so the channel-choice guidance lives next to the code that demonstrates it.
60. As an AFK agent picking up implementation, I want acceptance criteria stated as concrete file-level outcomes (file created, types nested, includes ordered, tests passing) per issue, so completion is verifiable mechanically.

## Implementation Decisions

**Coexistence with `minimal_app`.** New example sits beside, not replaces. `minimal_app` is unchanged. Root `CMakeLists.txt` gains one `add_subdirectory(examples/button_pipeline)` line under the existing `CORTEXFLOW_BUILD_EXAMPLES` guard.

**Pipeline shape.** Four modules in this order: `ButtonReader` (boundary) → `Debouncer` → `ClickClassifier` → `UiController`. No fifth output-side boundary module (e.g. an LED driver) in v1; `UiMode` cache key has no in-tree subscriber and is intended as the inspection surface.

**Composition declaration (the canonical front-door typedef block).**

```cpp
using Modules = cortexflow::ModuleList<
    ButtonReader, Debouncer, ClickClassifier, UiController>;

using Keys = cortexflow::CacheKeyList<
    cortexflow::Owned<DebouncedButtonState, Debouncer>,
    cortexflow::Owned<UiMode,               UiController>>;

using AppConfig = cortexflow::Config<cortexflow::MaxSubscriptions<8>>;
using Runtime   = cortexflow::Runtime<Modules, Keys, AppConfig>;
```

**Receiver-owned messages (ADR-0020).** Every cross-module message is a `public` nested type in the receiver:

- `Debouncer::RawTransition { bool pressed; }` — sent by `ButtonReader`.
- `UiController::Click {}`, `UiController::DoubleClick {}`, `UiController::LongPress {}` — sent by `ClickClassifier`.
- Framework-emitted `KeyChanged<DebouncedButtonState>` — owned by `cache.hpp`, not subject to the convention.

Cross-module `send<X>(...)` calls live in `.cpp` files only; the receiver's header is included from the `.cpp`, never from another module's header.

**Cache keys.**

| Key | `value_type` | Owner (writer) | Subscriber(s) |
|---|---|---|---|
| `DebouncedButtonState` | `bool` | `Debouncer` (writes on edge commit) | `ClickClassifier` (state-local Subscription in every state) |
| `UiMode` | `enum class UiMode { Idle, Active, Configuring }` | `UiController` (each state's `Locals` writes its corresponding value on entry) | None in tree — tests inspect via `app.cache_ref().get<UiMode>()` |

**Flow shapes.**

`Debouncer` (2 states, lockout pattern):

- `Settled` — no Locals. On `RawTransition` whose `pressed` differs from the cache's `DebouncedButtonState`: write the new value to the cache, transition to `CoolingDown`.
- `CoolingDown` — Locals hold a `Timer` armed for `kDebounceWindow`. Further `RawTransition` envelopes are ignored. On timer fire: transition back to `Settled`.

`ClickClassifier` (4 states):

- `Idle` — Locals: Subscription to `DebouncedButtonState`. On `KeyChanged<>(true)`: transition to `Pressed`.
- `Pressed` — Locals: Subscription + long-press `Timer` armed for `kLongPressThreshold`. On `KeyChanged<>(false)`: transition to `AwaitingSecondClick`. On timer fire: `send<UiController>(UiController::LongPress{})`, transition back to `Idle`.
- `AwaitingSecondClick` — Locals: Subscription + double-click `Timer` armed for `kDoubleClickWindow`. On `KeyChanged<>(true)`: transition to `SecondPressed`. On timer fire: `send<UiController>(UiController::Click{})`, transition to `Idle`.
- `SecondPressed` — Locals: Subscription. On `KeyChanged<>(false)`: `send<UiController>(UiController::DoubleClick{})`, transition to `Idle`.

`UiController` (3 states, side-effect-on-entry Locals):

- `Idle` — Locals constructor: `cache().set<UiMode>(UiMode::Idle)`. On `Click`: transition to `Active`. On `LongPress`: transition to `Configuring`. On `DoubleClick`: stay.
- `Active` — Locals constructor: `cache().set<UiMode>(UiMode::Active)`. On `Click`: transition to `Idle`. On `LongPress`: transition to `Configuring`. On `DoubleClick`: stay.
- `Configuring` — Locals constructor: `cache().set<UiMode>(UiMode::Configuring)`. On `LongPress`: transition to `Idle`. On `Click` / `DoubleClick`: stay.

The `UiController` Flow's `Inbox` is empty (`std::tuple<>`); the module's `handle()` forwards every envelope into `flow.step` and the state handlers dispatch on payload type-id directly — the same pattern `minimal_app::Consumer` uses, justified the same way (state handlers need to mutate state-machine state, not module-private state).

**`ButtonReader`'s contract.** No Flow. No `Inbox` (or an empty one) — it does not receive messages, it only *emits* them. The boundary surface is `Runtime::post(...)` from a foreign thread: external code (the stdin reader in `main.cpp`, or the integration test) constructs an `Envelope{kNoSender, type_id<Debouncer>(), make_message<Debouncer::RawTransition>(...)}` and posts it. `ButtonReader` exists as a Module instance so the example demonstrates that boundary modules *are* still modules (registered in `ModuleList`, addressable from `send<>`), even when they own no behaviour.

**Timing constants.** Three `inline constexpr std::chrono::milliseconds` in `examples/button_pipeline/app.hpp`: `kDebounceWindow{5}`, `kLongPressThreshold{500}`, `kDoubleClickWindow{300}`. The integration test imports them and advances `ManualClock` by these exact values.

**Host driver (the binary).** `main.cpp` spawns one extra thread that reads stdin in a loop and posts `Debouncer::RawTransition` envelopes. The main thread runs `app.run()`. EOF on stdin triggers `app.stop()`, which unblocks `run()`; the reader thread is then joined and `app.shutdown()` runs. A simple character mapping (e.g. `'d'` = press, `' '` = release, or "any char" = toggle) keeps the demo trivial to drive.

**Test driver (the integration suite).** Tests use `ManualClock` and post envelopes via `app.post(...)`, never spawn a stdin thread. Pattern mirrors `tests/integration/test_minimal_app.cpp`.

**Per-app `g_runtime` static pointer.** `examples/button_pipeline/app.cpp` reproduces the static `Runtime*` pointer pattern used by `minimal_app/app.cpp`, with the same assertion that two `App` instances cannot coexist. The helpers `button_pipeline::cache()` and `button_pipeline::timers()` resolve through it, enabling state-local Locals constructors to call `cache().subscribe<...>()` and `timers().arm<...>(...)`.

**Namespace and target names.** Namespace `button_pipeline`. CMake targets `button_pipeline_lib` (STATIC) and `button_pipeline` (executable). All file names are snake_case, matching repo convention.

**SPDX, no #ifdef walls.** Every new file carries the two-line SPDX header. No `CORTEXFLOW_TARGET_*` macros anywhere under `examples/button_pipeline/`. The `tests/CMakeLists.txt` `no_target_ifdefs` test's grep list is extended to scan the new directory.

**Diagram generation.** `scripts/gen-diagrams.py` is invoked against `examples/button_pipeline/app.hpp` in addition to `minimal_app`'s. The generated diagrams under `docs/diagrams/` are committed.

**README pointer.** Top-level `README.md` gains a "which example?" cue. The "Run everything CI runs" snippet's `gen-diagrams.py` invocation grows to cover the new example.

**Channel-choice paragraph in the example's README.** A short section in `examples/button_pipeline/README.md` explains the rule of thumb the example illustrates: *messages for events, cache for levels/state*. The Debouncer's choice (raw transitions = message, debounced state = cache) is cited as the canonical example. This guidance lives next to the code that demonstrates it rather than in a framework-level ADR — the trade-off does not meet the bar for a hard-to-reverse architectural decision (see Out of Scope).

## Testing Decisions

**Test surface.** The integration test drives the example through the same public surface `minimal_app`'s integration test uses: `app.post(...)`, `app.run_one()`, `clk.advance(...)`, plus the introspection knobs `app.queue_size()`, `app.cache_ref().get<K>()`, `app.cache_ref().subscriber_count()`, `app.timers_ref().armed_count()`. No internal pokes, no friend access. **What makes a good test here is that it asserts only observable behavior** — what envelopes the runtime processed, what the cache holds at each step, how many subscriptions / timers are alive — and never reads module-private state.

**Tests live at** `tests/integration/test_button_pipeline.cpp`, registered with CTest as test name `button_pipeline`, linking `button_pipeline_lib` directly (mirroring `test_minimal_app`'s linkage to `minimal_app_lib`).

**Tests cover** the end-to-end pipeline only. No per-module unit tests (matches `minimal_app`'s precedent). Six scenarios:

1. **Clean press commits after the lockout window.** Post a single `RawTransition{pressed=true}`; advance `kDebounceWindow`; assert `DebouncedButtonState` is now `true`.
2. **Glitch rejection.** Post a rapid sequence of `RawTransition` envelopes within `kDebounceWindow`; advance the window; assert `DebouncedButtonState` reflects the *first* committed edge only, with exactly one write to the cache (verifiable by reading the value and the queue depth).
3. **Single click → `UiMode::Active`.** Post press, advance lockout, post release, advance lockout, advance past `kDoubleClickWindow`; assert `UiMode == Active`.
4. **Double click → `UiMode` follows the double-click branch.** Post press-release-press-release within `kDoubleClickWindow`; assert `UiMode` transitions through the double-click path (concrete final mode depends on the table specified in Implementation Decisions; spec it once in code and assert it in the test).
5. **Long press → `UiMode::Configuring`.** Post press, advance past `kLongPressThreshold`; assert `UiMode == Configuring`. Post release afterwards; assert no further mode transitions.
6. **RAII pool counts return to baseline.** Across a sequence of gestures, sample `subscriber_count()` and `armed_count()` at quiescent moments and assert they never grow without bound — the same "pool occupancy" invariant that `minimal_app`'s test 4 pins.

**Compile-time composition shape check.** A test case mirroring `minimal_app`'s "PRD §Composition shape" `static_assert` block: `Runtime::kNumModules == 4`, `Keys::size == 2`, `Runtime::kMaxSubscriptions == 8`.

**Prior art for the test shape.** `tests/integration/test_minimal_app.cpp` is the canonical reference. The new test follows the same:

- single TU with `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`
- one `TEST_CASE` per scenario, narrative case name
- per-case construction of `ManualClock` + `App{clk}`, `app.start()` ... `app.shutdown()`
- introspection asserts after each `run_one()` to pin observable runtime state.

**Cross-target equivalence.** The same test binary is built and run under both `CORTEXFLOW_TARGET=host` and `CORTEXFLOW_TARGET=posix` (the existing CI matrix). No additional cross-target test fixture is required — the example uses only the typedef-swapped platform header surface.

## Out of Scope

- **Output-side boundary module (LED driver).** Skipped in v1; `UiMode` has no in-tree subscriber. Adding an `LedDriver` later is non-breaking — it just subscribes to `UiMode` and writes a separate `BlinkPattern` key.
- **"Wait-for-silence" debouncer variant.** Only the lockout pattern is implemented. A code comment in `debouncer.hpp` acknowledges the alternative and explains the choice. Adding a second example with the alternative pattern is out of scope.
- **ADR for messages-vs-cache channel choice.** The guidance lives in `examples/button_pipeline/README.md`, not in an ADR. The grilling session evaluated this against the three-criterion bar (hard-to-reverse, surprising-without-context, real-trade-off) and judged it as guidance rather than an architectural decision. The existing `CONTEXT.md` Cache definition ("most-recent value of a continuously-updated quantity") already points at the answer.
- **`CONTEXT.md` updates.** No new domain term emerged from the grilling. All terms used (Module, Boundary module, Flow, State, Cache, Cache key, Subscription, Timer, Owned, Envelope) are already defined.
- **Self-driving "fake button" inside the binary.** Considered and rejected: stdin-driven interactive UX is the binary's story; the tests are the deterministic correctness story. A scripted fake-button source would add a third driver path with no additional teaching value.
- **Per-module unit tests.** Considered and rejected: matches `minimal_app`'s integration-only precedent.
- **Tutorial restructure** (e.g. `examples/01-minimal/`, `examples/02-button-pipeline/`). Considered and rejected: high CMake/README churn for low immediate value. Can be revisited if a third example lands.
- **Real-time backend for host.** Not in scope here — the limitation that `SteadyClock` does not fire timers in v1 (documented in `minimal_app/main.cpp` and `architecture.md` §9) still applies. The button_pipeline binary works because its driving signal comes from stdin (foreign-thread posts), not from the timer service — *timers fire because envelopes arrive and dispatch advances the clock through `run_one`'s normal loop, modulated by `ManualClock` for tests*. The binary under `SteadyClock` will still block during state-local Timer waits the same way `minimal_app` does; that is acceptable in v1.

## Further Notes

- **Why "lockout" debouncer over "wait-for-silence."** The lockout pattern (Locals own a one-shot Timer that fires after the lockout window, with further inputs ignored until then) maps directly onto Locals' construct-on-entry / destruct-on-transition lifetime. Each entry into `CoolingDown` constructs a new Locals (new Timer); each transition out destructs them (cancels the Timer). The canonical "wait-for-silence" pattern requires re-arming the Timer mid-state on every transition, which means handlers mutating Locals via `ctx.locals<L>()`. That pattern is supported (`flow.hpp` line 192), but it has not been demonstrated anywhere in the existing repo, and showing it for the first time in an *example* feels like the wrong place. A future framework-primitive test or a follow-up example could pick up that variant. The chosen pattern is real ("Schmitt-style lockout debouncer") and works on most physical contacts with a long-enough window.

- **Why `ButtonReader` has no Flow.** Stripping it to a pure boundary makes the diagram clearer: the *only* job of the module is to label the in-from-outside boundary, post the message, and demonstrate that boundary modules are not architecturally distinct (no base class, no special dispatch). The complexity that would otherwise live inside `ButtonReader` (sampling logic, edge detection) lives in *whoever is calling `app.post(...)`* — the stdin thread in the binary, or the integration test in `tests/`. That is the framework's preferred coupling direction: the runtime stays sample-rate-agnostic; the source decides when to push.

- **Why `MaxSubscriptions<8>`.** The Classifier holds at most one Subscription at any given time (one per active state), so a peak of one is the theoretical maximum. The runtime's `subscribe<K>()` reserves a slot eagerly though, so the pool needs to hold a slot during *any* possible interleaving. Eight gives generous headroom and is still far below the 16-default in `Config`.

- **Why no Inbox for `ButtonReader`.** A module with an empty `Inbox` is legal (see `minimal_app::Consumer` for precedent). `ButtonReader` declares `using Inbox = std::tuple<>;` so the module registration is well-formed but the dispatch table is empty. The boundary thread posts envelopes addressed to `Debouncer`, not to `ButtonReader`.

- **Compile-time validators we lean on.** `Module::send<Target>(msg)` requires (1) `Target` declares `Inbox`, (2) `Inbox` contains `Msg`, (3) when `ModuleListT` is supplied, `Target` is in the list. The new example does not pass `ModuleListT` to its `Module` base (mirroring `minimal_app`), but every cross-module `send<>()` will trip checks (1) and (2) if a future refactor breaks the message contracts.

- **Diagram drift CI.** Two diagrams currently live under `docs/diagrams/` for `minimal_app` (a flow diagram and a modules diagram). Adding `button_pipeline` adds two more files. The CI line in `README.md`'s "Run everything CI runs" snippet grows from one `gen-diagrams.py` invocation to two; the `git diff --exit-code` check at the end remains a single line.
