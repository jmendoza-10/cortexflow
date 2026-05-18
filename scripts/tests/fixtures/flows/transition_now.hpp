#pragma once

#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

namespace fixture {

class NowModule;

struct Loading {
    struct Ready {};
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

struct Active {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

class NowModule : public cortexflow::Module<NowModule> {
public:
    cortexflow::Flow<NowModule, cortexflow::StateList<Loading, Active>> flow;
};

}  // namespace fixture
