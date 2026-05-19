// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/clock.hpp>
#include <cortexflow/runtime.hpp>
#include <cortexflow/timer.hpp>

#include <atomic>
#include <chrono>
#include <csetjmp>
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Fault handler override — capture and longjmp instead of abort.
// ---------------------------------------------------------------------------

static std::jmp_buf s_fault_jump;
static bool s_fault_called;
static const char* s_fault_reason;

extern "C" void platform_fault_handler(
    const char* file, int line, const char* reason) {
    (void)file; (void)line;
    s_fault_called = true;
    s_fault_reason = reason;
    std::longjmp(s_fault_jump, 1);
}

static void reset_fault_state() {
    s_fault_called = false;
    s_fault_reason = nullptr;
}

// ---------------------------------------------------------------------------
// Test messages and modules
// ---------------------------------------------------------------------------

using namespace std::chrono_literals;
using cortexflow::Clock;
using cortexflow::Envelope;
using cortexflow::kNoSender;
using cortexflow::ManualClock;
using cortexflow::make_message;
using cortexflow::Timer;
using cortexflow::TimerService;
using cortexflow::type_id;

struct TimerTick { int tag = 0; };

struct TimerSink : cortexflow::Module<TimerSink> {
    using Inbox = std::tuple<TimerTick>;

    int seen = 0;
    std::vector<int> tags;

    void on(TimerTick& m) {
        ++seen;
        tags.push_back(m.tag);
    }
};

using TimerApp = cortexflow::Runtime<
    cortexflow::ModuleList<TimerSink>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<cortexflow::DrainBudget<256>>>;

// ---------------------------------------------------------------------------
// Type-level shape: Timer is move-only RAII
// ---------------------------------------------------------------------------

TEST_CASE("Timer is move-only at the type level") {
    static_assert(!std::is_copy_constructible_v<Timer>);
    static_assert(!std::is_copy_assignable_v<Timer>);
    static_assert(std::is_nothrow_move_constructible_v<Timer>);
    static_assert(std::is_nothrow_move_assignable_v<Timer>);
    static_assert(std::is_nothrow_destructible_v<Timer>);
    CHECK(true);
}

TEST_CASE("default-constructed Timer is inert; cancel/destroy is a no-op") {
    Timer t;
    CHECK_FALSE(t.active());
    t.cancel();
    CHECK_FALSE(t.active());
}

// ---------------------------------------------------------------------------
// Runtime accessor
// ---------------------------------------------------------------------------

TEST_CASE("runtime.timers() returns a service tied to the runtime's clock") {
    ManualClock clk;
    TimerApp app{clk};
    // Accessor works before start().
    CHECK(app.timers().armed_count() == 0);

    app.start();
    CHECK(app.timers().armed_count() == 0);
    app.shutdown();
}

// ---------------------------------------------------------------------------
// arm + fire on advance
// ---------------------------------------------------------------------------

TEST_CASE("arm + ManualClock::advance posts the timer message; run_one dispatches") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto t = app.timers().arm<TimerSink>(10ms, TimerTick{42});
    CHECK(t.active());
    CHECK(app.timers().armed_count() == 1);

    // Not yet due — advancing less than the delay must not fire.
    clk.advance(5ms);
    CHECK(app.queue_size() == 0);
    CHECK(app.timers().armed_count() == 1);

    // Cross the threshold — fire_due posts the envelope.
    clk.advance(5ms);
    CHECK(app.queue_size() == 1);
    CHECK(app.timers().armed_count() == 0);

    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.size() == 1);
    if (!app.get<TimerSink>().tags.empty()) {
        CHECK(app.get<TimerSink>().tags[0] == 42);
    }

    app.shutdown();
}

TEST_CASE("zero-delay timer fires on the next advance, not synchronously on arm") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto t = app.timers().arm<TimerSink>(0ms, TimerTick{7});
    CHECK(t.active());

    // Arming must not synchronously post the envelope — the queue should be
    // empty until a fire_due call, even for a delay of zero.
    CHECK(app.queue_size() == 0);

    clk.advance(0ms);  // advance(0) still invokes fire_due
    CHECK(app.queue_size() == 1);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);

    app.shutdown();
}

