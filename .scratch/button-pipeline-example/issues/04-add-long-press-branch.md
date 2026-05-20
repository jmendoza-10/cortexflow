# Add the long-press branch end-to-end

Status: ready-for-agent

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
