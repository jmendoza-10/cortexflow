#include "producer.hpp"

#include "../app.hpp"
#include "../keys.hpp"

namespace minimal_app {

void Producer::on_start() {
    // Seed the system with one Bump so `app.run()` has work to do without
    // any external driver. In a real composition a boundary module (sensor,
    // socket, CAN adapter) would generate the first input.
    send<Producer>(Bump{});
}

void Producer::on(Bump&) {
    ++counter_;
    cache().set<Counter>(counter_);
}

void Producer::on(Done&) {
    ++acks_;
    // Continuous-loop pattern: every Done re-arms the cycle so the demo's
    // `app.run()` keeps progressing. Integration tests observe this by
    // stepping the ManualClock for the Processing-timer delay between
    // run_one() calls.
    send<Producer>(Bump{});
}

}  // namespace minimal_app