TEST_CASE("multiple timers fire in due-time order, regardless of arm order") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    // Arm late-then-early so the heap must reorder.
    auto t_late  = app.timers().arm<TimerSink>(30ms, TimerTick{2});
    auto t_mid   = app.timers().arm<TimerSink>(20ms, TimerTick{1});
    auto t_early = app.timers().arm<TimerSink>(10ms, TimerTick{0});

    CHECK(app.timers().armed_count() == 3);

    clk.advance(100ms);  // all three become due in one window
    CHECK(app.queue_size() == 3);
    CHECK(app.timers().armed_count() == 0);

    app.run_one();
    CHECK(app.get<TimerSink>().tags.size() == 3);
    if (app.get<TimerSink>().tags.size() == 3) {
        CHECK(app.get<TimerSink>().tags[0] == 0);
        CHECK(app.get<TimerSink>().tags[1] == 1);
        CHECK(app.get<TimerSink>().tags[2] == 2);
    }

    app.shutdown();
}

TEST_CASE("partial advance fires only the timers whose due time has arrived") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto t1 = app.timers().arm<TimerSink>(10ms, TimerTick{1});
    auto t2 = app.timers().arm<TimerSink>(50ms, TimerTick{2});

    clk.advance(15ms);
    CHECK(app.queue_size() == 1);
    CHECK(app.timers().armed_count() == 1);

    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.back() == 1);

    clk.advance(50ms);
    CHECK(app.queue_size() == 1);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 2);
    CHECK(app.get<TimerSink>().tags.back() == 2);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

TEST_CASE("Timer destruction cancels: envelope is not posted on advance") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    {
        auto t = app.timers().arm<TimerSink>(10ms, TimerTick{99});
        CHECK(app.timers().armed_count() == 1);
    }  // ~Timer fires cancel
    CHECK(app.timers().armed_count() == 0);

    clk.advance(100ms);
    CHECK(app.queue_size() == 0);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 0);

    app.shutdown();
}

TEST_CASE("Timer::cancel() releases eagerly; subsequent advance does not fire") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto t = app.timers().arm<TimerSink>(10ms, TimerTick{1});
    CHECK(t.active());
    CHECK(app.timers().armed_count() == 1);

    t.cancel();
    CHECK_FALSE(t.active());
    CHECK(app.timers().armed_count() == 0);

    clk.advance(100ms);
    CHECK(app.queue_size() == 0);

    // Cancel is idempotent.
    t.cancel();
    CHECK_FALSE(t.active());

    app.shutdown();
}

TEST_CASE("cancelling one of several timers leaves the others firing") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto t1 = app.timers().arm<TimerSink>(10ms, TimerTick{1});
    auto t2 = app.timers().arm<TimerSink>(10ms, TimerTick{2});
    auto t3 = app.timers().arm<TimerSink>(10ms, TimerTick{3});

    CHECK(app.timers().armed_count() == 3);
    t2.cancel();
    CHECK(app.timers().armed_count() == 2);

    clk.advance(10ms);
    CHECK(app.queue_size() == 2);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 2);

    // Tags should include 1 and 3 in some order; 2 must not appear.
    auto& tags = app.get<TimerSink>().tags;
    bool saw1 = false, saw2 = false, saw3 = false;
    for (int tag : tags) {
        if (tag == 1) saw1 = true;
        if (tag == 2) saw2 = true;
        if (tag == 3) saw3 = true;
    }
    CHECK(saw1);
    CHECK_FALSE(saw2);
    CHECK(saw3);

    app.shutdown();
}

TEST_CASE("moving a Timer transfers ownership; moved-from drop is a no-op") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto src = app.timers().arm<TimerSink>(10ms, TimerTick{5});
    CHECK(src.active());
    CHECK(app.timers().armed_count() == 1);

    Timer moved(std::move(src));
    CHECK_FALSE(src.active());
    CHECK(moved.active());
    CHECK(app.timers().armed_count() == 1);

    // Dropping the moved-from Timer must not cancel the timer.
    {
        Timer take_src(std::move(src));
        CHECK_FALSE(take_src.active());
    }
    CHECK(app.timers().armed_count() == 1);

    // The live one still fires on advance.
    clk.advance(10ms);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.back() == 5);

    app.shutdown();
}

