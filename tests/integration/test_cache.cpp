// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/runtime.hpp>
#include <cortexflow/subscription.hpp>

#include <csetjmp>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Fault handler override.
//
// Default: abort on any fault — most tests do not exercise assertion paths
// and should fail loudly if an invariant trips.
//
// Capture mode: when `s_capture_fault` is set, the handler copies the reason
// into `s_fault_reason_buf` and `longjmp`s back to the test. Used by the pool
// overflow test, which intentionally trips `CORTEXFLOW_ASSERT`.
// ---------------------------------------------------------------------------

static bool s_capture_fault = false;
static std::jmp_buf s_fault_jump;
static bool s_fault_called = false;
static char s_fault_reason_buf[256];

extern "C" void platform_fault_handler(
    const char* /*file*/, int /*line*/, const char* reason) {
    if (s_capture_fault) {
        s_capture_fault = false;
        s_fault_called = true;
        if (reason) {
            std::strncpy(s_fault_reason_buf, reason,
                         sizeof(s_fault_reason_buf) - 1);
            s_fault_reason_buf[sizeof(s_fault_reason_buf) - 1] = '\0';
        } else {
            s_fault_reason_buf[0] = '\0';
        }
        std::longjmp(s_fault_jump, 1);
    }
    std::abort();
}

static void reset_fault_capture() {
    s_capture_fault = false;
    s_fault_called = false;
    s_fault_reason_buf[0] = '\0';
}

// ---------------------------------------------------------------------------
// Global fanout order log: each subscriber records the registration-order
// position of its receipt, allowing tests to assert FIFO ordering matches
// registration order.
// ---------------------------------------------------------------------------

static std::vector<int> s_fanout_order;

static void reset_fanout_log() {
    s_fanout_order.clear();
}

// ---------------------------------------------------------------------------
// Cache key types
// ---------------------------------------------------------------------------

struct Speed {
    using value_type = int;
};

struct Charging {
    using value_type = bool;
};

// ---------------------------------------------------------------------------
// Subscriber modules
// ---------------------------------------------------------------------------

template <int Id>
struct OrderedSubscriber : cortexflow::Module<OrderedSubscriber<Id>> {
    using Inbox = std::tuple<cortexflow::KeyChanged<Speed>>;

    int seen = 0;
    int last_value = 0;
    std::optional<int> last_old;

    void on(cortexflow::KeyChanged<Speed>& msg) {
        ++seen;
        last_value = msg.new_value;
        last_old = msg.old_value;
        s_fanout_order.push_back(Id);
    }
};

using SubA = OrderedSubscriber<0>;
using SubB = OrderedSubscriber<1>;
using SubC = OrderedSubscriber<2>;

struct ChargingWatcher : cortexflow::Module<ChargingWatcher> {
    using Inbox = std::tuple<cortexflow::KeyChanged<Charging>>;

    int seen = 0;
    bool last = false;

    void on(cortexflow::KeyChanged<Charging>& msg) {
        ++seen;
        last = msg.new_value;
    }
};

// Dummy module used as a target for synthetic subscriber IDs in tests that
// only care about pool accounting (not actual KeyChanged delivery).
struct DummyMod : cortexflow::Module<DummyMod> {
    using Inbox = std::tuple<cortexflow::KeyChanged<Speed>>;
    void on(cortexflow::KeyChanged<Speed>&) {}
};

// ---------------------------------------------------------------------------
// Apps
// ---------------------------------------------------------------------------

// Single-subscriber app, both Owned<K, M> and plain key forms covered.
using SingleSubApp = cortexflow::Runtime<
    cortexflow::ModuleList<SubA>,
    cortexflow::CacheKeyList<
        cortexflow::Owned<Speed, SubA>,
        Charging
    >,
    cortexflow::Config<cortexflow::MaxSubscriptions<8>>>;

using FanoutApp = cortexflow::Runtime<
    cortexflow::ModuleList<SubA, SubB, SubC>,
    cortexflow::CacheKeyList<cortexflow::Owned<Speed, SubA>>,
    cortexflow::Config<cortexflow::MaxSubscriptions<8>>>;

using TwoKeyApp = cortexflow::Runtime<
    cortexflow::ModuleList<SubA, ChargingWatcher>,
    cortexflow::CacheKeyList<
        cortexflow::Owned<Speed, SubA>,
        cortexflow::Owned<Charging, ChargingWatcher>
    >,
    cortexflow::Config<cortexflow::MaxSubscriptions<8>>>;

// Small-capacity app used to exercise overflow and RAII slot recycling.
using SmallPoolApp = cortexflow::Runtime<
    cortexflow::ModuleList<DummyMod>,
    cortexflow::CacheKeyList<cortexflow::Owned<Speed, DummyMod>>,
    cortexflow::Config<cortexflow::MaxSubscriptions<3>>>;

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

