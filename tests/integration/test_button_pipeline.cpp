// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// End-to-end scenarios against the button_pipeline example
// (examples/button_pipeline/). This file grows scenario-by-scenario across
// the slice chain in .scratch/button-pipeline-example/issues/. After
// slice 05 every gesture in the PRD's state machine is exercised:
// scenario 3 walks a clean press → release through Debouncer →
// ClickClassifier → UiController and observes `UiMode::Active`;
// scenario 4 walks a press-release-press-release sequence inside
// `kDoubleClickWindow` and pins that `UiMode` stays at the baseline
// (the system took the DoubleClick branch, which is a no-op in
// UiController); scenario 5 covers long-press; scenario 6 pins the RAII
// subscription/timer invariants across a full click cycle.
//
// Every scenario drives the App via the public surface only (post +
// run_one + ManualClock::advance). No internal pokes; no real time; no
// real I/O.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/cache.hpp>
#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>

#include <app.hpp>
#include <keys.hpp>
#include <modules/button_reader.hpp>
#include <modules/click_classifier.hpp>
#include <modules/debouncer.hpp>
#include <modules/ui_controller.hpp>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Compile-time composition shape — pins the current shape so silent
// composition drift fails at compile time. After slice 05 the
// ClickClassifier carries its four-state shape end-to-end (Idle, Pressed,
// AwaitingSecondClick, SecondPressed); module / key counts are unchanged.
// ---------------------------------------------------------------------------

static_assert(button_pipeline::Runtime::kNumModules == 4,
              "button_pipeline slice 05: "
              "ModuleList<ButtonReader, Debouncer, ClickClassifier, "
              "UiController>");
static_assert(button_pipeline::Keys::size == 2,
              "button_pipeline slice 05: two cache keys "
              "(Owned<DebouncedButtonState, Debouncer>, "
              "Owned<UiMode_Key, UiController>)");
static_assert(button_pipeline::Runtime::kMaxSubscriptions == 8,
              "button_pipeline: MaxSubscriptions<8>");

namespace {

// Helper: build and post a RawTransition envelope to Debouncer, mirroring
// the path the stdin reader thread will take in the host binary (slice 06).
void post_raw_transition(button_pipeline::App& app, bool pressed) {
    auto ptr = cortexflow::make_message<button_pipeline::Debouncer::RawTransition>(
        button_pipeline::Debouncer::RawTransition{pressed});
    cortexflow::Envelope env(cortexflow::kNoSender,
                             cortexflow::type_id<button_pipeline::Debouncer>(),
                             std::move(ptr));
    app.post(std::move(env));
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1 — App lifecycle round-trip on the current composition.
//
// Slice 03 baseline after `start()`:
//   - ClickClassifier's Idle.Locals holds one Subscription to
//     DebouncedButtonState — that single subscription is the steady-state
//     pool occupancy, and it survives every transition (the new state's
//     Locals immediately re-subscribe).
//   - UiController's Idle.Locals writes UiMode::Idle to the cache on
//     entry, so UiMode_Key reads back Idle before any envelope is posted.
//   - No raw transition has been observed yet, so DebouncedButtonState is
//     empty and no Timer is armed in either Idle state.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: start/run_one/shutdown round-trip stays quiescent") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    CHECK(app.queue_size() == 0);
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .has_value() == false);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);

    app.run_one();  // no envelopes to drain

    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 2 — Clean press commits after the lockout window.
//
// Post a single RawTransition{pressed=true}. The Debouncer in Settled
// commits the new value to DebouncedButtonState immediately on the first
// edge, then transitions to CoolingDown (which arms the lockout Timer). The
// committed cache value is observable as soon as the envelope is drained;
// the lockout window then has to elapse for CoolingDown to return to
// Settled.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: clean press commits DebouncedButtonState") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    post_raw_transition(app, true);
    CHECK(app.queue_size() == 1);

    app.run_one();

    // The leading edge has been committed and CoolingDown is now active
    // with its debounce Timer armed for kDebounceWindow. The fanout
    // KeyChanged<DebouncedButtonState> also drained in this run_one,
    // pushing ClickClassifier from Idle → Pressed; Pressed.Locals arms
    // the long-press Timer for kLongPressThreshold, so two Timers are
    // armed at this point.
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);
    CHECK(app.queue_size() == 0);

    // Advance past the lockout window; only the debounce Timer fires
    // (kDebounceWindow < kLongPressThreshold), DebounceExpired is posted,
    // and the next run_one() returns Debouncer's flow to Settled. The
    // long-press Timer stays armed — Classifier is still in Pressed.
    clk.advance(button_pipeline::kDebounceWindow);
    CHECK(app.queue_size() == 1);
    CHECK(app.timers_ref().armed_count() == 1);

    app.run_one();

    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 3 — Glitch rejection: a tight burst of RawTransitions within the
