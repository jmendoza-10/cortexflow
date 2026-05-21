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

namespace button_pipeline {

// ClickClassifier — observes `DebouncedButtonState` and emits a gesture
// message to UiController. After slice 05 the full state machine is in
// place: a clean press + release followed by the double-click window
// elapsing yields a `Click`; a press held past `kLongPressThreshold` yields
// `LongPress`; a press-release-press-release within `kDoubleClickWindow`
// yields `DoubleClick`.
//
// Every state's Locals holds a `Subscription` to `DebouncedButtonState` so
// the cache's `KeyChanged<>` fan-out reaches the active state regardless of
// which one is current. The subscription is reconstructed on every
// transition — destructing the outgoing Locals releases the slot
// synchronously and the incoming Locals constructor takes a fresh one.
// `subscriber_count()` therefore stays at exactly 1 across the lifetime of
// the flow (scenario 6 pins this invariant).
//
// Module-level `handle` overrides the framework dispatch table and forwards
// to `flow.step` (same pattern as Debouncer / Consumer). The two
// exceptions are the timer-fire payloads `DoubleClickExpired` and
// `LongPressExpired`: the module intercepts each one to call the
// corresponding `send<UiController>(...)` before forwarding into the
// flow, because state handlers are static and cannot reach the `send<>`
// machinery.
class ClickClassifier;

class ClickClassifier : public cortexflow::Module<ClickClassifier> {
public:
    // Receiver-owned timer-fire payloads (ADR-0020). These are the
    // envelopes the state-locals Timers post back to this module when
    // their respective deadlines elapse. `LongPressExpired` is armed by
    // `Pressed.Locals`; `DoubleClickExpired` by `AwaitingSecondClick.Locals`.
    struct LongPressExpired {};
    struct DoubleClickExpired {};

    // Idle — waiting for the first leading edge. Subscription kept alive
    // by Locals; on `KeyChanged<DebouncedButtonState>` whose `new_value`
    // is true, transition to Pressed.
    struct Idle {
        struct Locals {
            cortexflow::Subscription sub;
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // Pressed — button is held. Locals re-arm the subscription and arm a
    // `LongPressExpired` timer for `kLongPressThreshold`. On `KeyChanged<>`
    // whose `new_value` is false (release), transition to
    // AwaitingSecondClick — destructing the Locals cancels the long-press
    // timer in the same step.
    struct Pressed {
        struct Locals {
            cortexflow::Subscription sub;
            cortexflow::Timer long_press_timer;
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // AwaitingSecondClick — button just released; waiting out the
    // double-click window. Locals re-arm the subscription and arm a
    // `DoubleClickExpired` timer for `kDoubleClickWindow`. On timer fire,
    // the module-level `handle` sends `UiController::Click{}` and the
    // state returns `transition_to<Idle>()`. On a second leading edge
    // (`KeyChanged<>` whose `new_value` is true) arriving within the
    // window, transition to `SecondPressed` — the Locals dtor cancels the
    // double-click timer in the same step.
    struct AwaitingSecondClick {
        struct Locals {
            cortexflow::Subscription sub;
            cortexflow::Timer double_click_timer;
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    // SecondPressed — second press is held during the double-click window.
    // Locals re-arm only the subscription (no timer: a held second press
    // is bounded by the user releasing it, not by a deadline). On
    // `KeyChanged<>` whose `new_value` is false (the second release),
    // send `UiController::DoubleClick{}` and transition to `Idle`. There
    // is no long-press timer in this branch: the PRD treats a
    // press-release-press-hold as a double-click, not as a long-press.
    struct SecondPressed {
        struct Locals {
            cortexflow::Subscription sub;
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    using Inbox = std::tuple<>;
    // `TraceTypes` declares the payload types the DISPATCH trace should
    // recognise by name for this module (Debouncer/keys.hpp documents
    // the same convention for the other flow-driven modules in this
    // example).
    using TraceTypes = std::tuple<
        LongPressExpired, DoubleClickExpired,
        cortexflow::KeyChanged<DebouncedButtonState>>;

    cortexflow::Flow<ClickClassifier,
                     cortexflow::StateList<Idle, Pressed, AwaitingSecondClick,
                                           SecondPressed>> flow;

    void on_start() override;
    void handle(cortexflow::Envelope& env) override;
};

}  // namespace button_pipeline
