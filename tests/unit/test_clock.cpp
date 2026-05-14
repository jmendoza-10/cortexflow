#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/clock.hpp>
#include <cortexflow/runtime.hpp>

#include <chrono>
#include <csetjmp>
#include <cstring>
#include <thread>

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
// ManualClock determinism
// ---------------------------------------------------------------------------

using namespace std::chrono_literals;
using cortexflow::Clock;
using cortexflow::ManualClock;
using cortexflow::SteadyClock;

TEST_CASE("ManualClock starts at zero") {
    ManualClock clk;
    CHECK(clk.now() == Clock::duration::zero());
}

TEST_CASE("ManualClock::now() is deterministic across reads without advance") {
    ManualClock clk;
    auto a = clk.now();
    auto b = clk.now();
    auto c = clk.now();
    CHECK(a == b);
    CHECK(b == c);

    clk.advance(1ms);
    auto d = clk.now();
    auto e = clk.now();
    CHECK(d == e);
    CHECK(d == a + 1ms);
}

TEST_CASE("ManualClock::advance(positive) moves now() forward") {
    ManualClock clk;
    clk.advance(10ms);
    CHECK(clk.now() == Clock::duration(10ms));
    clk.advance(20ms);
    CHECK(clk.now() == Clock::duration(30ms));
    clk.advance(Clock::duration::zero());
    CHECK(clk.now() == Clock::duration(30ms));
}

TEST_CASE("ManualClock honours initial offset") {
    ManualClock clk{500ms};
    CHECK(clk.now() == Clock::duration(500ms));
    clk.advance(1s);
    CHECK(clk.now() == Clock::duration(1500ms));
}

TEST_CASE("ManualClock::advance(negative) asserts via CORTEXFLOW_ASSERT") {
    ManualClock clk;
    clk.advance(5ms);
    auto before = clk.now();

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        clk.advance(-1ms);
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "negative") != nullptr);
    // The clock must not have moved backward despite the failed call.
    CHECK(clk.now() == before);
}

// ---------------------------------------------------------------------------
// SteadyClock monotonicity
// ---------------------------------------------------------------------------

TEST_CASE("SteadyClock::now() is non-decreasing across consecutive reads") {
    SteadyClock clk;
    constexpr int kSamples = 10000;
    auto previous = clk.now();
    for (int i = 0; i < kSamples; ++i) {
        auto current = clk.now();
        CHECK(current >= previous);
        previous = current;
    }
}

TEST_CASE("SteadyClock::now() advances across a sleep") {
    SteadyClock clk;
    auto before = clk.now();
    std::this_thread::sleep_for(2ms);
    auto after = clk.now();
    CHECK(after > before);
    CHECK(after - before >= 1ms);
}

// ---------------------------------------------------------------------------
// Runtime injection: a clock reference passed at construction is observable
// via runtime.clock() and stays bound across start()/shutdown().
// ---------------------------------------------------------------------------

struct DummyModule : cortexflow::Module<DummyModule> {
    using Inbox = std::tuple<>;
};

using DummyApp = cortexflow::Runtime<
    cortexflow::ModuleList<DummyModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("Runtime(Clock&) stores the injected clock and exposes it via clock()") {
    ManualClock clk;
    DummyApp app{clk};
    CHECK(&app.clock() == static_cast<Clock*>(&clk));

    clk.advance(123ms);
    CHECK(app.clock().now() == Clock::duration(123ms));

    app.start();
    CHECK(app.clock().now() == Clock::duration(123ms));
    clk.advance(7ms);
    CHECK(app.clock().now() == Clock::duration(130ms));
    app.shutdown();

    // After shutdown the clock reference remains valid; subsystems built in
    // a future test composition would still see the same clock.
    CHECK(app.clock().now() == Clock::duration(130ms));
}

TEST_CASE("Runtime() default-constructs with a SteadyClock") {
    DummyApp app;
    auto a = app.clock().now();
    std::this_thread::sleep_for(1ms);
    auto b = app.clock().now();
    CHECK(b >= a);
}
