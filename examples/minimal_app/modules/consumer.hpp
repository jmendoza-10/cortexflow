// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tuple>

#include <cortexflow/cache.hpp>
#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>
#include <cortexflow/subscription.hpp>
#include <cortexflow/timer.hpp>

#include "../keys.hpp"

namespace minimal_app {

// Idle and Processing must be *complete types* by the time Consumer's `flow`
// member is declared — the Flow buffer is sized from each state's `Locals`
// type via `sizeof`, and a forward-declared state would silently size the
// buffer to the empty-fallback type and corrupt memory at first transition.
// Consumer therefore forward-declares first and the state structs are defined
// here, in front of the class. Their `Locals` constructors live in the .cpp
// because they reach back into Consumer / app-wide helpers.

class Consumer;

// Idle — waiting for a KeyChanged<Counter>. The state-local subscription
// keeps Consumer registered with the cache for as long as Idle is active;
// transitioning out destructs the locals and releases the slot
// automatically (PRD US 25).
struct Idle {
    struct Locals {
        cortexflow::Subscription sub;
        Locals();
    };
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

// Processing — armed with a Timer that fires `Consumer::ProcessingTick` back
// to Consumer after a fixed delay. The timer lifetime is bound to the locals,
// so transitioning out cancels the timer if it has not yet fired (PRD US 40).
struct Processing {
    struct Locals {
        cortexflow::Timer timer;
        Locals();
    };
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

// Consumer — subscribes to Counter and runs a two-state flow.
//
// Inbox routing:
//   - KeyChanged<Counter>: arrives from cache fanout while Idle is the active
//     state (Idle's locals hold the subscription).
//   - ProcessingTick: fired by Processing's state-local Timer. Consumer's
//     module-level handler intercepts it to send `Producer::Done` back to
//     Producer before forwarding into the flow, which then transitions back
//     to Idle.
//
// Because every envelope is routed through `flow.step`, Consumer declares an
// empty `Inbox` and overrides `handle` directly — the dispatch-table path in
// `Module::handle` is bypassed (see test_flow.cpp for the same pattern).
class Consumer : public cortexflow::Module<Consumer> {
public:
    // Receiver-owned message type (ADR 0020): ProcessingTick is self-sent by
    // Processing's state-local Timer back into Consumer's queue.
    struct ProcessingTick {};

    using Inbox = std::tuple<>;

    cortexflow::Flow<Consumer,
                     cortexflow::StateList<Idle, Processing>> flow;

    void on_start() override;
    void handle(cortexflow::Envelope& env) override;
};

}  // namespace minimal_app
