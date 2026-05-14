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
    void on_start() override { flow.start(*this); }
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
    void on_start() override { flow.start(*this); }
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
    void on_start() override { flow.start(*this); }
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
    auto& mod = app.get<InertModule>();
    mod.flow.start(mod);

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        mod.flow.start(mod);
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
    void on_start() override { flow.start(*this); }
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
    void on_start() override { flow.start(*this); }
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

    mod.flow.start(mod);
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

// ---------------------------------------------------------------------------
// Slice 15 — `transition_to_now` chained transitions on a single envelope.
//
// Four states form a chain (Chain1 → Chain2 → Chain3 → Chain4) where each
// non-terminal state returns `transition_to_now` of the next. The chain
// completes synchronously inside one `step` call. Every state records the
// envelope's `from` field; if `transition_to_now` is correctly reusing the
// current envelope, all recorded `from` values are identical (and equal to
// the value the original sender stamped on the envelope).
// ---------------------------------------------------------------------------

struct ChainTrigger {};

static std::vector<cortexflow::type_id_t> s_chain_from_values;
static std::vector<std::string> s_chain_visits;

struct Chain1;
struct Chain2;
struct Chain3;
struct Chain4;

struct Chain1 {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        s_chain_visits.emplace_back("C1");
        s_chain_from_values.push_back(env.from());
        return cortexflow::transition_to_now<Chain2>();
    }
};

struct Chain2 {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        s_chain_visits.emplace_back("C2");
        s_chain_from_values.push_back(env.from());
        return cortexflow::transition_to_now<Chain3>();
    }
};

struct Chain3 {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        s_chain_visits.emplace_back("C3");
        s_chain_from_values.push_back(env.from());
        return cortexflow::transition_to_now<Chain4>();
    }
};

struct Chain4 {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        s_chain_visits.emplace_back("C4");
        s_chain_from_values.push_back(env.from());
        return cortexflow::stay();
    }
};

