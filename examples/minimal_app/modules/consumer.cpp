#include "consumer.hpp"

#include <cortexflow/type_name.hpp>

#include "../app.hpp"
#include "../keys.hpp"
#include "producer.hpp"

namespace minimal_app {

// ---------------------------------------------------------------------------
// Consumer module
// ---------------------------------------------------------------------------

void Consumer::on_start() {
    flow.start(*this);
}

void Consumer::handle(cortexflow::Envelope& env) {
    // Intercept ProcessingTick at the module level so we can send Done back
    // to Producer using the typed `send<>` machinery (state handlers are
    // static and have no Consumer reference). flow.step still runs below
    // and lets Processing transition back to Idle on the same envelope.
    if (env.payload_type_id() ==
        cortexflow::type_id<ProcessingTick>()) {
        send<Producer>(Producer::Done{});
    }
    flow.step(env);
}

// ---------------------------------------------------------------------------
// Idle — locals hold the Counter subscription. While Idle is active,
// KeyChanged<Counter> envelopes arrive through the runtime queue (PRD US 27).
// ---------------------------------------------------------------------------

Idle::Locals::Locals()
    : sub(cache().subscribe<Counter, Consumer>()) {}

cortexflow::StateDirective Idle::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<cortexflow::KeyChanged<Counter>>()) {
        return cortexflow::transition_to<Processing>();
    }
    // Init envelope (from = kSystemSender) and anything else: stay.
    return cortexflow::stay();
}

// ---------------------------------------------------------------------------
// Processing — locals arm a Timer that fires ProcessingTick after the
// configured delay. The Timer is held by value, so a transition out of
// Processing cancels it automatically.
// ---------------------------------------------------------------------------

Processing::Locals::Locals()
    : timer(timers().arm<Consumer>(kProcessingDelay, Consumer::ProcessingTick{})) {}

cortexflow::StateDirective Processing::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<Consumer::ProcessingTick>()) {
        return cortexflow::transition_to<Idle>();
    }
    return cortexflow::stay();
}

}  // namespace minimal_app
