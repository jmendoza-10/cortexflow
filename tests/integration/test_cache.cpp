#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/runtime.hpp>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Fault handler override — not strictly needed for these tests (no asserts
// are expected to fire on the happy paths) but provided for symmetry with
// other integration suites in case a regression trips an assertion.
// ---------------------------------------------------------------------------

extern "C" void platform_fault_handler(
    const char* /*file*/, int /*line*/, const char* /*reason*/) {
    // Abort by default if a regression accidentally fires an assertion.
    std::abort();
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
    app.cache().subscribe<Speed, SubA>();

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
    app.cache().subscribe<Speed, SubA>();

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
    app.cache().subscribe<Speed, SubA>();

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
    app.cache().subscribe<Speed, SubC>();
    app.cache().subscribe<Speed, SubA>();
    app.cache().subscribe<Speed, SubB>();

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

    app.cache().subscribe<Speed, SubA>();
    app.cache().subscribe<Charging, ChargingWatcher>();

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
    app.cache().subscribe<Speed, SubA>();
    app.cache().subscribe<Speed, SubB>();
    app.cache().subscribe<Speed, SubC>();

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