// lockout window collapses to exactly one committed edge.
//
// Post four envelopes in quick succession (pressed=true, false, true,
// false). After draining all four:
//   - The first envelope commits `true` and transitions to CoolingDown.
//   - The subsequent three arrive while CoolingDown is active; CoolingDown
//     stays on every RawTransition, so no additional cache writes happen.
// The cache short-circuits no-op writes anyway, but the lockout pattern
// guarantees no `set<>()` call is even attempted for the glitches — so the
// observed value stays at the first commit through the entire window.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: glitch train within lockout window commits once") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    post_raw_transition(app, true);   // edge 1 — will commit
    post_raw_transition(app, false);  // glitch — should be ignored
    post_raw_transition(app, true);   // glitch
    post_raw_transition(app, false);  // glitch
    CHECK(app.queue_size() == 4);

    // `run_one` drains the entire queue, including envelopes posted while
    // dispatching earlier ones — so this single call processes all four
    // RawTransitions plus the KeyChanged fanout from the first edge's
    // commit. Debouncer's first edge commits and CoolingDown arms the
    // debounce Timer; the three glitches arrive in CoolingDown and stay.
    // Classifier reacts to the lone KeyChanged<DBS>{nullopt, true} by
    // transitioning Idle → Pressed and arming the long-press Timer.
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);

    // Subsequent run_ones are no-ops — the queue was already empty after
    // the first call.
    app.run_one();
    app.run_one();
    app.run_one();

    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);
    CHECK(app.queue_size() == 0);

    // Advance past the lockout window — DebounceExpired fires (long-press
    // due-time has not arrived, so it stays armed), CoolingDown
    // transitions back to Settled. The cache still holds the first
    // commit; one armed timer remains (Classifier's long-press in
    // Pressed).
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();

    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 4 — Single click drives `UiMode` to `Active` (PRD scenario 3).
//
// The full end-to-end path of slice 03: a clean press, the debounce window
// elapsing, a clean release, the debounce window elapsing again, and
// finally the double-click window elapsing without a second press. After
// the double-click Timer fires, Classifier emits `UiController::Click{}`
// and UiController's Idle.handle transitions to Active — whose Locals
// constructor writes `UiMode::Active` to the cache.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: single click drives UiMode to Active") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // Initial state: Classifier in Idle (sub), UiController in Idle (cache
    // UiMode_Key == Idle), no Timer armed.
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);

    // Press: drains the RawTransition and the resulting KeyChanged<DBS>
    // fanout in a single run_one. After the press, Classifier is in
    // Pressed with the long-press Timer armed; Debouncer is in CoolingDown
    // with the debounce Timer armed.
    post_raw_transition(app, true);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);

    // Advance past the debounce window — debounce Timer fires, returning
    // Debouncer to Settled; long-press stays armed.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);

    // Release: Debouncer commits false and re-enters CoolingDown;
    // Classifier transitions Pressed → AwaitingSecondClick, cancelling
    // the long-press Timer and arming the double-click Timer.
    post_raw_transition(app, false);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(true) == false);
    CHECK(app.timers_ref().armed_count() == 2);  // debounce + double-click

    // Advance past the debounce window — debounce Timer fires.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);  // double-click only

    // Advance past the double-click window — double-click Timer fires,
    // the module-level handler posts `UiController::Click{}`,
    // AwaitingSecondClick → Idle, and on the same run_one UiController's
    // Idle.handle sees `Click` and transitions to Active, writing
    // UiMode::Active to the cache.
    clk.advance(button_pipeline::kDoubleClickWindow);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Active);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 5 — Double click leaves `UiMode` at the baseline (PRD scenario 4).
