// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tuple>

#include <cortexflow/cache.hpp>
#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

#include "../keys.hpp"

namespace button_pipeline {

// UiController — owner of the `UiMode_Key` cache key. Receives gesture
// messages from ClickClassifier and publishes the resulting `UiMode` to the
// cache as the side effect of state entry (PRD "side-effect on entry"
// pattern).
//
// Receiver-owned messages: all three gesture types are declared as `public`
// nested members here. The Inbox lists all three so the compile-time
// `Module::send<>` validators accept calls from ClickClassifier. Every
// envelope is routed through `flow.step` from the module-level `handle`
// override — the same pattern Debouncer and Consumer use, justified the
// same way (state handlers, not the dispatch table, route by payload type).
//
// State Locals are intentionally empty structs whose *constructors* perform
// the cache write: writing on construction makes the new UiMode visible
// before the state begins consuming envelopes, and Locals destruction on
// transition is a no-op (no resource to release — the cache slot is owned
// by the runtime, not the Locals).
class UiController;

class UiController : public cortexflow::Module<UiController> {
public:
    // Receiver-owned gesture messages (ADR-0020). `Click` is sent on the
    // single-click branch (slice 03), `LongPress` on the long-press branch
    // (slice 04); `DoubleClick` gets its sender in slice 05.
    struct Click {};
    struct DoubleClick {};
    struct LongPress {};

    // Idle — UiMode_Key == UiMode::Idle on entry. Transitions: Click →
    // Active; LongPress → Configuring; DoubleClick → stay.
    struct Idle {
        struct Locals {
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // Active — UiMode_Key == UiMode::Active on entry. Transitions: Click
    // → Idle; LongPress → Configuring; DoubleClick → stay.
    struct Active {
        struct Locals {
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // Configuring — UiMode_Key == UiMode::Configuring on entry.
    // Transitions: LongPress → Idle; Click → stay; DoubleClick → stay.
    struct Configuring {
        struct Locals {
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    using Inbox = std::tuple<Click, DoubleClick, LongPress>;

    cortexflow::Flow<UiController,
                     cortexflow::StateList<Idle, Active, Configuring>> flow;

    void on_start() override;
    void handle(cortexflow::Envelope& env) override;

    // Inbox-driven dispatch-handler stubs. The framework's `Module<>`
    // eagerly instantiates a `dispatch_handler<Derived, Msg>` for every
    // type listed in `Inbox` (it must, to populate the constexpr dispatch
    // table for the vtable's `Module::handle` slot — even though our
    // `handle` override never reaches that table). Each handler resolves
    // `Derived::on(Msg&)`, so non-empty Inbox + flow.step-style routing
    // requires these stubs to exist. The bodies are intentionally empty:
    // the routing happens in `handle` via `flow.step(env)` above; the
    // dispatch table is dead code.
    void on(Click&) noexcept {}
    void on(DoubleClick&) noexcept {}
    void on(LongPress&) noexcept {}
};

}  // namespace button_pipeline