struct ChainModule : cortexflow::Module<ChainModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<ChainModule,
                     StateList<Chain1, Chain2, Chain3, Chain4>> flow;
    void on_start() override { flow.start(*this); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using ChainApp = cortexflow::Runtime<
    cortexflow::ModuleList<ChainModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("transition_to_now reuses the current envelope across a chain of "
          "transitions") {
    s_chain_from_values.clear();
    s_chain_visits.clear();

    ChainApp app;
    app.start();

    // Craft an envelope with a recognisable sender so the test can prove
    // every state saw the *same* envelope.
    constexpr cortexflow::type_id_t kMarkerSender = 0xCAFEBABEULL;
    auto ptr = make_message<ChainTrigger>();
    Envelope env(kMarkerSender, type_id<ChainModule>(), std::move(ptr));
    app.post(std::move(env));
    app.run_one();

    CHECK(s_chain_visits ==
          std::vector<std::string>{"C1", "C2", "C3", "C4"});
    // All four states observed the same `from` — proves envelope reuse, not
    // a fresh envelope per transition.
    CHECK(s_chain_from_values ==
          std::vector<cortexflow::type_id_t>{
              kMarkerSender, kMarkerSender, kMarkerSender, kMarkerSender});

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 15 — `transition_to_now` destructs/constructs locals exactly like a
// regular transition: a RAII probe in each chained state's locals counts
// construct/destruct calls; chaining C1→C2→C3 produces one ctor + one dtor
// per state.
// ---------------------------------------------------------------------------

struct ChainProbeCounts {
    int constructs = 0;
    int destructs = 0;
};

static ChainProbeCounts s_chain_c1;
static ChainProbeCounts s_chain_c2;
static ChainProbeCounts s_chain_c3;

struct Chain1Locals {
    Chain1Locals() noexcept { ++s_chain_c1.constructs; }
    ~Chain1Locals() { ++s_chain_c1.destructs; }
};
struct Chain2Locals {
    Chain2Locals() noexcept { ++s_chain_c2.constructs; }
    ~Chain2Locals() { ++s_chain_c2.destructs; }
};
struct Chain3Locals {
    Chain3Locals() noexcept { ++s_chain_c3.constructs; }
    ~Chain3Locals() { ++s_chain_c3.destructs; }
};

struct ChainP1;
struct ChainP2;
struct ChainP3;

struct ChainP1 {
    using Locals = Chain1Locals;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        return cortexflow::transition_to_now<ChainP2>();
    }
};
struct ChainP2 {
    using Locals = Chain2Locals;
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::transition_to_now<ChainP3>();
    }
};
struct ChainP3 {
    using Locals = Chain3Locals;
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct ChainProbeModule : cortexflow::Module<ChainProbeModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<ChainProbeModule,
                     StateList<ChainP1, ChainP2, ChainP3>> flow;
    void on_start() override { flow.start(*this); }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using ChainProbeApp = cortexflow::Runtime<
    cortexflow::ModuleList<ChainProbeModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("transition_to_now destructs outgoing locals and constructs incoming "
          "locals just like transition_to") {
    s_chain_c1 = ChainProbeCounts{};
    s_chain_c2 = ChainProbeCounts{};
    s_chain_c3 = ChainProbeCounts{};

    {
        ChainProbeApp app;
        app.start();
        // On flow start: ChainP1's locals constructed (init envelope).
        CHECK(s_chain_c1.constructs == 1);
        CHECK(s_chain_c1.destructs == 0);

        // One external Trigger chains P1 → P2 → P3 synchronously.
        app.post(make_external_envelope<ChainProbeModule>(Trigger{}));
        app.run_one();

        CHECK(s_chain_c1.constructs == 1);
        CHECK(s_chain_c1.destructs == 1);
        CHECK(s_chain_c2.constructs == 1);
        CHECK(s_chain_c2.destructs == 1);
        CHECK(s_chain_c3.constructs == 1);
        CHECK(s_chain_c3.destructs == 0);

        app.shutdown();
        // Flow's destructor tears down the live P3 locals.
    }
    CHECK(s_chain_c3.destructs == 1);
}

// ---------------------------------------------------------------------------
// Slice 15 — `done` → `on_flow_done` ordering. The state returns `done()`;
// `on_flow_done` must run synchronously inside that dispatch, *before* any
// further enqueued message is processed.
//
// To distinguish "in-flow" from "out-of-flow" dispatch, the module's
// `handle` routes `WakeMe` to a direct handler that does NOT go through
// `flow.step`. We post Trigger first (which the flow consumes and returns
// `done` for), then WakeMe (which the module handles directly). The
// expected event sequence is:
//   "state_trigger"   — flow's state observed the Trigger
//   "on_flow_done"    — fired synchronously by `done`'s callback
//   "module_wake"     — module handled the next queued envelope
// ---------------------------------------------------------------------------

struct WakeMe {};

static std::vector<std::string> s_done_events;

struct DoneState {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        s_done_events.emplace_back("state_trigger");
        return cortexflow::done();
    }
};

struct DoneModule : cortexflow::Module<DoneModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<DoneModule, StateList<DoneState>> flow;
    void on_start() override { flow.start(*this); }
    void on_flow_done() override {
        s_done_events.emplace_back("on_flow_done");
    }
    void handle(cortexflow::Envelope& env) override {
        if (env.payload_type_id() == type_id<WakeMe>()) {
            s_done_events.emplace_back("module_wake");
            return;
        }
        flow.step(env);
    }
};

