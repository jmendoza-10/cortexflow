// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#include "ui_controller.hpp"

#include <cortexflow/type_name.hpp>

#include "../app.hpp"
#include "../keys.hpp"

namespace button_pipeline {

// ---------------------------------------------------------------------------
// UiController module
// ---------------------------------------------------------------------------

void UiController::on_start() {
    flow.start(*this);
}

void UiController::handle(cortexflow::Envelope& env) {
    flow.step(env);
}

// ---------------------------------------------------------------------------
// Idle — write UiMode::Idle on entry. Click drives the UI active; the other
// two gestures are no-ops in this state.
// ---------------------------------------------------------------------------

UiController::Idle::Locals::Locals() {
    cache().set<UiMode_Key>(UiMode::Idle);
}

cortexflow::StateDirective UiController::Idle::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Click>()) {
        return cortexflow::transition_to<Active>();
    }
    // DoubleClick / LongPress / synthetic init envelope: stay. The two
    // unhandled gestures are listed in the Inbox so future slices' senders
    // type-check; their effect lands when those slices wire the transitions.
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Active — write UiMode::Active on entry. Click returns to Idle; the other
// two gestures stay (slice 04 wires LongPress to Configuring).
// ---------------------------------------------------------------------------

UiController::Active::Locals::Locals() {
    cache().set<UiMode_Key>(UiMode::Active);
}

cortexflow::StateDirective UiController::Active::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Click>()) {
        return cortexflow::transition_to<Idle>();
    }
    return cortexflow::stay();
}

}  // namespace button_pipeline
