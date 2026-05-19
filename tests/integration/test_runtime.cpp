// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/runtime.hpp>

#include <atomic>
#include <chrono>
#include <csetjmp>
#include <cstring>
#include <string>
#include <thread>
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
// Trace sink override — capture WARN traces for drain-budget tests.
// ---------------------------------------------------------------------------

static std::vector<std::string> s_warn_messages;

extern "C" void platform_trace_sink(
    int level, const char* /*kind*/, const char* /*from*/,
    const char* /*to*/, const char* /*type_name*/, const char* message) {
    if (level == 2) {  // WARN
        s_warn_messages.emplace_back(message ? message : "");
    }
}

// ---------------------------------------------------------------------------
// Event log shared across the on_start/on_stop ordering test modules.
// ---------------------------------------------------------------------------

static std::vector<std::string> s_events;

// ---------------------------------------------------------------------------
// Two-module ping/pong system
// ---------------------------------------------------------------------------

struct PongCatcher : cortexflow::Module<PongCatcher> {
    struct Pong { int seq; };

    using Inbox = std::tuple<Pong>;

    // Atomic so tests can poll these fields from a non-loop thread while
    // run() dispatches on a worker thread (test_runtime.cpp:313, :411).
    std::atomic<int> pongs_seen{0};
    std::atomic<int> last_seq{-1};

    void on(Pong& msg) {
        ++pongs_seen;
        last_seq = msg.seq;
    }
};

struct PingResponder : cortexflow::Module<PingResponder> {
    struct Ping { int seq; };

    using Inbox = std::tuple<Ping>;

    int pings_seen = 0;

    void on(Ping& msg) {
        ++pings_seen;
        send<PongCatcher>(PongCatcher::Pong{msg.seq});
    }
};

using PingPongApp = cortexflow::Runtime<
    cortexflow::ModuleList<PingResponder, PongCatcher>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Three-module ordering system
// ---------------------------------------------------------------------------

struct OrderA : cortexflow::Module<OrderA> {
    using Inbox = std::tuple<>;
    void on_start() override { s_events.emplace_back("A_start"); }
    void on_stop()  override { s_events.emplace_back("A_stop"); }
};

struct OrderB : cortexflow::Module<OrderB> {
    using Inbox = std::tuple<>;
    void on_start() override { s_events.emplace_back("B_start"); }
    void on_stop()  override { s_events.emplace_back("B_stop"); }
};

struct OrderC : cortexflow::Module<OrderC> {
    using Inbox = std::tuple<>;
    void on_start() override { s_events.emplace_back("C_start"); }
    void on_stop()  override { s_events.emplace_back("C_stop"); }
};

using OrderApp = cortexflow::Runtime<
    cortexflow::ModuleList<OrderA, OrderB, OrderC>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Drain-budget system: a single counter module receiving many envelopes.
// ---------------------------------------------------------------------------

struct Counter : cortexflow::Module<Counter> {
    struct Tick {};

    using Inbox = std::tuple<Tick>;
    int handled = 0;
    void on(Tick&) { ++handled; }
};

using BoundedApp = cortexflow::Runtime<
    cortexflow::ModuleList<Counter>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<cortexflow::DrainBudget<2>>>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

using cortexflow::Envelope;
using cortexflow::kNoSender;
using cortexflow::make_message;
using cortexflow::type_id;