using DoneApp = cortexflow::Runtime<
    cortexflow::ModuleList<DoneModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("done() destructs locals and synchronously calls on_flow_done before "
          "the next enqueued message is processed") {
    s_done_events.clear();

    DoneApp app;
    app.start();

    // Queue two envelopes back-to-back: the Trigger that causes the flow
    // to finish, and a WakeMe the module will handle directly.
    app.post(make_external_envelope<DoneModule>(Trigger{}));
    app.post(make_external_envelope<DoneModule>(WakeMe{}));
    app.run_one();

    CHECK(s_done_events == std::vector<std::string>{
        "state_trigger", "on_flow_done", "module_wake"});

    // After `done`, the flow is no longer active; sending another Trigger
    // to it now would hit the "step before start" assert. Sanity-check by
    // inspecting the public predicate.
    CHECK(!app.get<DoneModule>().flow.started());

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 15 — `done` destructs the current state-locals before
// `on_flow_done` fires. A RAII probe in the state's locals confirms this:
// the destruct counter must be incremented by the time the test observes
// the on_flow_done event.
// ---------------------------------------------------------------------------

static ChainProbeCounts s_done_probe;

struct DoneProbeLocals {
    DoneProbeLocals() noexcept { ++s_done_probe.constructs; }
    ~DoneProbeLocals() { ++s_done_probe.destructs; }
};

static int s_done_probe_observed_destructs = -1;

struct DoneProbeState {
    using Locals = DoneProbeLocals;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        return cortexflow::done();
    }
};

struct DoneProbeModule : cortexflow::Module<DoneProbeModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<DoneProbeModule, StateList<DoneProbeState>> flow;
    void on_start() override { flow.start(*this); }
    void on_flow_done() override {
        s_done_probe_observed_destructs = s_done_probe.destructs;
    }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using DoneProbeApp = cortexflow::Runtime<
    cortexflow::ModuleList<DoneProbeModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("done() destructs locals before on_flow_done is invoked") {
    s_done_probe = ChainProbeCounts{};
    s_done_probe_observed_destructs = -1;

    DoneProbeApp app;
    app.start();
    CHECK(s_done_probe.constructs == 1);
    CHECK(s_done_probe.destructs == 0);

    app.post(make_external_envelope<DoneProbeModule>(Trigger{}));
    app.run_one();

    // By the time on_flow_done ran, the state-locals destructor had
    // already fired.
    CHECK(s_done_probe_observed_destructs == 1);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 15 — `flow.restart()` after `done` re-enters the initial state
// with newly constructed locals and re-delivers the synthetic init
// envelope.
// ---------------------------------------------------------------------------

static int s_restart_inits = 0;
static int s_restart_triggers = 0;
static ChainProbeCounts s_restart_locals;

struct RestartLocals {
    RestartLocals() noexcept { ++s_restart_locals.constructs; }
    ~RestartLocals() { ++s_restart_locals.destructs; }
};

struct RestartState {
    using Locals = RestartLocals;
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) {
            ++s_restart_inits;
            return cortexflow::stay();
        }
        ++s_restart_triggers;
        return cortexflow::done();
    }
};

