// Platform typedef-swap integration test (issue 16 acceptance criteria).
//
// The same composition compiles unchanged for any `CORTEXFLOW_TARGET` because
// it names `platform::Allocator` / `platform::TimerBackend` / `platform::
// TraceSink` from `<cortexflow/platform.hpp>` instead of any target-specific
// type. Running this test under host and posix builds is the cross-target
// equivalence check ("same composition, sends/receives equivalently").
//
// IMPORTANT: this file must NOT contain `#ifdef`s referring to the target.
// The CMake target `no_target_ifdefs` greps for that pattern and fails the
// build if any sneak in (PRD US 44 — no `#ifdef` walls in module code).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/clock.hpp>
#include <cortexflow/platform.hpp>
#include <cortexflow/runtime.hpp>

#include <type_traits>

// ---------------------------------------------------------------------------
// Test messages and a two-module ping/pong composition.
// ---------------------------------------------------------------------------

struct PlatformPing { int seq; };
struct PlatformPong { int seq; };

struct Echo;

struct Sender : cortexflow::Module<Sender> {
    using Inbox = std::tuple<PlatformPong>;
    int pongs = 0;
    int last_seq = -1;
    void on(PlatformPong& m) { ++pongs; last_seq = m.seq; }
};

struct Echo : cortexflow::Module<Echo> {
    using Inbox = std::tuple<PlatformPing>;
    int pings = 0;
    void on(PlatformPing& m) { ++pings; send<Sender>(PlatformPong{m.seq}); }
};

using PlatformApp = cortexflow::Runtime<
    cortexflow::ModuleList<Sender, Echo>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Type-level: the platform typedefs resolve to a real `MessageAllocator`
// subclass and a real `Clock` subclass on every target. A composition that
// references these names compiles unchanged across host and posix builds.
// ---------------------------------------------------------------------------

TEST_CASE("platform::Allocator is a MessageAllocator subclass") {
    static_assert(std::is_base_of<cortexflow::MessageAllocator,
                                  cortexflow::platform::Allocator>::value,
                  "platform::Allocator must derive from MessageAllocator");
    CHECK(true);
}

TEST_CASE("platform::TimerBackend is a Clock subclass") {
    static_assert(std::is_base_of<cortexflow::Clock,
                                  cortexflow::platform::TimerBackend>::value,
                  "platform::TimerBackend must derive from Clock");
    CHECK(true);
}

TEST_CASE("platform::TraceSink is a complete type") {
    static_assert(std::is_class<cortexflow::platform::TraceSink>::value,
                  "platform::TraceSink must be a class type tag");
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Behavioural: same composition built for both targets must send/receive
// equivalently. This is the acceptance-criteria integration test.
// ---------------------------------------------------------------------------

TEST_CASE("platform::Allocator round-trips a message through the runtime") {
    PlatformApp app;
    app.start();

    auto& alloc = cortexflow::platform::Allocator::instance();
    auto payload =
        cortexflow::make_message_with<PlatformPing>(alloc, PlatformPing{7});
    cortexflow::Envelope env(cortexflow::kNoSender,
                             cortexflow::type_id<Echo>(),
                             std::move(payload));
    app.post(std::move(env));

    app.run_one();

    CHECK(app.get<Echo>().pings == 1);
    CHECK(app.get<Sender>().pongs == 1);
    CHECK(app.get<Sender>().last_seq == 7);

    app.shutdown();
}

TEST_CASE("platform::TimerBackend (SteadyClock) produces monotonic times") {
    cortexflow::platform::TimerBackend clock;
    auto a = clock.now();
    auto b = clock.now();
    // Strict monotonicity is not guaranteed (the clock may return the same
    // value on consecutive reads under fast loops). Architecture §9.3 only
    // promises "monotonic — consecutive reads never decrease."
    CHECK(b >= a);
}

TEST_CASE("default_allocator() returns the platform allocator instance") {
    auto& generic = cortexflow::default_allocator();
    auto& platform = cortexflow::platform::Allocator::instance();
    CHECK(&generic == &platform);
}
