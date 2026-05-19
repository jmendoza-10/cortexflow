// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/flow.hpp>

// ---------------------------------------------------------------------------
// A handful of trivial state-tag types used as identity-checkable values.
// ---------------------------------------------------------------------------

using cortexflow::Envelope;
using cortexflow::FlowCtx;
using cortexflow::StateDirective;

struct StateDummyA {
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct StateDummyB {
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

// ---------------------------------------------------------------------------
// Tests — StateDirective and factory functions
// ---------------------------------------------------------------------------

TEST_CASE("stay() yields Kind::Stay with null next") {
    StateDirective d = cortexflow::stay();
    CHECK(d.kind == StateDirective::Kind::Stay);
    CHECK(d.next == nullptr);
}

TEST_CASE("transition_to<Tag>() yields Kind::Transition with that tag's info") {
    StateDirective d = cortexflow::transition_to<StateDummyB>();
    CHECK(d.kind == StateDirective::Kind::Transition);
    CHECK(d.next == &cortexflow::detail::kStateInfo<StateDummyB>);
    CHECK(d.next->handle != nullptr);
    CHECK(d.next->construct_locals != nullptr);
    CHECK(d.next->destruct_locals != nullptr);
}

TEST_CASE("distinct state tags produce distinct StateInfo addresses") {
    // Each `kStateInfo<Tag>` is its own variable template instantiation, so
    // distinct state-tag types compare unequal as pointers. Pointer identity
    // is the runtime state identity.
    StateDirective a_to_b = cortexflow::transition_to<StateDummyB>();
    StateDirective b_to_a = cortexflow::transition_to<StateDummyA>();
    CHECK(a_to_b.next != b_to_a.next);
}

TEST_CASE("any state tag can be passed as next for any other") {
    // Mirrors PRD user story 39: no static graph constraint. The choice of
    // next is uniformly typed across all state tags via `transition_to<T>`.
    StateDirective a_to_b = cortexflow::transition_to<StateDummyB>();
    StateDirective b_to_a = cortexflow::transition_to<StateDummyA>();
    CHECK(a_to_b.next == &cortexflow::detail::kStateInfo<StateDummyB>);
    CHECK(b_to_a.next == &cortexflow::detail::kStateInfo<StateDummyA>);
}

TEST_CASE("kSystemSender is distinct from kNoSender") {
    CHECK(cortexflow::kSystemSender != cortexflow::kNoSender);
}

// ---------------------------------------------------------------------------
// Tests — StateList compile-time computations
// ---------------------------------------------------------------------------

struct SmallLocals { char x; };
struct BigLocals { double a, b, c; };

struct StateSmall {
    using Locals = SmallLocals;
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct StateBig {
    using Locals = BigLocals;
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

struct StateNoLocals {
    static StateDirective handle(FlowCtx&, Envelope&) {
        return cortexflow::stay();
    }
};

TEST_CASE("StateList sizes the buffer to the largest Locals across the list") {
    using L = cortexflow::StateList<StateSmall, StateBig, StateNoLocals>;
    CHECK(L::kMaxLocalsSize == sizeof(BigLocals));
}

TEST_CASE("StateList picks the strictest alignment across the list") {
    using L = cortexflow::StateList<StateSmall, StateBig, StateNoLocals>;
    CHECK(L::kMaxLocalsAlign == alignof(BigLocals));
}

TEST_CASE("StateList of states without Locals defaults to the empty struct") {
    using L = cortexflow::StateList<StateNoLocals>;
    // An empty struct has sizeof == 1 and alignof == 1.
    CHECK(L::kMaxLocalsSize == sizeof(cortexflow::detail::EmptyLocals));
    CHECK(L::kMaxLocalsAlign == alignof(cortexflow::detail::EmptyLocals));
}

// ---------------------------------------------------------------------------
// Slice 15 — transition_to_now / done factory tests
// ---------------------------------------------------------------------------

TEST_CASE("transition_to_now<Tag>() yields Kind::TransitionNow with that tag's "
          "info") {
    StateDirective d = cortexflow::transition_to_now<StateDummyB>();
    CHECK(d.kind == StateDirective::Kind::TransitionNow);
    CHECK(d.next == &cortexflow::detail::kStateInfo<StateDummyB>);
}

TEST_CASE("done() yields Kind::Done with null next") {
    StateDirective d = cortexflow::done();
    CHECK(d.kind == StateDirective::Kind::Done);
    CHECK(d.next == nullptr);
}

TEST_CASE("the four directive Kinds are pairwise distinct") {
    // Sanity check that no enumerator collides under the encoding extension.
    CHECK(StateDirective::Kind::Stay != StateDirective::Kind::Transition);
    CHECK(StateDirective::Kind::Stay != StateDirective::Kind::TransitionNow);
    CHECK(StateDirective::Kind::Stay != StateDirective::Kind::Done);
    CHECK(StateDirective::Kind::Transition !=
          StateDirective::Kind::TransitionNow);
    CHECK(StateDirective::Kind::Transition != StateDirective::Kind::Done);
    CHECK(StateDirective::Kind::TransitionNow != StateDirective::Kind::Done);
}