TEST_CASE("get<K> before any set returns empty optional") {
    SingleSubApp app;
    app.start();
    auto v = app.cache().get<Speed>();
    CHECK_FALSE(v.has_value());
    auto c = app.cache().get<Charging>();
    CHECK_FALSE(c.has_value());
    app.shutdown();
}

TEST_CASE("set<K>(v) stores value; get<K>() returns it") {
    SingleSubApp app;
    app.start();

    app.cache().set<Speed>(42);
    auto v = app.cache().get<Speed>();
    CHECK(v.has_value());
    CHECK(v.value_or(0) == 42);

    app.cache().set<Speed>(7);
    v = app.cache().get<Speed>();
    CHECK(v.has_value());
    CHECK(v.value_or(0) == 7);

    app.shutdown();
}

TEST_CASE("first set on an unset key fires a KeyChanged with empty old_value") {
    reset_fanout_log();
    SingleSubApp app;
    app.start();
    auto sub_a = app.cache().subscribe<Speed, SubA>();

    app.cache().set<Speed>(99);
    app.run_one();

    CHECK(app.get<SubA>().seen == 1);
    CHECK(app.get<SubA>().last_value == 99);
    CHECK_FALSE(app.get<SubA>().last_old.has_value());

    app.shutdown();
}

TEST_CASE("idempotent set does not fire KeyChanged") {
    reset_fanout_log();
    SingleSubApp app;
    app.start();
    auto sub_a = app.cache().subscribe<Speed, SubA>();

    app.cache().set<Speed>(5);
    app.run_one();
    CHECK(app.get<SubA>().seen == 1);

    // Same value — must be silent.
    CHECK(app.queue_size() == 0);
    app.cache().set<Speed>(5);
    CHECK(app.queue_size() == 0);

    app.run_one();
    CHECK(app.get<SubA>().seen == 1);

    // A genuine change fires again.
    app.cache().set<Speed>(6);
    CHECK(app.queue_size() == 1);
    app.run_one();
    CHECK(app.get<SubA>().seen == 2);
    CHECK(app.get<SubA>().last_value == 6);
    CHECK(app.get<SubA>().last_old.has_value());
    CHECK(*app.get<SubA>().last_old == 5);

    app.shutdown();
}

TEST_CASE("KeyChanged is delivered through the queue, never synchronously") {
    reset_fanout_log();
    SingleSubApp app;
    app.start();
    auto sub_a = app.cache().subscribe<Speed, SubA>();

    // set() must not invoke the handler before returning.
    app.cache().set<Speed>(1);
    CHECK(app.get<SubA>().seen == 0);
    CHECK(app.queue_size() == 1);

    app.run_one();
    CHECK(app.get<SubA>().seen == 1);

    app.shutdown();
}

TEST_CASE("KeyChanged fanout order matches subscription registration order") {
    reset_fanout_log();
    FanoutApp app;
    app.start();

    // Register in C, A, B order to verify it is registration order — not
    // module declaration order — that determines fanout sequence.
    auto sub_c = app.cache().subscribe<Speed, SubC>();
    auto sub_a = app.cache().subscribe<Speed, SubA>();
    auto sub_b = app.cache().subscribe<Speed, SubB>();

    app.cache().set<Speed>(1);
    app.run_one();

    CHECK(s_fanout_order.size() == 3);
    if (s_fanout_order.size() == 3) {
        CHECK(s_fanout_order[0] == 2);  // SubC = Id 2
        CHECK(s_fanout_order[1] == 0);  // SubA = Id 0
        CHECK(s_fanout_order[2] == 1);  // SubB = Id 1
    }

    CHECK(app.get<SubA>().seen == 1);
    CHECK(app.get<SubB>().seen == 1);
    CHECK(app.get<SubC>().seen == 1);

    app.shutdown();
}

TEST_CASE("fanout targets only subscribers of the changed key") {
    reset_fanout_log();
    TwoKeyApp app;
    app.start();

    auto sub_a = app.cache().subscribe<Speed, SubA>();
    auto sub_charge = app.cache().subscribe<Charging, ChargingWatcher>();

    app.cache().set<Speed>(11);
    app.run_one();
    CHECK(app.get<SubA>().seen == 1);
    CHECK(app.get<ChargingWatcher>().seen == 0);

    app.cache().set<Charging>(true);
    app.run_one();
    CHECK(app.get<SubA>().seen == 1);
    CHECK(app.get<ChargingWatcher>().seen == 1);
    CHECK(app.get<ChargingWatcher>().last == true);

    app.shutdown();
}

