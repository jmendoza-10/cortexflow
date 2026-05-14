// This file must NOT compile successfully.
// Verifies the call-site static_assert that catches `send<Target>(msg)` where
// `Target` is in the ModuleList but does not declare a handler for `Msg`
// (Msg is not listed in `Target::Inbox`).
//
// Expected substring: "does not declare a handler for the message type"

#include <cortexflow/module.hpp>

#include <tuple>

struct ChargingDone {};
struct BatteryLow {};

struct BatteryManager : cortexflow::Module<BatteryManager> {
    // Inbox does NOT declare ChargingDone — sending it must be rejected.
    using Inbox = std::tuple<BatteryLow>;
    void on(BatteryLow&) {}
};

struct ChargeController : cortexflow::Module<ChargeController> {
    using Inbox = std::tuple<ChargingDone>;
    void on(ChargingDone&) {
        send<BatteryManager>(ChargingDone{});
    }
};

// Force ChargeController::on (and therefore send<BatteryManager>) to be
// instantiated. The static_assert fires here.
void instantiate() {
    ChargeController c;
    ChargingDone d;
    c.on(d);
}
