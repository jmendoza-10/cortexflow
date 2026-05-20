# Add the double-click branch end-to-end

Status: merged
## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Complete the gesture-recognition state machine by wiring the double-click branch through the pipeline. After this slice the example covers every gesture the PRD specifies and the integration test covers every scenario.

`ClickClassifier` gains its fourth state, `SecondPressed`, whose `Locals` holds a `cortexflow::Subscription` to `DebouncedButtonState`. The branch is wired off of `AwaitingSecondClick`:

- `AwaitingSecondClick` already transitions to `Idle` on its double-click timer (emitting `Click`) — that path is unchanged.
- `AwaitingSecondClick` gains a new transition: on `KeyChanged<DebouncedButtonState>` whose `new_value` is `true` (a second press within the double-click window), transition to `SecondPressed`. The state's Timer is cancelled automatically by the Locals destructor on transition out.
- `SecondPressed` returns `transition_to<Idle>()` on `KeyChanged<>` whose `new_value` is `false` (the release), after calling `send<UiController>(UiController::DoubleClick{})`.

`UiController` requires no new states or transition-table changes — `DoubleClick` is already declared as a no-op (`stay`) in all three states per slice 03 and slice 04. This is the design captured in the PRD's Implementation Decisions: the double-click branch's value here is *demonstrating that the Classifier correctly distinguishes single from double press sequences*, not that double-clicks drive a different UI behavior.

The integration test gains scenario 4. Its job is to pin that a double press-release-press-release sequence within `kDoubleClickWindow` results in `DoubleClick` (not two `Click`s) being delivered to `UiController`, and that `UiMode` stays in its baseline because `DoubleClick` is a no-op for `UiController`. The proof technique: contrast with scenario 3 — the *exact same input timing* without the second press-release would produce `UiMode == Active`; with the second press-release, `UiMode` stays `Idle`. That contrast is the assertion that the Classifier took the two-click branch.

## Acceptance criteria

- [ ] `ClickClassifier` adds a `SecondPressed` state to `StateList<Idle, Pressed, AwaitingSecondClick, SecondPressed>`.
- [ ] `SecondPressed.Locals` holds a `cortexflow::Subscription` to `DebouncedButtonState`.
- [ ] `AwaitingSecondClick.handle` gains a transition: on `KeyChanged<DebouncedButtonState>` whose `new_value` is `true`, return `transition_to<SecondPressed>()`.
- [ ] `SecondPressed.handle` on `KeyChanged<>` whose `new_value` is `false`: `send<UiController>(UiController::DoubleClick{})` then return `transition_to<Idle>()`.
- [ ] `UiController` is unchanged in this slice (`DoubleClick` is already a `stay` no-op in all states per prior slices).
- [ ] Integration test gains scenario 4: post a press-release-press-release sequence with debounce + double-click timing such that the second press arrives within `kDoubleClickWindow` of the first release; after advancing past `kDoubleClickWindow`, assert `UiMode` is still `Idle` (the baseline before the sequence), demonstrating that the system did *not* take the single-click branch.
- [ ] Scenario 4 includes a tightened sub-assertion that contrasts with scenario 3: the same outer cadence with a *single* press-release would have ended in `Active`; with two press-releases it ends in `Idle`.
- [ ] Scenario 6 (RAII pool counts) still passes; the new state is exercised by the test sequence.
- [ ] Test `static_assert` updated to reflect the Classifier's 4-state shape if the framework exposes a count (otherwise omit).
- [ ] All existing tests still pass; build clean under both `host` and `posix` targets.

## Blocked by

- [04-add-long-press-branch](04-add-long-press-branch.md)

## Comments

Built — all acceptance criteria met:

- `ClickClassifier`'s `StateList` now carries `<Idle, Pressed, AwaitingSecondClick, SecondPressed>`.
- `SecondPressed.Locals` holds only a `Subscription` (no Timer — the gesture resolves on release, not a deadline).
- `AwaitingSecondClick.handle` transitions to `SecondPressed` on `KeyChanged<DBS>{true}`; the AwaitingSecondClick Locals dtor cancels the double-click Timer in the same step.
- `SecondPressed.handle` transitions to `Idle` on `KeyChanged<DBS>{false}`.
- `UiController` is unchanged.
- Integration test gained scenario 5 (test ordering: existing test 4 is single-click, new test 5 is double-click, old test 5/6/7 shifted to 6/7/8). The test posts press-release-press-release with the second press inside `kDoubleClickWindow`, drains past `kDoubleClickWindow`, and asserts `UiMode == Idle` — explicitly contrasting with test 4 in the comment header.
- The RAII test (now test 8) extends after the single-click phase to also walk the full double-click sequence, exercising `SecondPressed`'s Locals dtor and the `AwaitingSecondClick → SecondPressed` Timer-cancellation path.
- All 23 ctest targets pass on both `host` and `posix`.

One design note worth flagging for the reviewer:

The issue text says "`SecondPressed.handle` ... `send<UiController>(UiController::DoubleClick{})` then return `transition_to<Idle>()`", but state handlers are static and can't reach the module's `send<>` machinery (the same constraint that already drove timer-fire payloads to be intercepted at the module-level `handle` in slices 03/04). I followed the slice-04 pattern: `SecondPressed.handle` only returns the transition, and the module-level `handle` posts `DoubleClick` to UiController by gating on `flow.current() == &cortexflow::detail::kStateInfo<SecondPressed>` plus the `KeyChanged<DBS>{false}` payload. This uses `cortexflow::detail::kStateInfo<>` (a `detail::` symbol) — same compromise as the timer-payload type-id check, and the example is already deeply intermediate with framework internals (`type_id<>`, `Module<>`, etc.). The acceptance criterion's "what the SecondPressed state does on release" is preserved logically; the `send` just lives one frame up. If the reviewer wants to keep state handlers strictly out of `cortexflow::detail::`, an alternative would be a per-state dispatch shim, but that's a framework-level change outside the slice's scope.

— 2026-05-20, from afk worker
