// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Scaffold entry point for the button_pipeline example. Slice 06 replaces
// this with an stdin-driven main that posts Debouncer::RawTransition
// envelopes from a foreign reader thread. For now this binary builds, runs,
// and immediately blocks in run() — the same shape as minimal_app/main.cpp.
//
// Under SteadyClock the runtime has no work to do (no module emits anything
// from on_start in this slice) so run() blocks indefinitely on an empty
// queue. SIGINT terminates the process. Subsequent slices replace this with
// the interactive driver and a clean stop() → join() → shutdown() path.

#include "app.hpp"

int main() {
    button_pipeline::App app;
    app.start();
    app.run();
    app.shutdown();
    return 0;
}
