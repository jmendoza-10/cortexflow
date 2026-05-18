#include "done_flow.hpp"

namespace fixture {

cortexflow::StateDirective Working::handle(
    cortexflow::FlowCtx&, cortexflow::Envelope& env) {
    if (env.payload_type_id() == cortexflow::type_id<Working::Shutdown>()) {
        return cortexflow::done();
    }
    return cortexflow::stay();
}

}  // namespace fixture
