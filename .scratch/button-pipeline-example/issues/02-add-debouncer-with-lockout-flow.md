# Add Debouncer with lockout Flow and `DebouncedButtonState` cache key

Status: merged
## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Extend the scaffolded `button_pipeline` example with its first real module: the `Debouncer`. After this slice, the example takes raw transitions in (posted via `app.post(...)` like the stdin thread will later) and publishes a clean `DebouncedButtonState` to the cache, with glitches shorter than the lockout window suppressed.

`Debouncer` owns a **Flow** with two states — `Settled` and `CoolingDown` — implementing the lockout debounce pattern: commit the new value on the first edge, then ignore further raw transitions for `kDebounceWindow` while `CoolingDown` is active. The state machine maps directly onto Locals lifetime: `CoolingDown.Locals` constructs a `cortexflow::Timer` armed for the lockout window; the Timer is cancelled automatically when the state transitions out (it never does in this design, because the timer is what triggers the transition back). A code comment in `debouncer.hpp` calls out the "wait-for-silence" alternative and explains why the lockout pattern was chosen (PRD Further Notes).

`Debouncer::RawTransition { bool pressed; }` and `Debouncer::DebounceExpired {}` are defined as `public` nested types per ADR-0020. `RawTransition` is what `ButtonReader` (or, more accurately, the foreign thread / test that posts on `ButtonReader`'s behalf) sends in. `DebounceExpired` is the self-sent timer payload. Both message types must be declared so the receiver-owned-messages convention is fully demonstrated end-to-end with cross-module sends, but `Debouncer`'s `Inbox` stays `std::tuple<>` and the module-level `handle()` forwards every envelope to `flow.step` (the same pattern `minimal_app::Consumer` uses, justified the same way: state handlers, not the dispatch table, route by payload type).

The cache key `DebouncedButtonState` is declared in `keys.hpp` (`using value_type = bool;`) and listed in the composition's `CacheKeyList` as `Owned<DebouncedButtonState, Debouncer>`. `Debouncer` writes to it once per accepted edge — the framework's `Cache::set` already short-circuits no-op writes (compare-equal), so subscribers never see redundant fanouts.

The integration test grows by two scenarios. Scenario 1 posts a clean press, advances the clock past `kDebounceWindow`, and asserts `DebouncedButtonState == true`. Scenario 2 posts a tight burst of `RawTransition` envelopes within the lockout window, advances past the window, and asserts `DebouncedButtonState` reflects only the *first* committed edge and `cache_ref().get<DebouncedButtonState>()` was written exactly once (verifiable by checking the value and that no subscriber-fanout-queue oddities surfaced, since there are still no subscribers).

## Acceptance criteria

- [ ] `examples/button_pipeline/modules/debouncer.hpp` and `debouncer.cpp` exist with SPDX headers.
- [ ] `Debouncer` declares `public struct RawTransition { bool pressed; };` and `public struct DebounceExpired {};` as nested types.
- [ ] `Debouncer` declares `using Inbox = std::tuple<>;` and overrides `handle(cortexflow::Envelope&)` to forward every envelope to `flow.step(env)` (matching `minimal_app::Consumer`'s pattern).
- [ ] `Debouncer::flow` is `cortexflow::Flow<Debouncer, cortexflow::StateList<Settled, CoolingDown>>`.
- [ ] `Settled` has empty Locals; its `handle` reads `cache().get<DebouncedButtonState>().value_or(false)`, compares to `env.payload<RawTransition>().pressed`, and on difference writes the new value to the cache and returns `transition_to<CoolingDown>()`.
- [ ] `CoolingDown.Locals` holds a `cortexflow::Timer` armed via `timers().arm<Debouncer>(kDebounceWindow, Debouncer::DebounceExpired{})`; its `handle` returns `transition_to<Settled>()` on receipt of `DebounceExpired` and `stay()` on any `RawTransition`.
- [ ] `keys.hpp` declares `struct DebouncedButtonState { using value_type = bool; };`.
- [ ] `app.hpp` extends `Modules` to `cortexflow::ModuleList<ButtonReader, Debouncer>` and `Keys` to `cortexflow::CacheKeyList<cortexflow::Owned<DebouncedButtonState, Debouncer>>`.
- [ ] `app.hpp` declares `inline constexpr std::chrono::milliseconds kDebounceWindow{5};` at namespace scope.
- [ ] CMake target `button_pipeline_lib` adds `modules/debouncer.cpp` to its sources.
- [ ] Integration test `tests/integration/test_button_pipeline.cpp` gains two `TEST_CASE`s:
  - Scenario 1: clean press → `DebouncedButtonState` flips from empty/false to `true` after the lockout window elapses.
  - Scenario 2: a high-frequency glitch train within `kDebounceWindow` results in a single committed edge; `DebouncedButtonState` reflects only the first commit; subsequent intra-window transitions do not produce additional writes (verifiable by reading the value after the window expires).
- [ ] Test `static_assert` updated to `Runtime::kNumModules == 2` and `Keys::size == 1`.
- [ ] `debouncer.hpp` carries a code comment acknowledging the wait-for-silence debouncer alternative and explaining the lockout-pattern choice.
- [ ] All existing tests still pass; build clean under both `host` and `posix` targets.

## Blocked by

- [01-scaffold-with-button-reader](01-scaffold-with-button-reader.md)

## Comments

Built (2026-05-20, from afk worker):

- Added `examples/button_pipeline/modules/debouncer.{hpp,cpp}` with the
  `Settled` / `CoolingDown` state pair and the lockout-pattern comment.
  Both state structs are *defined* before the `Debouncer` class so the
  Flow buffer is sized correctly (same forward-declare-then-define
  layout `minimal_app::Consumer` uses).
- `Debouncer::RawTransition { bool pressed; }` and
  `Debouncer::DebounceExpired {}` are public nested types per ADR-0020.
  `Inbox = std::tuple<>;` and the module-level `handle()` forwards every
  envelope to `flow.step` — `on_start` calls `flow.start(*this)`.
- `Settled::handle` reads
  `cache().get<DebouncedButtonState>().value_or(false)`, compares to the
  envelope's `RawTransition::pressed`, writes the new value on
  difference, and returns `transition_to<CoolingDown>()`.
- `CoolingDown::Locals::Locals()` arms the timer via
  `timers().arm<Debouncer>(kDebounceWindow,
  Debouncer::DebounceExpired{})`; its `handle` returns
  `transition_to<Settled>()` on `DebounceExpired` and `stay()` on every
  `RawTransition` (which is what makes glitches collapse).
- `keys.hpp` declares `DebouncedButtonState { using value_type = bool; }`.
- `app.hpp` extends `Modules` to `ModuleList<ButtonReader, Debouncer>`,
  `Keys` to `CacheKeyList<Owned<DebouncedButtonState, Debouncer>>`, and
  declares `inline constexpr std::chrono::milliseconds kDebounceWindow{5};`
  at namespace scope.
- `button_pipeline_lib` source list gains `modules/debouncer.cpp`.
- `tests/integration/test_button_pipeline.cpp` gains the clean-press and
  glitch-train scenarios; the lifecycle round-trip case now also asserts
  `DebouncedButtonState` is unset at start. The compile-time shape
  check moves to `kNumModules == 2` / `Keys::size == 1`.

Reviewer notes:

- The integration test's "exactly one write" criterion (acceptance
  criterion's Scenario 2) is verified indirectly: the cache holds the
  first commit through the entire window and `Cache::set` already
  short-circuits compare-equal writes — but the lockout pattern is the
  stronger guarantee, because the further three envelopes hit
  `CoolingDown::handle` which never even reaches `cache().set`. There
  is no public subscriber-fanout counter to observe directly without
  adding a subscriber, which `keys.hpp` deliberately doesn't yet have.
- All 23 tests pass under both `CORTEXFLOW_TARGET=host` and
  `CORTEXFLOW_TARGET=posix`. `spdx_headers` and `no_target_ifdefs`
  guards stay green.
- Did not commit the issue-file edits per the runner protocol.
