# Add ClickClassifier and UiController with the single-click path

Status: merged
## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Extend the pipeline so a single, well-formed click drives the UI mode all the way through to `UiMode::Active`. After this slice the example is end-to-end functional for the click gesture; long-press and double-click branches arrive in later slices.

Two modules are added in this slice because the single-click path crosses both: `ClickClassifier` observes `DebouncedButtonState` and emits a gesture message; `UiController` receives gesture messages and publishes `UiMode`.

`ClickClassifier` has three of its four eventual states: `Idle`, `Pressed`, and `AwaitingSecondClick`. Each state's `Locals` holds a `cortexflow::Subscription` to `DebouncedButtonState`, so the cache's `KeyChanged<>` fan-out reaches the active state regardless of which one is current. `Pressed` and `AwaitingSecondClick` additionally hold a `cortexflow::Timer` — `Pressed`'s timer is the long-press threshold (armed but unused in this slice; the LongPress emission is wired in the next slice), and `AwaitingSecondClick`'s timer is the double-click window. When the double-click timer fires in `AwaitingSecondClick`, `ClickClassifier` calls `send<UiController>(UiController::Click{})` and returns `transition_to<Idle>()`.

`UiController` ships with all three gesture message types defined as `public` nested members — `Click {}`, `DoubleClick {}`, `LongPress {}` — even though only `Click` is sent in this slice. Declaring them now keeps the message vocabulary stable across the next two slices; later slices add the *senders* and the *state-handling* code, not the type declarations. `UiController::Inbox` is `std::tuple<Click, DoubleClick, LongPress>` so the compile-time `Module::send<>` validators accept calls from `ClickClassifier` in this slice and the next two. `UiController` overrides `handle()` to forward to `flow.step` (same justification as `Debouncer` and `Consumer`).

`UiController` has two of its three eventual states: `Idle` and `Active`. Each state's `Locals` constructor writes its corresponding `UiMode` value to the cache on entry — the "side-effect on entry" pattern called out in the PRD. The transition table for this slice:

- `Idle`: on `Click` → `Active`; on `DoubleClick` → stay; on `LongPress` → stay.
- `Active`: on `Click` → `Idle`; on `DoubleClick` → stay; on `LongPress` → stay.

