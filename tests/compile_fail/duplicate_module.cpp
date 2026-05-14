// This file must NOT compile successfully.
// Verifies the Runtime-level static_assert that catches a duplicate module
// type declared in `ModuleList`.
//
// Expected substring: "duplicate module type declared in ModuleList"

#include <cortexflow/module.hpp>
#include <cortexflow/runtime.hpp>

#include <tuple>

struct ChargeController : cortexflow::Module<ChargeController> {
    using Inbox = std::tuple<>;
};

struct BatteryManager : cortexflow::Module<BatteryManager> {
    using Inbox = std::tuple<>;
};

// ChargeController appears twice in ModuleList — this must be rejected.
using App = cortexflow::Runtime<
    cortexflow::ModuleList<ChargeController, BatteryManager, ChargeController>,
    cortexflow::CacheKeyList<>,
    cortexflow::Config<>>;

void instantiate() {
    App app;
    (void)app;
}
