#include "initial_tag.hpp"

namespace fixture {

cortexflow::StateDirective First::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<First::Go>()) {
        return cortexflow::transition_to<Second>();
    }
    return cortexflow::stay();
}

cortexflow::StateDirective Second::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope&) {
    return cortexflow::stay();
}

}  // namespace fixture