(`LongPress` becomes meaningful in the next slice when the `Configuring` state appears; for now it is a no-op that pins the design's "no-op gesture is observable but inert" guarantee.)

`keys.hpp` adds `UiMode` as an `enum class` and a cache key whose `value_type` is that enum. The composition declaration grows to all four modules and both cache keys; the compile-time shape check moves to `Runtime::kNumModules == 4` and `Keys::size == 2`. The `kDoubleClickWindow` and `kLongPressThreshold` constants both land in `app.hpp` in this slice (the latter is used by `Pressed.Locals`'s timer arming).

The integration test gains scenario 3 (single click → `UiMode::Active`) and scenario 6 (RAII pool counts return to a known baseline across a full gesture). Scenario 3 walks the end-to-end path: post `RawTransition{pressed=true}`, advance the debounce window, post `RawTransition{pressed=false}`, advance the debounce window, advance past `kDoubleClickWindow`, then assert `UiMode == Active`. Scenario 6 pins that after returning to `Idle`, `subscriber_count()` and `armed_count()` settle to the values consistent with a single Classifier subscription and no armed timers — the same RAII-Locals invariant `minimal_app`'s test 4 pins.

## Acceptance criteria

- [ ] `examples/button_pipeline/modules/click_classifier.hpp` / `.cpp` and `modules/ui_controller.hpp` / `.cpp` exist with SPDX headers.
- [ ] `ClickClassifier` declares an empty `Inbox` and overrides `handle()` to forward to `flow.step`.
- [ ] `ClickClassifier::flow` is `Flow<ClickClassifier, StateList<Idle, Pressed, AwaitingSecondClick>>` with `Idle` as the initial state.
- [ ] Every Classifier state's `Locals` holds a `cortexflow::Subscription` to `DebouncedButtonState`.
- [ ] `Pressed.Locals` additionally holds a `cortexflow::Timer` armed for `kLongPressThreshold` carrying a Classifier-nested timer payload (the LongPress emission itself is wired in slice 04, but the timer is armed here so its lifecycle is exercised).
- [ ] `AwaitingSecondClick.Locals` holds a `cortexflow::Timer` armed for `kDoubleClickWindow`; on its fire, `ClickClassifier` calls `send<UiController>(UiController::Click{})` and the state returns `transition_to<Idle>()`.
- [ ] State transitions implement the single-click path: `Idle` → `Pressed` on `KeyChanged<DebouncedButtonState>` whose `new_value` is true; `Pressed` → `AwaitingSecondClick` on `KeyChanged<>` whose `new_value` is false; `AwaitingSecondClick` → `Idle` on its timer fire (emitting `Click`).
- [ ] `UiController` defines three `public` nested message types: `Click {}`, `DoubleClick {}`, `LongPress {}`.
- [ ] `UiController::Inbox` is `std::tuple<Click, DoubleClick, LongPress>` and `handle()` is overridden to forward to `flow.step`.
- [ ] `UiController::flow` is `Flow<UiController, StateList<Idle, Active>>` (the `Configuring` state arrives in slice 04).
- [ ] Each `UiController` state's `Locals` constructor calls `cache().set<UiMode>(...)` with its corresponding enum value.
- [ ] Transition table for this slice: `Idle{Click→Active, DoubleClick/LongPress→stay}`, `Active{Click→Idle, DoubleClick/LongPress→stay}`.
- [ ] `keys.hpp` declares `enum class UiMode { Idle, Active, Configuring };` and `struct UiMode_Key { using value_type = UiMode; };` (or however the implementation names the key; the value_type must be the enum, and the key type appears in `CacheKeyList<>`). The enum has all three values declared even though `Configuring` is unused in this slice.
- [ ] `app.hpp` extends `Modules` to `ModuleList<ButtonReader, Debouncer, ClickClassifier, UiController>` and `Keys` to include `Owned<UiMode (key), UiController>` alongside the Debouncer-owned key.
- [ ] `app.hpp` declares `inline constexpr std::chrono::milliseconds kLongPressThreshold{500};` and `inline constexpr std::chrono::milliseconds kDoubleClickWindow{300};` at namespace scope.
- [ ] CMake target `button_pipeline_lib` adds the two new module `.cpp` files.
- [ ] Integration test gains scenario 3: single press-release at debounced speeds, advance past double-click window, assert `UiMode == Active`.
- [ ] Integration test gains scenario 6: across a full click cycle ending in `Idle`, `subscriber_count() == 1` (Classifier's `Idle`-state Subscription) and `armed_count() == 0` (no timers armed in `Idle`).
- [ ] Test `static_assert` updated to `Runtime::kNumModules == 4` and `Keys::size == 2`.
- [ ] All existing tests still pass; build clean under both `host` and `posix` targets.

## Blocked by

- [02-add-debouncer-with-lockout-flow](02-add-debouncer-with-lockout-flow.md)

## Comments

### 2026-05-20 — from afk worker

Built the two new modules and the single-click end-to-end path. Composition
is now `ModuleList<ButtonReader, Debouncer, ClickClassifier, UiController>`
with `CacheKeyList<Owned<DebouncedButtonState, Debouncer>, Owned<UiMode_Key,
UiController>>`. All five integration test cases (39 assertions) pass under
both `host` and `posix` targets; the full 23-test ctest suite is green on
both.

Implementation notes worth a reviewer's eye:

- **State structs are nested in their owning class** (`ClickClassifier::Idle`
  vs `UiController::Idle`) rather than at namespace scope as Debouncer's
  `Settled`/`CoolingDown` are. The reason is concrete: both modules have an
  `Idle` state in this slice, and namespace-scope structs would collide. The
  buffer-sizing guarantee the Debouncer file's comment calls out is
  preserved — every state's Locals is complete at the point the `flow`
  member is declared inside the class body.

- **UiController has empty `on(Click&)`/`on(DoubleClick&)`/`on(LongPress&)`
  stubs alongside the `handle` override.** The framework's `Module<>`
  eagerly instantiates `dispatch_handler<Derived, Msg>` for every type in
  `Inbox` (it must — the constexpr dispatch table backs the vtable slot for
  `Module::handle`, even though our `handle` override never reaches that
  table). With `Inbox = std::tuple<Click, DoubleClick, LongPress>`, the
  dispatch handlers resolve `Derived::on(Msg&)` and fail to compile if
  those methods don't exist. Debouncer and Consumer get away without stubs
  only because their Inbox is empty. There's a header comment on the stubs
  explaining this. If the framework grows a way to opt out of dispatch
  (e.g. an Inbox-but-no-table marker), the stubs could go away — flagged
  for the framework owner to consider, but it's out of scope here.

- **`Pressed`'s long-press Timer is armed but its handler is intentionally
  unwired** in this slice, per the issue's wording ("the LongPress
  emission is wired in slice 04"). The timer's RAII lifecycle is exercised
  by every press/release cycle — `Pressed.Locals` dtor cancels it on
  transition out. Test 4 (single click → Active) walks this path with
  asserts on `armed_count()` along the way.

- **Existing test 1/2/3 assertions were updated**, not just appended to.
  Slice 03 adds the Classifier's reaction to `KeyChanged<DBS>` fanout
  (Idle → Pressed), which arms a long-press Timer and pins one
  Subscription in the pool. Every previous `armed_count()` and
  `subscriber_count()` assertion that was true in slice 02 needed the
  Classifier's contributions added in. The intent of each test
  (Debouncer-specific behaviour) is preserved; only the runtime-state
  expectations changed. Per the issue's "All existing tests still pass"
  criterion and the static_assert update requirement, this read as
  in-scope, but flagging it explicitly since the changes touch test
  assertions written in slice 02.

- **PRD-numbered test cases.** Test 4 maps to PRD scenario 3
  (single-click); test 5 maps to PRD scenario 6 (RAII baseline). Tests
  1/2/3 are the slice 01 scaffold round-trip plus PRD scenarios 1/2
  inherited from slice 02. PRD scenarios 4 (double-click) and 5
  (long-press) arrive in slice 04.

Nothing skipped or deferred relative to the acceptance criteria — every
checkbox is satisfied as written.

