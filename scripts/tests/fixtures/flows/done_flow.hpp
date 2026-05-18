#pragma once

#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

namespace fixture {

class DoneModule;

struct Working {
    struct Shutdown {};
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

class DoneModule : public cortexflow::Module<DoneModule> {
public:
    cortexflow::Flow<DoneModule, cortexflow::StateList<Working>> flow;
};

}  // namespace fixture
