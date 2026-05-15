// End-to-end scenarios against the minimal example app
// (examples/minimal_app/). This test file is the integration fixture called
// for in PRD §Testing — "A minimal composed system (also used as
// examples/minimal_app) is the integration fixture: two modules, one cache
// key, one flow with two states, one timer."
//
// Every scenario drives the app via the public surface only (post + run_one
// + ManualClock::advance). No internal pokes; no real time; no real I/O.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <chrono>

#include <cortexflow/cache.hpp>
#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>

#include <app.hpp>
#include <keys.hpp>
#include <modules/consumer.hpp>
#include <modules/producer.hpp>

using namespace std::chrono_literals;

namespace {

// Helper: drain one cycle of the loop. The shape of a single cycle is:
//   1. run_one() processes any pending Bump → cache.set → KeyChanged →
//      Consumer transitions Idle→Processing (arms its timer);
//   2. advance(kProcessingDelay) fires the timer → ProcessingTick posted;
//   3. run_one() dispatches ProcessingTick → Consumer sends Done →
//      Processing transitions back to Idle (re-subscribes); the Done is
//      consumed by Producer which re-posts a Bump → cache.set → KeyChanged
//      → Consumer transitions Idle→Processing again. The next cycle's
//      timer is now armed.
// Calling this helper N times therefore lands the system in Processing each
// time with `counter == N+1` and `acks == N` (the first transition to
// Processing happens during start-driven dispatch, before the first cycle).
void run_cycle(minimal_app::App& app, cortexflow::ManualClock& clk) {
    clk.advance(minimal_app::kProcessingDelay);
    app.run_one();
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1 — full composition wakes up correctly: on_start seeds a Bump, the
// cache fans out, Consumer transitions Idle→Processing and arms its timer.
// ---------------------------------------------------------------------------

TEST_CASE("minimal_app: start seeds a Bump that drives the first transition") {
    cortexflow::ManualClock clk;
    minimal_app::App app{clk};
    app.start();

    // Before any run_one(): the queue holds the Bump that Producer's
    // on_start posted. Consumer's flow has dispatched its synthetic init
    // envelope into Idle (which is now active with a live Subscription) but
    // has not yet seen any user message.
    CHECK(app.queue_size() == 1);
    CHECK(app.get<minimal_app::Producer>().counter() == 0);
    CHECK(app.get<minimal_app::Producer>().acks() == 0);
    CHECK(app.cache_ref().get<minimal_app::Counter>().has_value() == false);
    CHECK(app.cache_ref().subscriber_count() == 1);  // Idle's subscription
    CHECK(app.timers_ref().armed_count() == 0);

    app.run_one();

    // After the first drain:
    //   - Producer saw Bump → counter == 1, cache.set<Counter>(1) → fanout
    //     enqueued KeyChanged<Counter> for Consumer;
    //   - Consumer's Idle handler saw KeyChanged → transition to Processing
    //     → Processing locals constructed → Timer armed.
    CHECK(app.get<minimal_app::Producer>().counter() == 1);
    CHECK(app.cache_ref().get<minimal_app::Counter>().value_or(-1) == 1);
    CHECK(app.timers_ref().armed_count() == 1);
    // Idle's subscription has been released; Processing has no subscription
    // of its own (the cache pool is empty during Processing).
    CHECK(app.cache_ref().subscriber_count() == 0);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 2 — advancing the clock fires the Processing timer, which triggers
// the Consumer→Producer Done round-trip and re-seeds the next cycle.
// ---------------------------------------------------------------------------

TEST_CASE("minimal_app: clock advance fires the timer, drives one full cycle") {
    cortexflow::ManualClock clk;
    minimal_app::App app{clk};
    app.start();
    app.run_one();  // first cycle: arms the Processing timer

    CHECK(app.timers_ref().armed_count() == 1);

    // Advance below the timer's due time: nothing fires.
    clk.advance(minimal_app::kProcessingDelay - 1ms);
    CHECK(app.queue_size() == 0);
    CHECK(app.timers_ref().armed_count() == 1);

    // Cross the due time: timer fires, ProcessingTick is posted.
    clk.advance(1ms);
    CHECK(app.queue_size() == 1);
    CHECK(app.timers_ref().armed_count() == 0);

    app.run_one();

    // After the second drain the cycle has completed end-to-end and the
    // system has already begun the next one:
    //   - Consumer.handle saw ProcessingTick → send Done to Producer →
    //     flow.step routed ProcessingTick into Processing → transition back
    //     to Idle (Subscription reconstructed);
    //   - Producer.on(Done) → ack++, send Bump → on(Bump) → counter++,
    //     cache.set<Counter>(2) → fanout enqueues KeyChanged;
    //   - Consumer.Idle saw KeyChanged → transitioned to Processing,
    //     arming the next Timer.
    CHECK(app.get<minimal_app::Producer>().counter() == 2);
    CHECK(app.get<minimal_app::Producer>().acks() == 1);
    CHECK(app.cache_ref().get<minimal_app::Counter>().value_or(-1) == 2);
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.cache_ref().subscriber_count() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 3 — many cycles preserve the Bump↔Done invariant. After N advances
// + drains the counter is N+1 and acks is N; the Processing timer is armed
// exactly once; the cache holds the most recent Counter value.
// ---------------------------------------------------------------------------

TEST_CASE("minimal_app: repeated cycles keep counter == acks + 1 invariant") {
    cortexflow::ManualClock clk;
    minimal_app::App app{clk};
    app.start();
    app.run_one();  // initial Idle→Processing transition

    constexpr int kCycles = 10;
    for (int i = 0; i < kCycles; ++i) {
        run_cycle(app, clk);
    }

    auto& producer = app.get<minimal_app::Producer>();
    CHECK(producer.counter() == kCycles + 1);
    CHECK(producer.acks() == kCycles);
    CHECK(app.cache_ref().get<minimal_app::Counter>().value_or(-1) ==
          kCycles + 1);
    // Exactly one armed timer (the most recently entered Processing state).
    CHECK(app.timers_ref().armed_count() == 1);
    CHECK(app.queue_size() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 4 — state-local lifetime: each cycle entering Idle constructs a
// fresh Subscription, and each cycle entering Processing arms a fresh
// Timer. The number of live subscriptions/timers never grows past 1 — the
// proof that RAII is releasing them on transition.
// ---------------------------------------------------------------------------

TEST_CASE("minimal_app: state-locals do not leak subscriptions or timers") {
    cortexflow::ManualClock clk;
    minimal_app::App app{clk};
    app.start();

    // After app.start(): Idle is the active state with one Subscription;
    // no Bump has been processed yet so no Timer is armed.
    CHECK(app.cache_ref().subscriber_count() == 1);
    CHECK(app.timers_ref().armed_count() == 0);

    // 50 transitions: subscriber_count must oscillate 1 (Idle) → 0
    // (Processing) → 1 → 0 …, and armed_count must oscillate 0 → 1 → 0 …
    // We sample after each transition; the assertions below verify the
    // pool sizes never grow past 1.
    for (int i = 0; i < 50; ++i) {
        app.run_one();  // Bump → KeyChanged → Idle→Processing
        CHECK(app.cache_ref().subscriber_count() == 0);
        CHECK(app.timers_ref().armed_count() == 1);

        clk.advance(minimal_app::kProcessingDelay);
        // Tick fired but not yet dispatched: timer count drops; no new
        // subscription yet.
        CHECK(app.timers_ref().armed_count() == 0);
    }

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Test 5 — the composition matches the PRD shape exactly: ModuleList holds
// {Producer, Consumer}; CacheKeyList declares Counter Owned by Producer;
// MaxSubscriptions is 4. The asserts below are compile-time invariants and
// will fail at compile if anyone changes the composition without updating
// this test.
// ---------------------------------------------------------------------------

TEST_CASE("minimal_app: composition shape matches PRD §Composition shape") {
    static_assert(minimal_app::Runtime::kNumModules == 2,
                  "minimal_app must have exactly two modules");
    static_assert(minimal_app::Runtime::kMaxSubscriptions == 4,
                  "minimal_app composition declares MaxSubscriptions<4>");
    // CacheKeyList size = 1 — one cache key.
    static_assert(minimal_app::Keys::size == 1,
                  "minimal_app composition declares exactly one cache key");
    CHECK(true);
}
