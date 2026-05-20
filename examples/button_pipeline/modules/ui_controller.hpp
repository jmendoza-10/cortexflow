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
// nested members here in slice 03 even though only `Click` is sent in this
// slice. Declaring `DoubleClick` and `LongPress` up-front keeps the message
// vocabulary stable across the next two slices, which add the *senders* and
// the *state-handling* code, not the type declarations.
//
// Inbox lists all three so the compile-time `Module::send<>` validators
// accept calls from ClickClassifier in this slice and the next two. Every
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
    // Receiver-owned gesture messages (ADR-0020). All three declared in
    // slice 03 so the Inbox vocabulary is stable; only `Click` is sent in
    // this slice — `DoubleClick` and `LongPress` get their senders in
    // later slices.
    struct Click {};
    struct DoubleClick {};
    struct LongPress {};

    // Idle — UiMode_Key == UiMode::Idle on entry. Transitions: Click →
    // Active; DoubleClick / LongPress → stay (no-op gesture is observable
    // but inert, per the PRD's design guarantee).
    struct Idle {
        struct Locals {
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // Active — UiMode_Key == UiMode::Active on entry. Transitions: Click
    // → Idle; DoubleClick / LongPress → stay.
    struct Active {
        struct Locals {
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    using Inbox = std::tuple<Click, DoubleClick, LongPress>;

    cortexflow::Flow<UiController,
                     cortexflow::StateList<Idle, Active>> flow;

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
