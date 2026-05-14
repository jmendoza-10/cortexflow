#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/flow.hpp>

#include <csetjmp>
#include <cstring>

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
// A handful of trivial state functions used as identity-checkable pointers.
// ---------------------------------------------------------------------------

using cortexflow::Envelope;
using cortexflow::FlowCtx;
using cortexflow::StateDirective;
using cortexflow::StateFn;

static StateDirective state_dummy_a(FlowCtx&, Envelope&) {
    return cortexflow::stay();
}

static StateDirective state_dummy_b(FlowCtx&, Envelope&) {
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Tests — StateDirective and factory functions
// ---------------------------------------------------------------------------

TEST_CASE("stay() yields Kind::Stay with null next") {
    StateDirective d = cortexflow::stay();
    CHECK(d.kind == StateDirective::Kind::Stay);
    CHECK(d.next == nullptr);
}

TEST_CASE("transition_to(fn) yields Kind::Transition with that fn as next") {
    StateDirective d = cortexflow::transition_to(&state_dummy_b);
    CHECK(d.kind == StateDirective::Kind::Transition);
    CHECK(d.next == &state_dummy_b);
}

TEST_CASE("transition_to(nullptr) asserts") {
    reset_fault_state();
    if (setjmp(s_fault_jump) == 0) {
        (void)cortexflow::transition_to(nullptr);
    }
    CHECK(s_fault_called);
    CHECK(std::strstr(s_fault_reason, "transition_to") != nullptr);
}

TEST_CASE("StateFn typed as free-function pointer; any fn is a valid next") {
    // Compile-time check via assignment: the two distinct state functions
    // have the same StateFn type, so either may be passed as `next` to the
    // other (architecture §8.1, PRD user story 39).
    StateFn p1 = &state_dummy_a;
    StateFn p2 = &state_dummy_b;
    CHECK(p1 != p2);
    StateDirective a_to_b = cortexflow::transition_to(p2);
    StateDirective b_to_a = cortexflow::transition_to(p1);
    CHECK(a_to_b.next == p2);
    CHECK(b_to_a.next == p1);
}

TEST_CASE("kSystemSender is distinct from kNoSender") {
    CHECK(cortexflow::kSystemSender != cortexflow::kNoSender);
}
