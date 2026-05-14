// This file must NOT compile successfully.
// Verifies the call-site static_assert that catches `send<Target>(msg)` where
// `Target` is not declared in the module's `ModuleList`. The check is opt-in:
// a module passes its `ModuleList` as the second template argument to
// `Module<>` to enable it.
//
// Expected substring: "Target is not declared in ModuleList"

#include <cortexflow/module.hpp>
#include <cortexflow/runtime.hpp>

#include <tuple>

struct ChargeController;
struct BatteryManager;
struct UnregisteredSensor;

// AppList intentionally omits UnregisteredSensor.
using AppList = cortexflow::ModuleList<ChargeController, BatteryManager>;

struct Wakeup {};

struct UnregisteredSensor : cortexflow::Module<UnregisteredSensor> {
    using Inbox = std::tuple<Wakeup>;
    void on(Wakeup&) {}
};

struct BatteryManager : cortexflow::Module<BatteryManager, AppList> {
    using Inbox = std::tuple<>;
};

struct ChargeController : cortexflow::Module<ChargeController, AppList> {
    using Inbox = std::tuple<Wakeup>;
    void on(Wakeup&) {
        // UnregisteredSensor handles Wakeup, so the handler-check passes — the
        // failure must come from the ModuleList membership check.
        send<UnregisteredSensor>(Wakeup{});
    }
};

// Force ChargeController::on (and therefore send<UnregisteredSensor>) to be
// instantiated. The static_assert fires here.
void instantiate() {
    ChargeController c;
    Wakeup w;
    c.on(w);
}
