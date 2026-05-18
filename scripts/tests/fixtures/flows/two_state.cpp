#include "two_state.hpp"

namespace fixture {

cortexflow::StateDirective Idle::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<cortexflow::KeyChanged<Counter>>()) {
        return cortexflow::transition_to<Processing>();
    }
    return cortexflow::stay();
}

cortexflow::StateDirective Processing::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() ==
        cortexflow::type_id<Processing::Tick>()) {
        return cortexflow::transition_to<Idle>();
    }
    return cortexflow::stay();
}

}  // namespace fixture
