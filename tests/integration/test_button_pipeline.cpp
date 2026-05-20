// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// End-to-end scenarios against the button_pipeline example
// (examples/button_pipeline/). This file grows scenario-by-scenario across
// the slice chain in .scratch/button-pipeline-example/issues/. Slice 03
// completes the single-click path: scenario 3 walks a clean press →
// release through Debouncer → ClickClassifier → UiController and observes
// `UiMode::Active`; scenario 6 pins the RAII subscription/timer
// invariants across a full click cycle.
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
// composition drift fails at compile time. Slice 04 will extend this as
// the LongPress / DoubleClick branches arrive (the Configuring state on
// UiController, the double-click-press handler on ClickClassifier).
// ---------------------------------------------------------------------------

static_assert(button_pipeline::Runtime::kNumModules == 4,
              "button_pipeline slice 03: "
              "ModuleList<ButtonReader, Debouncer, ClickClassifier, "
              "UiController>");
static_assert(button_pipeline::Keys::size == 2,
              "button_pipeline slice 03: two cache keys "
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
// Test 5 — RAII pool counts return to baseline across a full gesture
// (PRD scenario 6).
//
// Mirrors `minimal_app`'s test 4: walks the same single-click sequence as
// test 4 and asserts that the subscription pool count and the
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

    // Run the full single-click gesture.
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

    // After the gesture: Classifier is back in Idle (single Subscription
    // alive), UiController is in Active (no Subscription, no Timer), no
    // Timer is armed anywhere. Cache reads confirm both keys are in the
    // expected steady state.
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::UiMode_Key>()
              .value_or(button_pipeline::UiMode::Configuring) ==
          button_pipeline::UiMode::Active);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}
