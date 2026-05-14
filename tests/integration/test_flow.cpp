#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/cache.hpp>
#include <cortexflow/flow.hpp>
#include <cortexflow/runtime.hpp>
#include <cortexflow/subscription.hpp>

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
using cortexflow::StateList;
using cortexflow::kNoSender;
using cortexflow::kSystemSender;
using cortexflow::make_message;
using cortexflow::type_id;

// ---------------------------------------------------------------------------
// Two-state flow used by the transition test and the init-visibility test.
// ---------------------------------------------------------------------------

static std::vector<std::string> s_events;

struct StateA;
struct StateB;

struct StateA {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) {
            s_events.emplace_back("A_init");
            return cortexflow::stay();
        }
        if (env.payload_type_id() == type_id<Trigger>()) {
            s_events.emplace_back("A_trigger");
            return cortexflow::transition_to<StateB>();
        }
        s_events.emplace_back("A_other");
        return cortexflow::stay();
    }
};

struct StateB {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.payload_type_id() == type_id<Followup>()) {
            s_events.emplace_back("B_followup");
        } else if (env.payload_type_id() == type_id<Trigger>()) {
            s_events.emplace_back("B_trigger");
        } else {
            s_events.emplace_back("B_other");
        }
        return cortexflow::stay();
    }
};

struct FlowModule : cortexflow::Module<FlowModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<FlowModule, StateList<StateA, StateB>> flow;
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
// PRD user story 39 — any state tag is a legal `next` for any other.
// ---------------------------------------------------------------------------

static int s_x_hits = 0;
static int s_y_hits = 0;

struct StateX;
struct StateY;

struct StateX {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        ++s_x_hits;
        return cortexflow::transition_to<StateY>();
    }
};

struct StateY {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        ++s_y_hits;
        return cortexflow::transition_to<StateX>();
    }
};

struct DynModule : cortexflow::Module<DynModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<DynModule, StateList<StateX, StateY>> flow;
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

struct ProbeState {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        s_probe_from = env.from();
        s_probe_seen = true;
        return cortexflow::stay();
    }
};

struct ProbeModule : cortexflow::Module<ProbeModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<ProbeModule, StateList<ProbeState>> flow;
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

struct InertState {
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct InertModule : cortexflow::Module<InertModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<InertModule, StateList<InertState>> flow;
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
// Test cases — acceptance criteria preserved from issue 13.
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

TEST_CASE("any state tag is a legal next for any other — dynamic flow shape") {
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

// ---------------------------------------------------------------------------
// Slice 14 — state-locals construct/destruct lifecycle (RAII probe).
//
// Each probe state's locals contains a `ProbeLocals` whose constructor and
// destructor increment global counters tagged by the state tag. The test
// drives the flow through entries and transitions and asserts that exactly
// one construction is observed per entry and exactly one destruction per
// transition-out, with no manual cleanup in the state body.
// ---------------------------------------------------------------------------

struct ProbeCounts {
    int constructs = 0;
    int destructs = 0;
};

static ProbeCounts s_probe_p;
static ProbeCounts s_probe_q;

struct ProbeLocalsP {
    ProbeLocalsP() noexcept { ++s_probe_p.constructs; }
    ~ProbeLocalsP() { ++s_probe_p.destructs; }
    ProbeLocalsP(const ProbeLocalsP&) = delete;
    ProbeLocalsP& operator=(const ProbeLocalsP&) = delete;
};

struct ProbeLocalsQ {
    ProbeLocalsQ() noexcept { ++s_probe_q.constructs; }
    ~ProbeLocalsQ() { ++s_probe_q.destructs; }
    ProbeLocalsQ(const ProbeLocalsQ&) = delete;
    ProbeLocalsQ& operator=(const ProbeLocalsQ&) = delete;
};

struct StateP;
struct StateQ;

struct StateP {
    using Locals = ProbeLocalsP;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        return cortexflow::transition_to<StateQ>();
    }
};

struct StateQ {
    using Locals = ProbeLocalsQ;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        return cortexflow::transition_to<StateP>();
    }
};