TEST_CASE("multiple subscribers to one key all receive each change") {
    reset_fanout_log();
    FanoutApp app;
    app.start();
    auto sub_a = app.cache().subscribe<Speed, SubA>();
    auto sub_b = app.cache().subscribe<Speed, SubB>();
    auto sub_c = app.cache().subscribe<Speed, SubC>();

    app.cache().set<Speed>(10);
    app.cache().set<Speed>(20);  // distinct -> fires again
    app.run_one();

    CHECK(app.get<SubA>().seen == 2);
    CHECK(app.get<SubB>().seen == 2);
    CHECK(app.get<SubC>().seen == 2);
    CHECK(app.get<SubA>().last_value == 20);
    CHECK(app.get<SubB>().last_value == 20);
    CHECK(app.get<SubC>().last_value == 20);

    app.shutdown();
}

TEST_CASE("cache state resets between start and start (after shutdown)") {
    SingleSubApp app;
    app.start();
    app.cache().set<Speed>(123);
    CHECK(app.cache().get<Speed>().value_or(0) == 123);
    app.shutdown();

    app.start();
    CHECK_FALSE(app.cache().get<Speed>().has_value());
    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 10: RAII Subscription, pool overflow, subscribe-during-write
// ---------------------------------------------------------------------------

TEST_CASE("Subscription is move-only at the type level") {
    using cortexflow::Subscription;
    static_assert(!std::is_copy_constructible_v<Subscription>);
    static_assert(!std::is_copy_assignable_v<Subscription>);
    static_assert(std::is_nothrow_move_constructible_v<Subscription>);
    static_assert(std::is_nothrow_move_assignable_v<Subscription>);
    static_assert(std::is_nothrow_destructible_v<Subscription>);
    CHECK(true);  // assertions above are static; this CHECK keeps doctest happy
}

TEST_CASE("subscribe<K>(subscriber_id) returns a live Subscription handle") {
    SingleSubApp app;
    app.start();

    {
        auto sub = app.cache().subscribe<Speed>(
            cortexflow::type_id<SubA>());
        CHECK(sub.active());
        CHECK(app.cache().subscriber_count() == 1);
    }
    // Destructor released the slot synchronously.
    CHECK(app.cache().subscriber_count() == 0);

    app.shutdown();
}

TEST_CASE("dropping a Subscription releases the slot synchronously") {
    SmallPoolApp app;
    app.start();

    {
        auto s1 = app.cache().subscribe<Speed, DummyMod>();
        auto s2 = app.cache().subscribe<Speed, DummyMod>();
        auto s3 = app.cache().subscribe<Speed, DummyMod>();
        CHECK(app.cache().subscriber_count() == 3);
    }
    CHECK(app.cache().subscriber_count() == 0);

    // The pool is now fully reusable.
    auto s1 = app.cache().subscribe<Speed, DummyMod>();
    auto s2 = app.cache().subscribe<Speed, DummyMod>();
    auto s3 = app.cache().subscribe<Speed, DummyMod>();
    CHECK(app.cache().subscriber_count() == 3);

    app.shutdown();
}

TEST_CASE("Subscription::reset releases the slot eagerly") {
    SmallPoolApp app;
    app.start();

    auto s = app.cache().subscribe<Speed, DummyMod>();
    CHECK(s.active());
    CHECK(app.cache().subscriber_count() == 1);

    s.reset();
    CHECK_FALSE(s.active());
    CHECK(app.cache().subscriber_count() == 0);

    // Reset is idempotent.
    s.reset();
    CHECK(app.cache().subscriber_count() == 0);

    app.shutdown();
}

TEST_CASE("moving a Subscription transfers ownership cleanly") {
    SmallPoolApp app;
    app.start();

    auto src = app.cache().subscribe<Speed, DummyMod>();
    CHECK(src.active());
    CHECK(app.cache().subscriber_count() == 1);

    // Move-construct.
    cortexflow::Subscription moved(std::move(src));
    CHECK_FALSE(src.active());
    CHECK(moved.active());
    CHECK(app.cache().subscriber_count() == 1);

    // Moved-from drop is a no-op.
    {
        cortexflow::Subscription drop_src(std::move(src));
    }
    CHECK(app.cache().subscriber_count() == 1);

    // Move-assign.
    cortexflow::Subscription dest;
    dest = std::move(moved);
    CHECK_FALSE(moved.active());
    CHECK(dest.active());
    CHECK(app.cache().subscriber_count() == 1);

    // Final drop releases the slot.
    dest.reset();
    CHECK(app.cache().subscriber_count() == 0);

    app.shutdown();
}

TEST_CASE("move-assigning to a live Subscription releases the prior slot") {
    SmallPoolApp app;
    app.start();

    auto a = app.cache().subscribe<Speed, DummyMod>();
    auto b = app.cache().subscribe<Speed, DummyMod>();
    CHECK(app.cache().subscriber_count() == 2);

    // Overwriting `a` with `b`'s subscription must release `a`'s slot first.
    a = std::move(b);
    CHECK(app.cache().subscriber_count() == 1);
    CHECK(a.active());
    CHECK_FALSE(b.active());

    app.shutdown();
}

TEST_CASE("RAII drop frees the slot for reuse and preserves fanout order") {
    reset_fanout_log();
    FanoutApp app;
    app.start();

    auto sub_a = app.cache().subscribe<Speed, SubA>();
    auto sub_b = app.cache().subscribe<Speed, SubB>();
    auto sub_c = app.cache().subscribe<Speed, SubC>();

    // Drop B; A and C remain, in their original registration order.
    sub_b.reset();
    CHECK(app.cache().subscriber_count() == 2);

    app.cache().set<Speed>(1);
    app.run_one();
    CHECK(s_fanout_order.size() == 2);
    if (s_fanout_order.size() == 2) {
        CHECK(s_fanout_order[0] == 0);   // SubA
        CHECK(s_fanout_order[1] == 2);   // SubC
    }

    // Re-subscribe B; it should now be last in registration order.
    auto sub_b2 = app.cache().subscribe<Speed, SubB>();
    CHECK(app.cache().subscriber_count() == 3);
    reset_fanout_log();

    app.cache().set<Speed>(2);
    app.run_one();
    CHECK(s_fanout_order.size() == 3);
    if (s_fanout_order.size() == 3) {
        CHECK(s_fanout_order[0] == 0);   // SubA
        CHECK(s_fanout_order[1] == 2);   // SubC
        CHECK(s_fanout_order[2] == 1);   // SubB (re-registered)
    }

    app.shutdown();
}

TEST_CASE("subscription-pool overflow asserts with capacity and key in reason") {
    SmallPoolApp app;
    app.start();
    constexpr std::size_t cap = SmallPoolApp::kMaxSubscriptions;
    static_assert(cap == 3, "test was authored against MaxSubscriptions<3>");

    std::vector<cortexflow::Subscription> live;
    for (std::size_t i = 0; i < cap; ++i) {
        live.push_back(app.cache().subscribe<Speed, DummyMod>());
    }
    CHECK(app.cache().subscriber_count() == cap);

    reset_fault_capture();
    s_capture_fault = true;
    if (setjmp(s_fault_jump) == 0) {
        // This call must fire CORTEXFLOW_ASSERT and longjmp back.
        auto overflow = app.cache().subscribe<Speed, DummyMod>();
        (void)overflow;
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason_buf, "overflow") != nullptr);
    CHECK(std::strstr(s_fault_reason_buf, "capacity=3") != nullptr);
    CHECK(std::strstr(s_fault_reason_buf, "Speed") != nullptr);

    // Pool accounting is intact; existing subs still occupy their slots.
    CHECK(app.cache().subscriber_count() == cap);

    // Drop one and re-subscribe to verify the pool remains usable.
    live.pop_back();
    CHECK(app.cache().subscriber_count() == cap - 1);
    auto refilled = app.cache().subscribe<Speed, DummyMod>();
    CHECK(app.cache().subscriber_count() == cap);

    live.clear();
    refilled.reset();
    app.shutdown();
}

