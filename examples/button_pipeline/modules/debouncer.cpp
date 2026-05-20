// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#include "debouncer.hpp"

#include <cortexflow/type_name.hpp>

#include "../app.hpp"
#include "../keys.hpp"

namespace button_pipeline {

// ---------------------------------------------------------------------------
// Debouncer module
// ---------------------------------------------------------------------------

void Debouncer::on_start() {
    flow.start(*this);
}

void Debouncer::handle(cortexflow::Envelope& env) {
    flow.step(env);
}

// ---------------------------------------------------------------------------
// Settled — commit on the first edge whose value differs from what the cache
// already holds, then transition to CoolingDown to start the lockout window.
// A leading-edge commit avoids the redundant-fanout cost since Cache::set
// short-circuits compare-equal writes anyway, but checking the cached value
// here keeps the "wrote the new edge" observable cleanly tied to "transitioned
// to CoolingDown."
// ---------------------------------------------------------------------------

cortexflow::StateDirective Settled::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<Debouncer::RawTransition>()) {
        const bool pressed = env.payload<Debouncer::RawTransition>().pressed;
        const bool current =
            cache().get<DebouncedButtonState>().value_or(false);
        if (pressed != current) {
            cache().set<DebouncedButtonState>(pressed);
            return cortexflow::transition_to<CoolingDown>();
        }
        return cortexflow::stay();
    }
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// CoolingDown — Locals arm a one-shot Timer for kDebounceWindow that posts
// DebounceExpired back to Debouncer. The Timer is held by value, so the
// transition out of CoolingDown destructs it and the underlying heap entry
// is cancelled. In this design the Timer always fires (no further raw
// transitions can short-circuit the wait), but the RAII pattern is the same
// one the rest of the framework uses for state-local resources.
// ---------------------------------------------------------------------------

CoolingDown::Locals::Locals()
    : timer(timers().arm<Debouncer>(kDebounceWindow,
                                    Debouncer::DebounceExpired{})) {}

cortexflow::StateDirective CoolingDown::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<Debouncer::DebounceExpired>()) {
        return cortexflow::transition_to<Settled>();
    }
    // RawTransition (or anything else) arriving inside the lockout window
    // is intentionally ignored — that is the whole point of the pattern.
    return cortexflow::stay();
}

}  // namespace button_pipeline
