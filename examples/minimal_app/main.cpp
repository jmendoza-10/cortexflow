// Minimal cortexflow example — the canonical three-line lifecycle (PRD US 3).
//
// Under the default SteadyClock the Consumer's Processing-state timer never
// fires (v1 only ManualClock drives timer expiry — see docs/architecture.md
// §9 and timer.hpp). The binary will therefore block in `run()` after the
// first Idle→Processing transition; in production a real-time timer backend
// (FreeRTOS / bare-metal) would replace SteadyClock and make this loop
// progress autonomously. The integration tests under tests/integration/ use
// ManualClock so the end-to-end behavior can be observed deterministically.

#include "app.hpp"

int main() {
    minimal_app::App app;
    app.start();
    app.run();
    app.shutdown();
    return 0;
}