struct ProbeRaiiModule : cortexflow::Module<ProbeRaiiModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<ProbeRaiiModule, StateList<StateP, StateQ>> flow;
    void on_start() override { flow.start(); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using ProbeRaiiApp = cortexflow::Runtime<
    cortexflow::ModuleList<ProbeRaiiModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("state-locals construct exactly once per entry and destruct exactly "
          "once per transition") {
    s_probe_p = ProbeCounts{};
    s_probe_q = ProbeCounts{};

    {
        ProbeRaiiApp app;
        app.start();
        // Entering StateP at start() constructs ProbeLocalsP exactly once.
        CHECK(s_probe_p.constructs == 1);
        CHECK(s_probe_p.destructs == 0);
        CHECK(s_probe_q.constructs == 0);
        CHECK(s_probe_q.destructs == 0);

        // P -> Q: ProbeLocalsP destructed, ProbeLocalsQ constructed.
        app.post(make_external_envelope<ProbeRaiiModule>(Trigger{}));
        app.run_one();
        CHECK(s_probe_p.constructs == 1);
        CHECK(s_probe_p.destructs == 1);
        CHECK(s_probe_q.constructs == 1);
        CHECK(s_probe_q.destructs == 0);

        // Q -> P: ProbeLocalsQ destructed, ProbeLocalsP constructed again.
        app.post(make_external_envelope<ProbeRaiiModule>(Trigger{}));
        app.run_one();
        CHECK(s_probe_p.constructs == 2);
        CHECK(s_probe_p.destructs == 1);
        CHECK(s_probe_q.constructs == 1);
        CHECK(s_probe_q.destructs == 1);

        // P -> Q: another full transition round.
        app.post(make_external_envelope<ProbeRaiiModule>(Trigger{}));
        app.run_one();
        CHECK(s_probe_p.constructs == 2);
        CHECK(s_probe_p.destructs == 2);
        CHECK(s_probe_q.constructs == 2);
        CHECK(s_probe_q.destructs == 1);

        app.shutdown();
        // Module's destructor (post-shutdown) destructs the Flow, which
        // destructs the currently-live locals (Q).
    }
    CHECK(s_probe_p.constructs == 2);
    CHECK(s_probe_p.destructs == 2);
    CHECK(s_probe_q.constructs == 2);
    CHECK(s_probe_q.destructs == 2);
}

// ---------------------------------------------------------------------------
// Slice 14 — state-local Subscription releases on transition (no manual
// cleanup). The locals struct holds a cache Subscription as a member; the
// transition out of that state must release the subscription slot back to
// the cache pool.
// ---------------------------------------------------------------------------

struct DummyKey {
    using value_type = int;
};

struct SubLocalsCtx {
    using Cache = cortexflow::Cache<cortexflow::CacheKeyList<DummyKey>, 4>;
    static Cache* cache;
};
SubLocalsCtx::Cache* SubLocalsCtx::cache = nullptr;

struct SubLocals {
    cortexflow::Subscription sub;
    SubLocals()
        : sub(SubLocalsCtx::cache->subscribe<DummyKey>(0)) {}
};

struct SubStateHold;
struct SubStateReleased;

// Holds a Subscription via state-locals. On any non-init envelope,
// transitions to SubStateReleased — which has no locals at all, so the
// Subscription slot must be returned to the pool by the framework.
struct SubStateHold {
    using Locals = SubLocals;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        return cortexflow::transition_to<SubStateReleased>();
    }
};

struct SubStateReleased {
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct SubModule : cortexflow::Module<SubModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<SubModule, StateList<SubStateHold, SubStateReleased>> flow;
    void on_start() override { flow.start(); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

TEST_CASE("state-local Subscription is released on transition with no manual "
          "cleanup") {
    SubLocalsCtx::Cache cache;
    SubLocalsCtx::cache = &cache;
    // Cache's post sink is unused in this test (no `set` is performed); a
    // null sink would only matter if we triggered a fanout.

    SubModule mod;
    // The module's `bind_post` is never invoked because this test does not
    // route messages through a Runtime — only the cache subscribe path is
    // exercised. Subscription's RAII release uses the trampoline stored on
    // the handle and does not depend on the module's post sink.

    CHECK(cache.subscriber_count() == 0);

    mod.flow.start();
    // Entering SubStateHold constructed SubLocals, which subscribed once.
    CHECK(cache.subscriber_count() == 1);

    auto env = make_external_envelope<SubModule>(Trigger{});
    mod.flow.step(env);
    // The transition out of SubStateHold destructed SubLocals, which
    // dropped the Subscription handle, which released the slot. No code in
    // SubStateHold::handle did any cleanup.
    CHECK(cache.subscriber_count() == 0);

    SubLocalsCtx::cache = nullptr;
}