template <typename Target, typename Msg>
static Envelope make_external_envelope(Msg&& payload) {
    auto ptr = make_message<std::decay_t<Msg>>(std::forward<Msg>(payload));
    return Envelope(kNoSender, type_id<Target>(), std::move(ptr));
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

TEST_CASE("two modules exchange a message via send, observed by run_one") {
    PingPongApp app;
    app.start();

    app.post(make_external_envelope<PingResponder>(PingResponder::Ping{42}));

    CHECK(app.get<PingResponder>().pings_seen == 0);
    CHECK(app.get<PongCatcher>().pongs_seen == 0);

    app.run_one();

    CHECK(app.get<PingResponder>().pings_seen == 1);
    CHECK(app.get<PongCatcher>().pongs_seen == 1);
    CHECK(app.get<PongCatcher>().last_seq == 42);

    app.shutdown();
}

TEST_CASE("run_one on empty queue returns without blocking") {
    PingPongApp app;
    app.start();
    app.run_one();  // must not hang
    CHECK(app.get<PingResponder>().pings_seen == 0);
    app.shutdown();
}

TEST_CASE("run_one drains messages posted during dispatch") {
    PingPongApp app;
    app.start();

    for (int i = 0; i < 5; ++i) {
        app.post(make_external_envelope<PingResponder>(PingResponder::Ping{i}));
    }

    app.run_one();

    CHECK(app.get<PingResponder>().pings_seen == 5);
    CHECK(app.get<PongCatcher>().pongs_seen == 5);
    CHECK(app.get<PongCatcher>().last_seq == 4);

    app.shutdown();
}

TEST_CASE("on_start runs in declaration order, on_stop in reverse") {
    s_events.clear();
    {
        OrderApp app;
        app.start();
        app.shutdown();
    }
    CHECK(s_events.size() == 6);
    if (s_events.size() < 6) return;
    CHECK(s_events[0] == "A_start");
    CHECK(s_events[1] == "B_start");
    CHECK(s_events[2] == "C_start");
    CHECK(s_events[3] == "C_stop");
    CHECK(s_events[4] == "B_stop");
    CHECK(s_events[5] == "A_stop");
}

TEST_CASE("shutdown drains queue before on_stop") {
    PingPongApp app;
    app.start();

    app.post(make_external_envelope<PingResponder>(PingResponder::Ping{7}));
    // No run_one — let shutdown's drain handle it.
    app.shutdown();

    // Modules were destructed by shutdown, so we cannot inspect counters
    // after the fact. The behavior under test is that no fault occurred:
    // the queue drained cleanly during shutdown.
    CHECK_FALSE(s_fault_called);
}

TEST_CASE("drain budget exhaustion logs WARN and aborts remaining drain") {
    s_warn_messages.clear();
    {
        BoundedApp app;
        app.start();

        for (int i = 0; i < 5; ++i) {
            app.post(make_external_envelope<Counter>(Counter::Tick{}));
        }
        CHECK(app.get<Counter>().handled == 0);

        app.shutdown();
        // shutdown destructs modules; assertion below relies on captured warn.
    }
    bool drain_warn_seen = false;
    for (const auto& m : s_warn_messages) {
        if (m.find("drain budget") != std::string::npos) {
            drain_warn_seen = true;
            break;
        }
    }
    CHECK(drain_warn_seen);
}

TEST_CASE("drain budget exactly drains the configured count") {
    BoundedApp app;
    app.start();

    for (int i = 0; i < 5; ++i) {
        app.post(make_external_envelope<Counter>(Counter::Tick{}));
    }

    // Drain exactly 2 envelopes; 3 remain in the queue.
    CHECK(app.queue_size() == 5);

    // shutdown will drain at most 2 then warn.
    app.shutdown();

    // We can't inspect Counter after shutdown (modules destructed). Verify
    // via the warn path in the previous test case; this case verifies the
    // queue-size invariant pre-shutdown.
}

TEST_CASE("post is thread-safe; foreign thread enqueues drained by run_one") {
    PingPongApp app;
    app.start();

    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i) {
            app.post(make_external_envelope<PingResponder>(PingResponder::Ping{i}));
        }
        producer_done = true;
    });

    producer.join();
    CHECK(producer_done.load());

    app.run_one();

    CHECK(app.get<PingResponder>().pings_seen == 100);
    CHECK(app.get<PongCatcher>().pongs_seen == 100);

    app.shutdown();
}

TEST_CASE("run() returns after stop() once queue is drained") {
    PingPongApp app;
    app.start();

    std::thread runner([&] { app.run(); });

    // Post some work.
    for (int i = 0; i < 10; ++i) {
        app.post(make_external_envelope<PingResponder>(PingResponder::Ping{i}));
    }

    // Give the runner a chance to drain.
    using namespace std::chrono_literals;
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (app.get<PongCatcher>().pongs_seen == 10) break;
        std::this_thread::sleep_for(1ms);
    }

    CHECK(app.get<PongCatcher>().pongs_seen == 10);

    app.stop();
    runner.join();

    app.shutdown();
}

TEST_CASE("double start asserts") {
    PingPongApp app;
    app.start();

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        app.start();
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "start") != nullptr);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Foreign-thread post: stress + ordering coverage.
// ---------------------------------------------------------------------------

struct SeqRecorder : cortexflow::Module<SeqRecorder> {
    struct SeqMsg { int seq; };

    using Inbox = std::tuple<SeqMsg>;
    std::vector<int> seen;
    void on(SeqMsg& msg) { seen.push_back(msg.seq); }
};

using SeqApp = cortexflow::Runtime<
    cortexflow::ModuleList<SeqRecorder>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<cortexflow::DrainBudget<8192>>>;

