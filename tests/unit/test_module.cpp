#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/module.hpp>

#include <csetjmp>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Strong override of the weak platform_fault_handler.
// Captures call arguments and longjmps back to the test, preventing abort.
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
// Post sink capture — stores envelopes posted by modules under test.
// ---------------------------------------------------------------------------

static std::vector<cortexflow::Envelope> s_posted;

static void capture_post(void* /*ctx*/, cortexflow::Envelope&& env) {
    s_posted.push_back(std::move(env));
}

// ---------------------------------------------------------------------------
// Test message types
// ---------------------------------------------------------------------------

struct Ping { int seq; };
struct Pong { int seq; };
struct Status { bool ok; };
struct Unknown { int x; };

// ---------------------------------------------------------------------------
// Test modules
// ---------------------------------------------------------------------------

struct Receiver : cortexflow::Module<Receiver> {
    using Inbox = std::tuple<Ping, Status>;

    int ping_count = 0;
    int last_seq = -1;
    int status_count = 0;
    bool last_ok = false;

    void on(Ping& msg) {
        ++ping_count;
        last_seq = msg.seq;
    }

    void on(Status& msg) {
        ++status_count;
        last_ok = msg.ok;
    }
};

struct Replier : cortexflow::Module<Replier> {
    using Inbox = std::tuple<Ping>;

    void on(Ping& msg) {
        reply_to(envelope(), Pong{msg.seq});
    }
};

struct PongReceiver : cortexflow::Module<PongReceiver> {
    using Inbox = std::tuple<Pong>;

    int pong_count = 0;
    int last_seq = -1;

    void on(Pong& msg) {
        ++pong_count;
        last_seq = msg.seq;
    }
};

using cortexflow::Envelope;
using cortexflow::kNoSender;
using cortexflow::make_message;
using cortexflow::type_id;

// ---------------------------------------------------------------------------
// Dispatch table — inbox messages reach correct handler
// ---------------------------------------------------------------------------

TEST_CASE("dispatch routes Ping to on(Ping&)") {
    Receiver r;
    auto msg = make_message<Ping>(Ping{42});
    Envelope env(kNoSender, type_id<Receiver>(), std::move(msg));
    r.handle(env);
    CHECK(r.ping_count == 1);
    CHECK(r.last_seq == 42);
    CHECK(r.status_count == 0);
}

TEST_CASE("dispatch routes Status to on(Status&)") {
    Receiver r;
    auto msg = make_message<Status>(Status{true});
    Envelope env(kNoSender, type_id<Receiver>(), std::move(msg));
    r.handle(env);
    CHECK(r.status_count == 1);
    CHECK(r.last_ok == true);
    CHECK(r.ping_count == 0);
}

TEST_CASE("multiple dispatches to same module") {
    Receiver r;

    auto p1 = make_message<Ping>(Ping{1});
    Envelope e1(kNoSender, type_id<Receiver>(), std::move(p1));
    r.handle(e1);

    auto p2 = make_message<Ping>(Ping{2});
    Envelope e2(kNoSender, type_id<Receiver>(), std::move(p2));
    r.handle(e2);

    auto s = make_message<Status>(Status{false});
    Envelope e3(kNoSender, type_id<Receiver>(), std::move(s));
    r.handle(e3);

    CHECK(r.ping_count == 2);
    CHECK(r.last_seq == 2);
    CHECK(r.status_count == 1);
    CHECK(r.last_ok == false);
}

// ---------------------------------------------------------------------------
// Out-of-inbox message — CORTEXFLOW_ASSERT
// ---------------------------------------------------------------------------

TEST_CASE("out-of-inbox message triggers CORTEXFLOW_ASSERT") {
    Receiver r;
    auto msg = make_message<Unknown>(Unknown{0});
    Envelope env(kNoSender, type_id<Receiver>(), std::move(msg));

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        r.handle(env);
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "not in module inbox") != nullptr);
}

// ---------------------------------------------------------------------------
// Identity — type_id<Module> matches kTypeId and module_type_id()
// ---------------------------------------------------------------------------

TEST_CASE("module_type_id equals type_id<Module>") {
    Receiver r;
    CHECK(r.module_type_id() == type_id<Receiver>());
}

TEST_CASE("kTypeId equals type_id<Module>") {
    CHECK(Receiver::kTypeId == type_id<Receiver>());
    CHECK(Replier::kTypeId == type_id<Replier>());
    CHECK(PongReceiver::kTypeId == type_id<PongReceiver>());
}

TEST_CASE("module_type_id and kTypeId are consistent") {
    Receiver r;
    CHECK(r.module_type_id() == Receiver::kTypeId);
}

// ---------------------------------------------------------------------------
// reply_to — sends message to envelope.from
// ---------------------------------------------------------------------------

TEST_CASE("reply_to sends response to envelope.from") {
    Replier rep;
    rep.bind_post(&capture_post, nullptr);
    s_posted.clear();

    auto sender_id = type_id<PongReceiver>();
    auto msg = make_message<Ping>(Ping{7});
    Envelope env(sender_id, type_id<Replier>(), std::move(msg));

    rep.handle(env);

    CHECK(s_posted.size() == 1);
    if (s_posted.empty()) return;
    auto& reply = s_posted[0];
    CHECK(reply.from() == type_id<Replier>());
    CHECK(reply.to() == sender_id);
    CHECK(reply.payload_type_id() == type_id<Pong>());
    CHECK(reply.payload<Pong>().seq == 7);
}

TEST_CASE("reply_to with sentinel from asserts") {
    Replier rep;
    rep.bind_post(&capture_post, nullptr);
    s_posted.clear();

    auto msg = make_message<Ping>(Ping{0});
    Envelope env(kNoSender, type_id<Replier>(), std::move(msg));

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        rep.handle(env);
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "sentinel") != nullptr);
}

// ---------------------------------------------------------------------------
// Reply round-trip — end-to-end dispatch through two modules
// ---------------------------------------------------------------------------

TEST_CASE("reply round-trip: Replier posts Pong dispatched to PongReceiver") {
    Replier rep;
    rep.bind_post(&capture_post, nullptr);
    s_posted.clear();

    auto sender_id = type_id<PongReceiver>();
    auto ping = make_message<Ping>(Ping{99});
    Envelope env(sender_id, type_id<Replier>(), std::move(ping));

    rep.handle(env);

    CHECK(s_posted.size() == 1);
    if (s_posted.empty()) return;

    PongReceiver recv;
    recv.handle(s_posted[0]);

    CHECK(recv.pong_count == 1);
    CHECK(recv.last_seq == 99);
}

// ---------------------------------------------------------------------------
// Lifecycle hooks — default no-op implementation
// ---------------------------------------------------------------------------

TEST_CASE("lifecycle hooks have default no-op implementation") {
    Receiver r;
    r.on_start();
    r.on_stop();
    r.on_flow_done();
    CHECK(true);
}
