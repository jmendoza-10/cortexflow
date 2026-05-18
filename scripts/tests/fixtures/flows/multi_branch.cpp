#include "multi_branch.hpp"

namespace fixture {

cortexflow::StateDirective Dispatch::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Dispatch::ToA>()) {
        return cortexflow::transition_to<AState>();
    }
    if (env.payload_type_id() == cortexflow::type_id<Dispatch::ToB>()) {
        return cortexflow::transition_to<BState>();
    }
    if (env.payload_type_id() == cortexflow::type_id<Dispatch::Halt>()) {
        return cortexflow::done();
    }
    return cortexflow::stay();
}

cortexflow::StateDirective AState::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope&) {
    return cortexflow::stay();
}

cortexflow::StateDirective BState::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope&) {
    return cortexflow::stay();
}

}  // namespace fixture