TEST_CASE("subscribe-during-write: new subscriber does not see the in-flight write") {
    reset_fanout_log();
    FanoutApp app;
    app.start();

    auto sub_a = app.cache().subscribe<Speed, SubA>();

    // The write captures the snapshot of subscribers as of this point: only
    // SubA. KeyChanged<Speed> is enqueued for SubA; SubB does not exist yet
    // in the subscriber list.
    app.cache().set<Speed>(1);
    CHECK(app.queue_size() == 1);

    // A subscription created *after* set() (but before the queued envelope is
    // dispatched) does not observe the in-flight write — architecture §7.5.
    auto sub_b = app.cache().subscribe<Speed, SubB>();

    app.run_one();
    CHECK(app.get<SubA>().seen == 1);
    CHECK(app.get<SubA>().last_value == 1);
    CHECK(app.get<SubB>().seen == 0);

    // A subsequent change reaches both subscribers — SubB is now in the
    // snapshot.
    app.cache().set<Speed>(2);
    CHECK(app.queue_size() == 2);
    app.run_one();
    CHECK(app.get<SubA>().seen == 2);
    CHECK(app.get<SubA>().last_value == 2);
    CHECK(app.get<SubB>().seen == 1);
    CHECK(app.get<SubB>().last_value == 2);

    app.shutdown();
}