//
// Drives a press-release-press-release sequence where the second press
// arrives within `kDoubleClickWindow` of the first release. The branch
// under test:
//   - First press → Classifier `Idle → Pressed`.
//   - First release → `Pressed → AwaitingSecondClick` (double-click Timer
//     armed for `kDoubleClickWindow`).
//   - Second press arrives inside the window → `AwaitingSecondClick →
//     SecondPressed`; the Locals dtor cancels the double-click Timer in
//     the same step. No gesture has been emitted yet.
//   - Second release → `SecondPressed → Idle`; the module-level handler
//     sees `KeyChanged<>{false}` arrive in `SecondPressed` and posts
//     `UiController::DoubleClick{}` before the transition is applied.
//   - `UiController` in `Idle` treats `DoubleClick` as a no-op (stay),
//     so `UiMode` remains `Idle`.
//
// The contrast with test 4 is the assertion: the *same* outer cadence
// with a single press-release would have driven `UiMode` to `Active` (as
// test 4 proves). With two press-releases inside the window, `UiMode`
// stays at the baseline `Idle` — which is the proof that the Classifier
// took the two-click branch and not the single-click branch.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: double click leaves UiMode at the baseline Idle") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // Baseline: UiController in Idle, UiMode_Key == Idle.
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);

    // First press: Classifier Idle → Pressed; Debouncer Settled →
    // CoolingDown; both Timers armed.
    post_raw_transition(app, true);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);  // debounce + long-press

    // Drain the debounce window.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);  // long-press only

    // First release: Debouncer commits false and re-arms debounce;
    // Classifier Pressed → AwaitingSecondClick — long-press Timer
    // cancelled by the Locals dtor, double-click Timer armed for
    // kDoubleClickWindow.
    post_raw_transition(app, false);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(true) == false);
    CHECK(app.timers_ref().armed_count() == 2);  // debounce + double-click

    // Drain the debounce window. The double-click Timer is still armed —
    // its window has not elapsed yet, and we are about to land a second
    // press inside it.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);  // double-click only

    // Second press inside the double-click window: Debouncer commits true
    // and arms debounce; Classifier AwaitingSecondClick → SecondPressed —
    // the Locals dtor cancels the double-click Timer in the same step. No
    // gesture has been emitted yet, so UiMode is still Idle.
    post_raw_transition(app, true);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);  // debounce only
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);

    // Drain debounce.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 0);

    // Second release: Classifier SecondPressed → Idle; the module-level
    // handler posts `UiController::DoubleClick{}` on the way through.
    // UiController is in Idle, treats DoubleClick as a no-op, stays in
    // Idle — so UiMode remains Idle.
    post_raw_transition(app, false);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(true) == false);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);
    CHECK(app.timers_ref().armed_count() == 1);  // debounce only

    // Advance past the debounce window and well past `kDoubleClickWindow`
    // (which is > kDebounceWindow) to make sure no spurious Timer fires
    // and no late gesture leaks through. After all the dust settles,
    // `UiMode` must still be `Idle` — the contrast assertion against
    // test 4, where the same outer cadence ended at `Active`.
    clk.advance(button_pipeline::kDoubleClickWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 6 — Long press drives `UiMode` to `Configuring`; the eventual
// release is a no-op (PRD scenario 5 / issue 04 scenario 5).
//
// Post a press, drain the debounce window, then advance past
// `kLongPressThreshold`. The long-press Timer fires; ClickClassifier's
// module-level handler posts `UiController::LongPress{}` and the flow
// transitions `Pressed` → `Idle`, discarding the eventual release. The
// UiController, on receiving `LongPress` in `Idle`, transitions to
// `Configuring`, whose Locals constructor writes `UiMode::Configuring` to
// the cache. A subsequent release lands at Classifier-Idle, which treats
// `new_value == false` as a no-op — so `UiMode` stays at `Configuring`
// after the release drains.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: long press drives UiMode to Configuring; release is a no-op") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // Press: Classifier Idle → Pressed (long-press Timer armed for
    // kLongPressThreshold); Debouncer Settled → CoolingDown (debounce
    // Timer armed for kDebounceWindow).
    post_raw_transition(app, true);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 2);

    // Drain the debounce window: debounce Timer fires, CoolingDown →
    // Settled. The long-press Timer (kLongPressThreshold > kDebounceWindow)
    // stays armed.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);

    // Advance past the long-press threshold — long-press Timer fires.
    // The module-level handler posts `UiController::LongPress{}` and
    // Classifier transitions Pressed → Idle (long-press Timer's dtor
    // releases the already-fired seq cleanly). UiController, in Idle,
    // sees `LongPress` and transitions to `Configuring`, writing
    // `UiMode::Configuring` to the cache.
    clk.advance(button_pipeline::kLongPressThreshold);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Idle) ==
          button_pipeline::UiMode::Configuring);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.queue_size() == 0);

    // Now release: Debouncer commits `false` and arms a fresh debounce
    // Timer; Classifier (in Idle) sees `KeyChanged<>{false}` and stays
    // — the release post-long-press is intentionally absorbed. No
    // gesture is emitted to UiController, so UiMode stays at
    // `Configuring`.
    post_raw_transition(app, false);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(true) == false);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Idle) ==
          button_pipeline::UiMode::Configuring);
    CHECK(app.timers_ref().armed_count() == 1);  // debounce only

    // Drain the debounce; nothing else should change.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Idle) ==
          button_pipeline::UiMode::Configuring);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 7 — A second long press from `Configuring` returns `UiMode` to
