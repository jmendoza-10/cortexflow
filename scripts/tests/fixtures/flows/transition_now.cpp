#include "transition_now.hpp"

namespace fixture {

cortexflow::StateDirective Loading::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Loading::Ready>()) {
        return cortexflow::transition_to_now<Active>();
    }
    return cortexflow::stay();
}

cortexflow::StateDirective Active::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope&) {
    return cortexflow::stay();
}

}  // namespace fixture
