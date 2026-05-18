#pragma once

#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

namespace fixture {

struct Counter { using value_type = int; };

class TwoState;

struct Idle {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

struct Processing {
    struct Tick {};
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

class TwoState : public cortexflow::Module<TwoState> {
public:
    cortexflow::Flow<TwoState, cortexflow::StateList<Idle, Processing>> flow;
};

}  // namespace fixture
