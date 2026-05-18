#pragma once

#include <cortexflow/flow.hpp>
#include <cortexflow/module.hpp>

namespace fixture {

class TaggedModule;

struct First {
    struct Go {};
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

struct Second {
    static cortexflow::StateDirective handle(
        cortexflow::FlowCtx& ctx, cortexflow::Envelope& env);
};

// `InitialStateTag` overrides StateList::head — the flow starts in Second
// even though First appears first in the StateList.
class TaggedModule : public cortexflow::Module<TaggedModule> {
public:
    cortexflow::Flow<TaggedModule,
                     cortexflow::StateList<First, Second>,
                     Second> flow;
};

}  // namespace fixture