struct RestartModule : cortexflow::Module<RestartModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<RestartModule, StateList<RestartState>> flow;
    void on_start() override { flow.start(*this); }
    void on_flow_done() override { /* no auto-restart in this test */ }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using RestartApp = cortexflow::Runtime<
    cortexflow::ModuleList<RestartModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("restart() after done re-enters the initial state with fresh locals "
          "and a fresh synthetic init envelope") {
    s_restart_inits = 0;
    s_restart_triggers = 0;
    s_restart_locals = ChainProbeCounts{};

    RestartApp app;
    app.start();
    CHECK(s_restart_inits == 1);
    CHECK(s_restart_triggers == 0);
    CHECK(s_restart_locals.constructs == 1);
    CHECK(s_restart_locals.destructs == 0);

    // First Trigger: state returns done, locals destructed.
    app.post(make_external_envelope<RestartModule>(Trigger{}));
    app.run_one();
    CHECK(s_restart_triggers == 1);
    CHECK(s_restart_locals.destructs == 1);
    CHECK(!app.get<RestartModule>().flow.started());

    // Restart: fresh init envelope, fresh locals construction.
    app.get<RestartModule>().flow.restart();
    CHECK(s_restart_inits == 2);
    CHECK(s_restart_locals.constructs == 2);
    CHECK(s_restart_locals.destructs == 1);
    CHECK(app.get<RestartModule>().flow.started());

    // Second Trigger after restart: state runs again, returns done.
    app.post(make_external_envelope<RestartModule>(Trigger{}));
    app.run_one();
    CHECK(s_restart_triggers == 2);
    CHECK(s_restart_locals.destructs == 2);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 15 — `flow.terminate()` from outside `step` tears down the active
// flow without invoking `on_flow_done`.
// ---------------------------------------------------------------------------

struct TerminateMe {};

static int s_term_locals_constructs = 0;
static int s_term_locals_destructs = 0;
static int s_term_on_flow_done_calls = 0;

struct TermLocals {
    TermLocals() noexcept { ++s_term_locals_constructs; }
    ~TermLocals() { ++s_term_locals_destructs; }
};

struct TermState {
    using Locals = TermLocals;
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct TermModule : cortexflow::Module<TermModule> {
    using Inbox = std::tuple<>;
    cortexflow::Flow<TermModule, StateList<TermState>> flow;
    void on_start() override { flow.start(*this); }
    void on_flow_done() override { ++s_term_on_flow_done_calls; }
    void handle(cortexflow::Envelope& env) override {
        if (env.payload_type_id() == type_id<TerminateMe>()) {
            // Outside flow.step — invoked directly from the module's
            // dispatch path.
            flow.terminate();
            return;
        }
        flow.step(env);
    }
};

using TermApp = cortexflow::Runtime<
    cortexflow::ModuleList<TermModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("terminate() from outside step destructs locals and deactivates the "
          "flow without firing on_flow_done") {
    s_term_locals_constructs = 0;
    s_term_locals_destructs = 0;
    s_term_on_flow_done_calls = 0;

    TermApp app;
    app.start();
    CHECK(s_term_locals_constructs == 1);
    CHECK(s_term_locals_destructs == 0);

    app.post(make_external_envelope<TermModule>(TerminateMe{}));
    app.run_one();

    CHECK(s_term_locals_destructs == 1);
    CHECK(s_term_on_flow_done_calls == 0);
    CHECK(!app.get<TermModule>().flow.started());

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Slice 15 — calling `terminate()` from inside `step` (the state's handle)
// asserts. The test reaches that path by smuggling a flow pointer through a
// global; production code can never do this from a `static handle(...)`
// because FlowCtx exposes no Flow access, but the assert is the safety
// belt for any future Flow API that might cross this boundary.
// ---------------------------------------------------------------------------

static void* s_term_assert_flow = nullptr;
using TermAssertFlowType =
    cortexflow::Flow<struct TermAssertModule,
                     StateList<struct TermAssertState>>;

struct TermAssertState {
    static StateDirective handle(FlowCtx&, Envelope& env) {
        if (env.from() == kSystemSender) return cortexflow::stay();
        // Smuggle access to the owning Flow and try to terminate from
        // inside a step — must trip the in_step_ assert.
        static_cast<TermAssertFlowType*>(s_term_assert_flow)->terminate();
        return cortexflow::stay();
    }
};

struct TermAssertModule : cortexflow::Module<TermAssertModule> {
    using Inbox = std::tuple<>;
    TermAssertFlowType flow;
    void on_start() override {
        s_term_assert_flow = &flow;
        flow.start(*this);
    }
    void handle(cortexflow::Envelope& env) override { flow.step(env); }
};

using TermAssertApp = cortexflow::Runtime<
    cortexflow::ModuleList<TermAssertModule>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

TEST_CASE("terminate() called from inside a step asserts") {
    TermAssertApp app;
    app.start();

    app.post(make_external_envelope<TermAssertModule>(Trigger{}));

    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        app.run_one();
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "inside a step") != nullptr);

    s_term_assert_flow = nullptr;
    // We don't shutdown — the runtime is in an inconsistent state after
    // the longjmp out of dispatch.
}