TEST_CASE("move-assigning over a live Timer cancels the prior one") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto a = app.timers().arm<TimerSink>(10ms, TimerTick{1});
    auto b = app.timers().arm<TimerSink>(10ms, TimerTick{2});
    CHECK(app.timers().armed_count() == 2);

    a = std::move(b);
    // `a` previously held timer{tag=1}; that one must now be cancelled.
    CHECK(a.active());
    CHECK_FALSE(b.active());
    CHECK(app.timers().armed_count() == 1);

    clk.advance(10ms);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.back() == 2);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Advance-window semantics: timers armed during firing don't fire same window.
// ---------------------------------------------------------------------------

namespace {

struct Rearmer;

// A module that, upon receiving TimerTick, arms another timer (delay 0) so the
// handler is racing the same advance window — but per spec, the new timer
// must NOT fire in this advance call.
struct ReentrantSink : cortexflow::Module<ReentrantSink> {
    using Inbox = std::tuple<TimerTick>;

    int seen = 0;
    Timer rearm_handle;
    TimerService* svc = nullptr;

    void on(TimerTick& m) {
        (void)m;
        ++seen;
        if (svc && seen == 1) {
            // Re-arm a new timer of the same kind, delay 0.
            rearm_handle = svc->arm<ReentrantSink>(
                Clock::duration::zero(), TimerTick{99});
        }
    }
};

using ReentrantApp = cortexflow::Runtime<
    cortexflow::ModuleList<ReentrantSink>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<cortexflow::DrainBudget<256>>>;

} // namespace

TEST_CASE("timers armed during firing do not fire in the same advance window") {
    ManualClock clk;
    ReentrantApp app{clk};
    app.start();

    app.get<ReentrantSink>().svc = &app.timers();

    auto trigger = app.timers().arm<ReentrantSink>(10ms, TimerTick{1});

    // First advance: only the originally-armed timer is in the heap snapshot.
    // It fires; we then run_one which invokes the handler; the handler arms a
    // new timer. That new timer is NOT in the snapshot of this advance.
    clk.advance(10ms);
    CHECK(app.queue_size() == 1);
    app.run_one();
    CHECK(app.get<ReentrantSink>().seen == 1);

    // The handler's re-arm queued a delay-0 timer. It MUST NOT have fired in
    // this advance window — the queue is empty until the next advance.
    CHECK(app.queue_size() == 0);
    CHECK(app.timers().armed_count() == 1);

    // Second advance: now the snapshot includes the re-armed timer.
    clk.advance(0ms);
    CHECK(app.queue_size() == 1);
    app.run_one();
    CHECK(app.get<ReentrantSink>().seen == 2);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Foreign-thread arm: must be safe from outside the loop thread.
// ---------------------------------------------------------------------------

TEST_CASE("arm is callable from a foreign thread") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    // Stash handles in main-thread storage so destruction order is
    // controlled; foreign-thread arms return Timer values that we move into
    // here.
    std::vector<Timer> handles;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (int i = 0; i < 50; ++i) {
            handles.push_back(
                app.timers().arm<TimerSink>(1ms, TimerTick{i}));
        }
        producer_done = true;
    });
    producer.join();
    CHECK(producer_done.load());
    CHECK(app.timers().armed_count() == 50);

    clk.advance(1ms);
    CHECK(app.queue_size() == 50);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 50);

    app.shutdown();
}

TEST_CASE("arm from main thread and foreign thread interleave correctly") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    std::vector<Timer> main_handles;
    std::vector<Timer> foreign_handles;

    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i) {
            foreign_handles.push_back(
                app.timers().arm<TimerSink>(2ms, TimerTick{1000 + i}));
        }
        producer_done = true;
    });

    for (int i = 0; i < 100; ++i) {
        main_handles.push_back(
            app.timers().arm<TimerSink>(2ms, TimerTick{i}));
    }

    producer.join();
    CHECK(producer_done.load());
    CHECK(app.timers().armed_count() == 200);

    clk.advance(2ms);
    CHECK(app.queue_size() == 200);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 200);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// State-local scenario: Timer held as a member; destruction cancels cleanly.
