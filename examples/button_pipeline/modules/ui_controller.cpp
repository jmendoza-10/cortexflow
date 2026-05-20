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
// Idle — write UiMode::Idle on entry. Click drives the UI active; LongPress
// drives it into Configuring; DoubleClick is a no-op in this state.
// ---------------------------------------------------------------------------

UiController::Idle::Locals::Locals() {
    cache().set<UiMode_Key>(UiMode::Idle);
}

cortexflow::StateDirective UiController::Idle::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Click>()) {
        return cortexflow::transition_to<Active>();
    }
    if (env.payload_type_id() == cortexflow::type_id<LongPress>()) {
        return cortexflow::transition_to<Configuring>();
    }
    // DoubleClick / synthetic init envelope: stay.
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Active — write UiMode::Active on entry. Click returns to Idle; LongPress
// drives into Configuring; DoubleClick stays.
// ---------------------------------------------------------------------------

UiController::Active::Locals::Locals() {
    cache().set<UiMode_Key>(UiMode::Active);
}

cortexflow::StateDirective UiController::Active::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Click>()) {
        return cortexflow::transition_to<Idle>();
    }
    if (env.payload_type_id() == cortexflow::type_id<LongPress>()) {
        return cortexflow::transition_to<Configuring>();
    }
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Configuring — write UiMode::Configuring on entry. A second LongPress
// returns to Idle; Click / DoubleClick are observable but inert (the design
// guarantees no-op gestures do not change UiMode while configuring).
// ---------------------------------------------------------------------------

UiController::Configuring::Locals::Locals() {
    cache().set<UiMode_Key>(UiMode::Configuring);
}

cortexflow::StateDirective UiController::Configuring::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<LongPress>()) {
        return cortexflow::transition_to<Idle>();
    }
    return cortexflow::stay();
}

}  // namespace button_pipeline