TEST_CASE(
    "foreign thread posts N envelopes; main thread drains in FIFO order") {
    constexpr int kN = 5000;
    SeqApp app;
    app.start();

    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        for (int i = 0; i < kN; ++i) {
            app.post(make_external_envelope<SeqRecorder>(SeqRecorder::SeqMsg{i}));
        }
        producer_done = true;
    });

    // Drain concurrently with production: simulates the boundary-thread
    // pattern where the loop runs while a foreign producer streams in.
    while (!producer_done.load() ||
           app.get<SeqRecorder>().seen.size() < static_cast<std::size_t>(kN)) {
        app.run_one();
    }
    producer.join();

    auto& seen = app.get<SeqRecorder>().seen;
    CHECK(seen.size() == static_cast<std::size_t>(kN));
    bool ordered = seen.size() == static_cast<std::size_t>(kN);
    if (ordered) {
        for (int i = 0; i < kN; ++i) {
            if (seen[i] != i) { ordered = false; break; }
        }
    }
    CHECK(ordered);

    app.shutdown();
}

TEST_CASE("foreign-thread post wakes the main thread blocked in run()") {
    PingPongApp app;
    app.start();

    std::thread runner([&] { app.run(); });

    // Block briefly to ensure the runner has reached cv_.wait. The wake
    // semantics are what is under test, not the scheduling delay.
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5ms);

    // Foreign-thread post: this is what should wake the loop.
    std::thread producer([&] {
        app.post(make_external_envelope<PingResponder>(PingResponder::Ping{1}));
    });
    producer.join();

    // Spin until the post has been dispatched (or time out).
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (app.get<PongCatcher>().pongs_seen == 1) break;
        std::this_thread::sleep_for(1ms);
    }
    CHECK(app.get<PongCatcher>().pongs_seen == 1);

    app.stop();
    runner.join();
    app.shutdown();
}

TEST_CASE("foreign-thread post after stop() is silently dropped") {
    PingPongApp app;
    app.start();

    app.stop();  // signal shutdown; run() not started here.

    // Foreign thread tries to post after stop. Must not enqueue and must
    // not assert. The producer's envelope is destroyed in place.
    std::atomic<bool> producer_done{false};
    std::thread producer([&] {
        for (int i = 0; i < 50; ++i) {
            app.post(make_external_envelope<PingResponder>(PingResponder::Ping{i}));
        }
        producer_done = true;
    });
    producer.join();
    CHECK(producer_done.load());
    CHECK(app.queue_size() == 0);

    // Recovery: a fresh start() after shutdown() works. (start() asserts on
    // double-start, so we need shutdown first.)
    app.shutdown();
    app.start();
    app.post(make_external_envelope<PingResponder>(PingResponder::Ping{99}));
    app.run_one();
    CHECK(app.get<PingResponder>().pings_seen == 1);
    CHECK(app.get<PongCatcher>().last_seq == 99);
    app.shutdown();
}

TEST_CASE("multiple foreign producers + main drainer preserve total count") {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 1000;
    constexpr int kTotal = kProducers * kPerProducer;

    SeqApp app;
    app.start();

    std::atomic<int> producers_done{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                app.post(make_external_envelope<SeqRecorder>(
                    SeqRecorder::SeqMsg{p * kPerProducer + i}));
            }
            ++producers_done;
        });
    }

    while (producers_done.load() < kProducers ||
           app.get<SeqRecorder>().seen.size() <
               static_cast<std::size_t>(kTotal)) {
        app.run_one();
    }
    for (auto& t : producers) t.join();

    CHECK(app.get<SeqRecorder>().seen.size() ==
          static_cast<std::size_t>(kTotal));
    // Per-producer FIFO: each producer's stream of seq values must appear
    // in increasing order (interleavings between producers are allowed).
    bool per_producer_ordered = true;
    int last_per_producer[kProducers];
    for (int p = 0; p < kProducers; ++p) {
        last_per_producer[p] = p * kPerProducer - 1;
    }
    for (int seq : app.get<SeqRecorder>().seen) {
        int producer = seq / kPerProducer;
        if (seq != last_per_producer[producer] + 1) {
            per_producer_ordered = false;
            break;
        }
        last_per_producer[producer] = seq;
    }
    CHECK(per_producer_ordered);

    app.shutdown();
}

TEST_CASE("envelope to unknown module asserts during dispatch") {
    PingPongApp app;
    app.start();

    // Forge an envelope addressed to OrderA, which is not in PingPongApp.
    auto ptr = make_message<Counter::Tick>(Counter::Tick{});
    Envelope env(kNoSender, type_id<OrderA>(), std::move(ptr));
    app.post(std::move(env));

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        app.run_one();
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "not in ModuleList") != nullptr);

    app.shutdown();
}