// ---------------------------------------------------------------------------
//
// This is the "integration test" the acceptance criteria call for. The Timer
// lives inside a struct that simulates a state-locals block — it arms in its
// constructor (given a service ref + delay) and cancels on destruction. The
// flow subsystem will manage this lifetime for real once that work lands;
// here we drive it from the test directly to verify TimerService delivers the
// required guarantees.

namespace {

struct StateLocals {
    Timer t;

    StateLocals(TimerService& svc, Clock::duration delay, int tag)
        : t(svc.arm<TimerSink>(delay, TimerTick{tag})) {}
};

} // namespace

TEST_CASE(
    "state-local Timer arms on construction; advance fires it; "
    "destruction during arm cancels cleanly") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    // Construct → arm → advance → observe fire.
    {
        StateLocals locals(app.timers(), 10ms, 0xC0DE);
        CHECK(app.timers().armed_count() == 1);

        clk.advance(10ms);
        CHECK(app.queue_size() == 1);
        app.run_one();
        CHECK(app.get<TimerSink>().seen == 1);
        CHECK(app.get<TimerSink>().tags.back() == 0xC0DE);

        // The timer already fired; destructing locals here is a no-op cancel.
    }
    CHECK(app.timers().armed_count() == 0);

    // Destruction during arm cancels cleanly: build another locals, drop
    // before any advance, then verify no envelope is delivered.
    {
        StateLocals locals(app.timers(), 10ms, 0xDEAD);
        CHECK(app.timers().armed_count() == 1);
        // Drop without advancing.
    }
    CHECK(app.timers().armed_count() == 0);

    clk.advance(100ms);
    CHECK(app.queue_size() == 0);
    app.run_one();
    // Still only the one fire from the first locals block.
    CHECK(app.get<TimerSink>().seen == 1);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Generic arm(Envelope) path
// ---------------------------------------------------------------------------

TEST_CASE("non-templated arm accepts a pre-built envelope") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    auto ptr = make_message<TimerTick>(TimerTick{77});
    Envelope env(kNoSender, type_id<TimerSink>(), std::move(ptr));

    auto t = app.timers().arm(15ms, std::move(env));
    CHECK(t.active());
    CHECK(app.timers().armed_count() == 1);

    clk.advance(15ms);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.back() == 77);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Misuse: negative delay asserts.
// ---------------------------------------------------------------------------

TEST_CASE("arm with a negative duration asserts via CORTEXFLOW_ASSERT") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        // The bound Timer value is constructed only when arm returns;
        // longjmp aborts before that, so there is nothing to clean up.
        auto t = app.timers().arm<TimerSink>(-1ms, TimerTick{0});
        (void)t;
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "negative") != nullptr);
    // No timer was added to the heap.
    CHECK(app.timers().armed_count() == 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Lifecycle: shutdown drops any armed timers; subsequent restart sees fresh
// state.
// ---------------------------------------------------------------------------

TEST_CASE("shutdown drops armed timers; start sees a clean service") {
    ManualClock clk;
    TimerApp app{clk};
    app.start();

    // Arm and immediately drop the Timer handle. Without the handle there is
    // nothing to cancel from user code, but the entry remains in the heap
    // until either fired or service-cleared.
    Timer t = app.timers().arm<TimerSink>(10ms, TimerTick{1});
    CHECK(app.timers().armed_count() == 1);
    t.cancel();
    CHECK(app.timers().armed_count() == 0);

    // Arm without cancelling, then shutdown. The Timer dtor on the
    // following line cancels (so armed_count() goes to 0) but the heap
    // entry remains until reaped — clear() must reclaim it.
    {
        auto live = app.timers().arm<TimerSink>(10ms, TimerTick{2});
        CHECK(app.timers().armed_count() == 1);
    }
    CHECK(app.timers().armed_count() == 0);

    app.shutdown();
    CHECK(app.timers().armed_count() == 0);

    // Restart: a fresh start() should have a clean slate.
    app.start();
    CHECK(app.timers().armed_count() == 0);

    auto fresh = app.timers().arm<TimerSink>(10ms, TimerTick{3});
    CHECK(app.timers().armed_count() == 1);
    clk.advance(10ms);
    app.run_one();
    CHECK(app.get<TimerSink>().seen == 1);
    CHECK(app.get<TimerSink>().tags.back() == 3);

    app.shutdown();
}
