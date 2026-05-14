#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/flow.hpp>
#include <cortexflow/runtime.hpp>

#include <csetjmp>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Fault handler override (negative-path tests rely on this).
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
// Shared message types.
// ---------------------------------------------------------------------------

struct Trigger {};
struct Followup {};

using cortexflow::Envelope;
using cortexflow::FlowCtx;
using cortexflow::StateDirective;
using cortexflow::kNoSender;
using cortexflow::kSystemSender;
using cortexflow::make_message;
using cortexflow::type_id;

// ---------------------------------------------------------------------------
// Two-state flow used by the transition test and the init-visibility test.
// ---------------------------------------------------------------------------

static std::vector<std::string> s_events;

static StateDirective state_b(FlowCtx&, Envelope&);

static StateDirective state_a(FlowCtx&, Envelope& env) {
    if (env.from() == kSystemSender) {
        s_events.emplace_back("A_init");
        return cortexflow::stay();
    }
    if (env.payload_type_id() == type_id<Trigger>()) {
        s_events.emplace_back("A_trigger");
        return cortexflow::transition_to(&state_b);
    }
    s_events.emplace_back("A_other");
    return cortexflow::stay();
}

static StateDirective state_b(FlowCtx&, Envelope& env) {
    if (env.payload_type_id() == type_id<Followup>()) {
        s_events.emplace_back("B_followup");
    } else if (env.payload_type_id() == type_id<Trigger>()) {
        s_events.emplace_back("B_trigger");
    } else {
        s_events.emplace_back("B_other");
    }
    return cortexflow::stay();
}

struct FlowModule : cortexflow::Module<FlowModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<FlowModule> flow{&state_a};
    void on_start() override { flow.start(); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using FlowApp = cortexflow::Runtime<
    cortexflow::ModuleList<FlowModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Ping-pong flow used by the dynamic-shape test: state_x toggles to state_y
// on Trigger, state_y toggles back to state_x on Trigger. The pair proves
// PRD user story 39 — any StateFn is a legal `next` for any other.
// ---------------------------------------------------------------------------

static int s_x_hits = 0;
static int s_y_hits = 0;

static StateDirective state_x(FlowCtx&, Envelope&);
static StateDirective state_y(FlowCtx&, Envelope&);

static StateDirective state_x(FlowCtx&, Envelope& env) {
    if (env.from() == kSystemSender) return cortexflow::stay();
    ++s_x_hits;
    return cortexflow::transition_to(&state_y);
}

static StateDirective state_y(FlowCtx&, Envelope& env) {
    if (env.from() == kSystemSender) return cortexflow::stay();
    ++s_y_hits;
    return cortexflow::transition_to(&state_x);
}

struct DynModule : cortexflow::Module<DynModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<DynModule> flow{&state_x};
    void on_start() override { flow.start(); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using DynApp = cortexflow::Runtime<
    cortexflow::ModuleList<DynModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Probe flow: captures the `from` field of whatever envelope first reaches
// the initial state.
// ---------------------------------------------------------------------------

static cortexflow::type_id_t s_probe_from = 0;
static bool s_probe_seen = false;

static StateDirective probe_state(FlowCtx&, Envelope& env) {
    s_probe_from = env.from();
    s_probe_seen = true;
    return cortexflow::stay();
}

struct ProbeModule : cortexflow::Module<ProbeModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<ProbeModule> flow{&probe_state};
    void on_start() override { flow.start(); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using ProbeApp = cortexflow::Runtime<
    cortexflow::ModuleList<ProbeModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Inert flow used by negative-path tests. Its on_start does NOT call
// flow.start, so the test can poke the underlying flow directly.
// ---------------------------------------------------------------------------

static StateDirective inert_state(FlowCtx&, Envelope&) {
    return cortexflow::stay();
}

struct InertModule : cortexflow::Module<InertModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<InertModule> flow{&inert_state};
    void on_start() override {}  // intentionally do not start the flow
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using InertApp = cortexflow::Runtime<
    cortexflow::ModuleList<InertModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <typename Target, typename Msg>
static Envelope make_external_envelope(Msg&& payload) {
    auto ptr = make_message<std::decay_t<Msg>>(std::forward<Msg>(payload));
    return Envelope(kNoSender, type_id<Target>(), std::move(ptr));
}

// ---------------------------------------------------------------------------
// Test cases — acceptance criteria from issue 13.
// ---------------------------------------------------------------------------

TEST_CASE("initial state sees synthetic init envelope before any external send") {
    s_events.clear();
    FlowApp app;
    app.start();

    // `app.start()` invokes `on_start` on the module, which calls
    // `flow.start()`; that synchronously dispatches the synthetic init
    // envelope into the initial state. By the time `start()` returns, the
    // initial state has already observed the init envelope — no external
    // post happened in between.
    CHECK(s_events.size() == 1);
    CHECK(s_events == std::vector<std::string>{"A_init"});

    app.shutdown();
}

TEST_CASE("two-state flow transitions on triggering message; "
          "subsequent message handled by the new state") {
    s_events.clear();
    FlowApp app;
    app.start();
    CHECK(s_events == std::vector<std::string>{"A_init"});

    // First external message — Trigger — fires the transition A -> B.
    app.post(make_external_envelope<FlowModule>(Trigger{}));
    app.run_one();
    CHECK(s_events == std::vector<std::string>{"A_init", "A_trigger"});

    // Subsequent message — Followup — is observed by state B, proving the
    // transition took effect.
    app.post(make_external_envelope<FlowModule>(Followup{}));
    app.run_one();
    CHECK(s_events == std::vector<std::string>{
        "A_init", "A_trigger", "B_followup"});

    // A second Trigger now reaches state B (the new state), not state A.
    app.post(make_external_envelope<FlowModule>(Trigger{}));
    app.run_one();
    CHECK(s_events == std::vector<std::string>{
        "A_init", "A_trigger", "B_followup", "B_trigger"});

    app.shutdown();
}

TEST_CASE("synthetic init envelope carries from = kSystemSender") {
    s_probe_from = 0;
    s_probe_seen = false;

    ProbeApp app;
    app.start();
    CHECK(s_probe_seen);
    CHECK(s_probe_from == kSystemSender);
    app.shutdown();
}

TEST_CASE("any StateFn is a legal next for any other — dynamic flow shape") {
    s_x_hits = 0;
    s_y_hits = 0;

    DynApp app;
    app.start();

    auto post_trigger = [&] {
        app.post(make_external_envelope<DynModule>(Trigger{}));
        app.run_one();
    };

    post_trigger();  // x -> y
    post_trigger();  // y -> x
    post_trigger();  // x -> y
    CHECK(s_x_hits == 2);
    CHECK(s_y_hits == 1);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Negative paths
// ---------------------------------------------------------------------------

TEST_CASE("Flow::start called twice asserts") {
    InertApp app;
    app.start();
    app.get<InertModule>().flow.start();

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        app.get<InertModule>().flow.start();
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "already started") != nullptr);

    app.shutdown();
}

TEST_CASE("Flow::step before start asserts") {
    InertApp app;
    app.start();

    app.post(make_external_envelope<InertModule>(Trigger{}));

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        app.run_one();
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "before start") != nullptr);

    app.shutdown();
}
