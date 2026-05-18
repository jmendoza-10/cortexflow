#pragma once

#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

namespace fixture {

class Router;

struct Dispatch {
    struct ToA {};
    struct ToB {};
    struct Halt {};
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

struct AState {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

struct BState {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

class Router : public cortexflow::Module<Router> {
public:
    cortexflow::Flow<Router,
                     cortexflow::StateList<Dispatch, AState, BState>> flow;
};

}  // namespace fixture