// `Idle` (issue 04 sub-case).
//
// Sequences a full long-press → release → press → long-press cycle:
// after the first long-press the system is in `Configuring`; after the
// second the `Configuring → Idle` transition fires and the cache reads
// back `UiMode::Idle`.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: long press from Configuring returns UiMode to Idle") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // First long press: drive UiMode to Configuring.
    post_raw_transition(app, true);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    clk.advance(button_pipeline::kLongPressThreshold);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Idle) ==
          button_pipeline::UiMode::Configuring);

    // Release the button and drain the debounce window so DBS == false
    // before we press again. UiMode stays Configuring throughout.
    post_raw_transition(app, false);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(true) == false);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Idle) ==
          button_pipeline::UiMode::Configuring);
    CHECK(app.timers_ref().armed_count() == 0);

    // Second press from `Configuring`: Classifier Idle → Pressed
    // re-arms the long-press Timer.
    post_raw_transition(app, true);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 2);  // debounce + long-press

    // Drain debounce.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    CHECK(app.timers_ref().armed_count() == 1);

    // Advance past the long-press threshold. LongPress is sent;
    // UiController `Configuring` → `Idle` writes UiMode::Idle.
    clk.advance(button_pipeline::kLongPressThreshold);
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Idle);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 8 — RAII pool counts return to baseline across a full gesture
// (PRD scenario 6).
//
// Walks the same single-click sequence as test 4 and asserts that the
// subscription pool count and the
// armed-Timer count settle back to values consistent with a single
// Classifier subscription and zero armed Timers once Classifier is back
// in Idle. Any silent leak in a state's Locals dtor (e.g. a Timer not
// held by value, a Subscription captured by reference) would surface
// here as a non-zero delta.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: RAII pool counts return to baseline after click") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    // Baseline: Classifier.Idle holds one Subscription; no Timer is armed.
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);

    // Phase 1: full single-click gesture. Exercises Idle → Pressed →
    // AwaitingSecondClick → Idle and the long-press + double-click Timer
    // lifecycles.
    post_raw_transition(app, true);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    post_raw_transition(app, false);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    clk.advance(button_pipeline::kDoubleClickWindow);
    app.run_one();

    // After the single click: subscription count back to 1, no Timer armed,
    // UiController in Active.
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Active);

    // Phase 2: full double-click gesture from `Active`. Exercises the new
    // `SecondPressed` state's Locals dtor (Subscription must be released
    // cleanly even though the state holds no Timer) and the
    // AwaitingSecondClick → SecondPressed Timer-cancellation path. The
    // outer cadence mirrors test 5; the assertion here is only that the
    // pool counts return to baseline regardless of which branch the
    // Classifier took.
    post_raw_transition(app, true);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    post_raw_transition(app, false);
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    post_raw_transition(app, true);   // second press inside double-click window
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();
    post_raw_transition(app, false);  // second release: emit DoubleClick
    app.run_one();
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();

    // After the double click: subscription count still 1, no Timer armed,
    // UiController unchanged at `Active` (DoubleClick is a no-op in
    // every UiController state).
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Active);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}
