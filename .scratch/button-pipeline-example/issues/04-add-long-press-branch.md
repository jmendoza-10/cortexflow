# Add the long-press branch end-to-end

Status: merged
## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Wire the long-press gesture path through the pipeline so a held button transitions the UI into `UiMode::Configuring`. After this slice the click and long-press gestures are both functional; double-click arrives in slice 05.

`ClickClassifier.Pressed` already arms a long-press `Timer` (added in slice 03 for the RAII-lifecycle test). This slice attaches the *behavior*: when the long-press timer fires while `Pressed` is the active state, `ClickClassifier` calls `send<UiController>(UiController::LongPress{})` and returns `transition_to<Idle>()` — discarding the eventual release because the gesture has already been classified. The release that follows lands in `Idle`, which is already specified to treat a "released" `KeyChanged` as a no-op.

`UiController` gains its third and final state, `Configuring`, whose `Locals` constructor writes `UiMode::Configuring` to the cache on entry. The transition table grows:

- `Idle`: on `LongPress` → `Configuring` (was: stay in slice 03).
- `Active`: on `LongPress` → `Configuring` (was: stay).
- `Configuring` (new): on `LongPress` → `Idle`; on `Click` → stay; on `DoubleClick` → stay.

The integration test gains scenario 5: post a press, advance time past `kLongPressThreshold`, assert `UiMode == Configuring`. Post a subsequent release; advance to drain; assert `UiMode` remains `Configuring` (the release post-long-press should not produce a second gesture). A separate sub-case posts a long-press while already in `Configuring` and asserts the system returns to `Idle`.

## Acceptance criteria

- [ ] `ClickClassifier.Pressed.handle` returns `transition_to<Idle>()` after calling `send<UiController>(UiController::LongPress{})` when the long-press timer payload arrives.
- [ ] The long-press payload is a nested type inside `ClickClassifier` (e.g. `ClickClassifier::LongPressExpired {}`), `public`, and is what `Pressed.Locals`'s Timer carries.
- [ ] After emitting `LongPress`, `Pressed` does not also emit `Click` on the next release — the release in `Idle` is a no-op as already specified.
- [ ] `UiController` gains a `Configuring` state with `Locals` whose constructor calls `cache().set<UiMode>(UiMode::Configuring)`.
- [ ] `UiController::flow` becomes `Flow<UiController, StateList<Idle, Active, Configuring>>`.
- [ ] Transition table after this slice:
  - `Idle`: `Click → Active`, `LongPress → Configuring`, `DoubleClick → stay`.
  - `Active`: `Click → Idle`, `LongPress → Configuring`, `DoubleClick → stay`.
  - `Configuring`: `LongPress → Idle`, `Click → stay`, `DoubleClick → stay`.
- [ ] Integration test gains scenario 5: press, advance past `kLongPressThreshold`, assert `UiMode == Configuring`; post release, drain, assert mode stays `Configuring`.
- [ ] Integration test sub-case: press from `Configuring`, advance past `kLongPressThreshold`, assert `UiMode` returns to `Idle`.
- [ ] Scenario 6 (RAII pool counts) still passes after the new state is introduced.
- [ ] Test `static_assert` updated to assert the Flow now has 3 states (e.g. `UiController::flow_type::state_count == 3` if the framework exposes it; otherwise omit — the runtime composition-shape asserts already cover module / key counts).
- [ ] All existing tests still pass; build clean under both `host` and `posix` targets.

## Blocked by

- [03-add-classifier-and-uicontroller-click-path](03-add-classifier-and-uicontroller-click-path.md)

## Comments

What I built:
- `ClickClassifier::handle` now intercepts `LongPressExpired` in the same
  way it intercepts `DoubleClickExpired`: it posts
  `UiController::LongPress{}` via `send<>` and then forwards the envelope
  into `flow.step`. `Pressed::handle` returns `transition_to<Idle>()` on
  `LongPressExpired`. The release that follows lands in Idle and stays,
  as previously specified.
- `UiController` gained the `Configuring` state with `Locals` that writes
  `UiMode::Configuring` on entry. The Flow is now
  `Flow<UiController, StateList<Idle, Active, Configuring>>`. Idle/Active
  transition to Configuring on `LongPress`; Configuring transitions back
  to Idle on `LongPress` and stays on Click/DoubleClick.
- Integration tests gained two new scenarios:
  - "long press drives UiMode to Configuring; release is a no-op" —
    presses, advances past `kLongPressThreshold`, asserts `Configuring`;
    then posts release, drains debounce, asserts the mode stays
    `Configuring`.
  - "long press from Configuring returns UiMode to Idle" — drives one
    full long-press cycle to `Configuring`, releases and drains the
    debounce, presses again, advances past `kLongPressThreshold`, and
    asserts `UiMode == Idle`.
- The existing test labelled "Test 5 — RAII pool counts" is now Test 7
  in source-order; its assertions are unchanged and it still passes.

Anything skipped or deferred:
- The acceptance-criterion suggesting `flow_type::state_count == 3` was
  omitted because the framework does not expose `flow_type` or
  `state_count` on `Flow`/`StateList`. The issue explicitly authorises
  omission in that case; the runtime composition-shape `static_assert`s
  (kNumModules == 4, Keys::size == 2, kMaxSubscriptions == 8) already
  cover module / key counts.

What the reviewer should pay attention to:
- The module-level `handle` now branches on two timer-fire payloads with
  an `if / else if`. Each timer payload is still armed by exactly one
  state, so the conditional emission is safe in the same way it was for
  `DoubleClickExpired` alone.
- The post-long-press release scenario is the most subtle part: when
  Classifier sits in Idle with `DBS == true` after a long-press fires,
  the eventual `RawTransition{false}` flows through Debouncer →
  KeyChanged<DBS>{false} → Classifier.Idle.handle, which already treats
  `new_value == false` as a no-op. UiMode stays at `Configuring` for the
  whole release drain.
- Both targets (`-DCORTEXFLOW_TARGET=host` and `-DCORTEXFLOW_TARGET=posix`)
  build clean and all 23 ctest cases pass under both.

— 2026-05-20, from afk worker
