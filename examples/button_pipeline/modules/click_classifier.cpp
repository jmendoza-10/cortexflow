// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#include "click_classifier.hpp"

#include <cortexflow/cache.hpp>
#include <cortexflow/type_name.hpp>

#include "../app.hpp"
#include "../keys.hpp"
#include "ui_controller.hpp"

namespace button_pipeline {

// ---------------------------------------------------------------------------
// ClickClassifier module
// ---------------------------------------------------------------------------

void ClickClassifier::on_start() {
    flow.start(*this);
}

void ClickClassifier::handle(cortexflow::Envelope& env) {
    // Intercept the double-click-timer fire at the module level so we can
    // post `UiController::Click{}` through the typed `send<>` machinery
    // (state handlers are static and have no module reference). The state
    // handler still runs below and transitions AwaitingSecondClick back to
    // Idle on the same envelope.
    //
    // Safe to gate purely on payload type: `DoubleClickExpired` is only
    // ever armed by `AwaitingSecondClick.Locals`; transitioning out of
    // AwaitingSecondClick destructs the Timer, cancelling the seq and
    // preventing the envelope from being posted.
    if (env.payload_type_id() ==
        cortexflow::type_id<DoubleClickExpired>()) {
        send<UiController>(UiController::Click{});
    }
    flow.step(env);
}

// ---------------------------------------------------------------------------
// Idle — Locals subscribe to DebouncedButtonState. On a leading edge
// (`new_value == true`) transition to Pressed.
// ---------------------------------------------------------------------------

ClickClassifier::Idle::Locals::Locals()
    : sub(cache().subscribe<DebouncedButtonState, ClickClassifier>()) {}

cortexflow::StateDirective ClickClassifier::Idle::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<cortexflow::KeyChanged<DebouncedButtonState>>()) {
        const auto& kc =
            env.payload<cortexflow::KeyChanged<DebouncedButtonState>>();
        if (kc.new_value) {
            return cortexflow::transition_to<Pressed>();
        }
    }
    // Synthetic init envelope (from = kSystemSender) and any
    // `KeyChanged<>` with `new_value == false` (e.g. trailing edge after a
    // run that ended in Active without going through Idle's subscription
    // — impossible in this slice but worth not asserting on): stay.
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Pressed — re-subscribe to DebouncedButtonState and arm the long-press
// timer. On release (`new_value == false`) transition to
// AwaitingSecondClick; the Locals dtor cancels the long-press timer
// because it was held by value.
//
// The `LongPressExpired` envelope it would post if the button were held
// past `kLongPressThreshold` is intentionally unhandled in this slice —
// the handler arrives in slice 04 when the LongPress branch is wired. The
// timer is armed here so its RAII lifecycle is exercised by the click
// scenario (it must be cancelled cleanly on the press/release path).
// ---------------------------------------------------------------------------

ClickClassifier::Pressed::Locals::Locals()
    : sub(cache().subscribe<DebouncedButtonState, ClickClassifier>()),
      long_press_timer(timers().arm<ClickClassifier>(
          kLongPressThreshold, ClickClassifier::LongPressExpired{})) {}

cortexflow::StateDirective ClickClassifier::Pressed::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<cortexflow::KeyChanged<DebouncedButtonState>>()) {
        const auto& kc =
            env.payload<cortexflow::KeyChanged<DebouncedButtonState>>();
        if (!kc.new_value) {
            return cortexflow::transition_to<AwaitingSecondClick>();
        }
    }
    // `LongPressExpired` arrives here only if the button is held past
    // `kLongPressThreshold`; slice 04 wires its transition. For now stay
    // — the inert-but-observable no-op the PRD calls out.
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// AwaitingSecondClick — re-subscribe and arm the double-click window
// timer. On `DoubleClickExpired` the module-level `handle` has already
// fired `Click` to UiController; the state itself returns to Idle.
//
// On `KeyChanged<>` envelopes the state stays in this slice. Slice 04
// will branch a `new_value == true` arrival into a DoubleClick path.
// ---------------------------------------------------------------------------

ClickClassifier::AwaitingSecondClick::Locals::Locals()
    : sub(cache().subscribe<DebouncedButtonState, ClickClassifier>()),
      double_click_timer(timers().arm<ClickClassifier>(
          kDoubleClickWindow, ClickClassifier::DoubleClickExpired{})) {}

cortexflow::StateDirective ClickClassifier::AwaitingSecondClick::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<DoubleClickExpired>()) {
        return cortexflow::transition_to<Idle>();
    }
    return cortexflow::stay();
}

}  // namespace button_pipeline
