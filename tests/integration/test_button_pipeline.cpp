// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// End-to-end scenarios against the button_pipeline example
// (examples/button_pipeline/). This file grows scenario-by-scenario across
// the slice chain in .scratch/button-pipeline-example/issues/. Slice 02
// adds the Debouncer and its lockout-flow scenarios on top of the slice 01
// scaffold.
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
#include <modules/debouncer.hpp>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Compile-time composition shape — pins the current shape so silent
// composition drift fails at compile time. Slice 03 will update these as
// ClickClassifier and UiController land:
//   - slice 03: kNumModules == 4, Keys::size == 2
// ---------------------------------------------------------------------------

static_assert(button_pipeline::Runtime::kNumModules == 2,
              "button_pipeline slice 02: ModuleList<ButtonReader, Debouncer>");
static_assert(button_pipeline::Keys::size == 1,
              "button_pipeline slice 02: one cache key "
              "(Owned<DebouncedButtonState, Debouncer>)");
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
// No module emits anything from on_start, so the queue is empty after
// start(); run_one() returns immediately and shutdown() tears the runtime
// down without asserting. Debouncer's Flow does dispatch its synthetic init
// envelope into Settled on start() — but Settled has no Locals, so no
// subscriptions or timers are armed.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline: start/run_one/shutdown round-trip stays quiescent") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    CHECK(app.queue_size() == 0);
    CHECK(app.cache_ref().subscriber_count() == 0);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .has_value() == false);

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
    // with its Timer armed for kDebounceWindow.
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.queue_size() == 0);

    // Advance past the lockout window; the timer fires, DebounceExpired is
    // posted, and the next run_one() returns the flow to Settled.
    clk.advance(button_pipeline::kDebounceWindow);
    CHECK(app.queue_size() == 1);
    CHECK(app.timers_ref().armed_count() == 0);

    app.run_one();

    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 0);
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

    // Drain all four envelopes. The first commits and arms the timer; the
    // rest land in CoolingDown and stay.
    app.run_one();
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);

    app.run_one();
    app.run_one();
    app.run_one();

    // After all four glitches: still the first committed value, still
    // exactly one armed timer (the one armed on the first edge), and the
    // queue is empty.
    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.queue_size() == 0);

    // Advance past the lockout window — DebounceExpired fires, CoolingDown
    // transitions back to Settled. The cache still holds the first commit.
    clk.advance(button_pipeline::kDebounceWindow);
    app.run_one();

    CHECK(app.cache_ref()
              .get<button_pipeline::DebouncedButtonState>()
              .value_or(false) == true);
    CHECK(app.timers_ref().armed_count() == 0);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}
