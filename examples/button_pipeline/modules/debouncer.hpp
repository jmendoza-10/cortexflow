// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tuple>

#include <cortexflow/cache.hpp>
#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>
#include <cortexflow/timer.hpp>

#include "../keys.hpp"

namespace button_pipeline {

// Settled and CoolingDown must be *complete types* by the time Debouncer's
// `flow` member is declared — the Flow buffer is sized from each state's
// `Locals` type via `sizeof`, and a forward-declared state would silently size
// the buffer to the empty-fallback type and corrupt memory at first
// transition. Debouncer therefore forward-declares first and the state structs
// are defined here in front of the class. Their handlers live in the .cpp
// where they can reach the app-wide cache() / timers() helpers.

class Debouncer;

// ---------------------------------------------------------------------------
// "Lockout" vs "wait-for-silence" debouncer
//
// Two patterns are common for digital contact debouncing:
//
//   1. *Lockout* (the one implemented here): commit the new value on the first
//      edge that disagrees with the cached value, then ignore further raw
//      transitions for kDebounceWindow. The lockout window is held by a Timer
//      armed in CoolingDown's Locals — transitioning back to Settled destructs
//      the Locals and the Timer along with them. The pattern maps cleanly onto
//      Locals' construct-on-entry / destruct-on-transition lifetime.
//
//   2. *Wait-for-silence*: keep re-arming a Timer on every observed raw
//      transition; commit only once the line has been quiet for the full
//      window. This requires mutating Locals mid-state (via `ctx.locals<L>()`
//      in `handle`) rather than rebuilding them on every transition.
//
// The lockout pattern is preferred for this example because it demonstrates
// the RAII state-locals story without introducing the mid-state-mutation
// concept. The wait-for-silence variant is supported by the framework
// (`flow.hpp`'s FlowCtx::locals) but has not been demonstrated end-to-end yet;
// a future framework primitive test or follow-up example could pick it up.
// See PRD §"Further Notes" for the full justification.
// ---------------------------------------------------------------------------

// Settled — no Locals. On `RawTransition` whose `pressed` differs from the
// cache's DebouncedButtonState, commit the new value to the cache and
// transition to CoolingDown.
struct Settled {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

// CoolingDown — Locals hold a Timer armed for kDebounceWindow. While
// CoolingDown is active, further RawTransition envelopes are ignored. On
// timer fire (DebounceExpired self-send) transition back to Settled; the
// Timer is destructed along with the Locals.
struct CoolingDown {
    struct Locals {
        cortexflow::Timer timer;
        Locals();
    };
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

// Debouncer — owns DebouncedButtonState. Drives a two-state lockout flow
// (Settled, CoolingDown). Receiver-owned message types per ADR-0020:
// `RawTransition` is what ButtonReader's foreign-thread driver posts on the
// boundary's behalf; `DebounceExpired` is self-sent by CoolingDown's
// state-local Timer.
//
// Inbox routing: every envelope is forwarded into `flow.step` from the
// module-level `handle` override — the same pattern minimal_app::Consumer
// uses, justified the same way (state handlers, not the dispatch table,
// route by payload type).
class Debouncer : public cortexflow::Module<Debouncer> {
public:
    // Receiver-owned message types (ADR-0020). RawTransition is posted by
    // whatever drives the boundary: the stdin thread in the host binary, or
    // the integration test. DebounceExpired is the self-sent timer payload
    // armed by CoolingDown.Locals.
    struct RawTransition {
        bool pressed;
    };
    struct DebounceExpired {};

    using Inbox = std::tuple<>;

    cortexflow::Flow<Debouncer,
                     cortexflow::StateList<Settled, CoolingDown>> flow;

    void on_start() override;
    void handle(cortexflow::Envelope& env) override;
};

}  // namespace button_pipeline
