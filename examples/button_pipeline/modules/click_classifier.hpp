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
// message to UiController. Slice 03 implements only the single-click
// branch: a clean press + release followed by the double-click window
// elapsing yields a `Click`. Long-press and double-click branches arrive
// in slice 04.
//
// Every state's Locals holds a `Subscription` to `DebouncedButtonState` so
// the cache's `KeyChanged<>` fan-out reaches the active state regardless of
// which one is current. The subscription is reconstructed on every
// transition — destructing the outgoing Locals releases the slot
// synchronously and the incoming Locals constructor takes a fresh one.
// `subscriber_count()` therefore stays at exactly 1 across the lifetime of
// the flow (slice 03's scenario 6 pins this invariant).
//
// Module-level `handle` overrides the framework dispatch table and forwards
// to `flow.step` (same pattern as Debouncer / Consumer). The one exception
// is `DoubleClickExpired`: the module intercepts it to call
// `send<UiController>(Click{})` before forwarding into the flow, because
// the state handler is static and cannot reach the `send<>` machinery.
class ClickClassifier;

class ClickClassifier : public cortexflow::Module<ClickClassifier> {
public:
    // Receiver-owned timer-fire payloads (ADR-0020). These are the
    // envelopes the state-locals Timers post back to this module when
    // their respective deadlines elapse.
    //
    // `LongPressExpired` is armed in `Pressed.Locals` but its handler is
    // wired in slice 04; in this slice the timer is armed purely to
    // exercise its RAII lifecycle through the press/release sequence.
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
    // state returns `transition_to<Idle>()` so the flow drops back to
    // waiting for a new leading edge.
    struct AwaitingSecondClick {
        struct Locals {
            cortexflow::Subscription sub;
            cortexflow::Timer double_click_timer;
            Locals();
        };
        static cortexflow::StateDirective handle(
            cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
    };

    using Inbox = std::tuple<>;

    cortexflow::Flow<ClickClassifier,
                     cortexflow::StateList<Idle, Pressed,
                                           AwaitingSecondClick>> flow;

    void on_start() override;
    void handle(cortexflow::Envelope& env) override;
};

}  // namespace button_pipeline
