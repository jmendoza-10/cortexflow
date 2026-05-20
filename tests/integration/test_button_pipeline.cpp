// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// End-to-end scenarios against the button_pipeline example
// (examples/button_pipeline/). This file grows scenario-by-scenario across
// the slice chain in .scratch/button-pipeline-example/issues/. Initial
// slice: a single round-trip lifecycle test on the scaffolded composition
// (ButtonReader only).
//
// Every scenario drives the App via the public surface only (post +
// run_one + ManualClock::advance). No internal pokes; no real time; no
// real I/O.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/clock.hpp>

#include <app.hpp>
#include <modules/button_reader.hpp>

// ---------------------------------------------------------------------------
// Compile-time composition shape — pins the scaffolded shape so silent
// composition drift fails at compile time. Later slices update these as
// modules and cache keys land:
//   - slice 02: kNumModules == 2, Keys::size == 1
//   - slice 03: kNumModules == 4, Keys::size == 2
// ---------------------------------------------------------------------------

static_assert(button_pipeline::Runtime::kNumModules == 1,
              "button_pipeline scaffold: ModuleList<ButtonReader> only");
static_assert(button_pipeline::Keys::size == 0,
              "button_pipeline scaffold: CacheKeyList<> (empty)");
static_assert(button_pipeline::Runtime::kMaxSubscriptions == 8,
              "button_pipeline scaffold: MaxSubscriptions<8>");

// ---------------------------------------------------------------------------
// Test 1 — App lifecycle round-trip on the scaffolded composition.
//
// ButtonReader has no on_start override and no Flow, so nothing is posted
// during start(). The queue is empty; run_one() returns immediately;
// shutdown() tears the runtime down without asserting. This pins that the
// scaffolding compiles, links, and exercises every lifecycle phase before
// any behavior lands in later slices.
// ---------------------------------------------------------------------------

TEST_CASE("button_pipeline scaffold: start/run_one/shutdown round-trip") {
    cortexflow::ManualClock clk;
    button_pipeline::App app{clk};
    app.start();

    CHECK(app.queue_size() == 0);
    CHECK(app.cache_ref().subscriber_count() == 0);
    CHECK(app.timers_ref().armed_count() == 0);

    app.run_one();  // no envelopes to drain

    CHECK(app.queue_size() == 0);

    app.shutdown();
}
