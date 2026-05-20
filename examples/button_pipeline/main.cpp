// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Interactive host driver for the button_pipeline example.
//
// Character → Debouncer::RawTransition mapping (read from stdin one char at
// a time, whitespace included):
//
//     'd'        → RawTransition{pressed = true}   (press)
//     ' ', 'u'   → RawTransition{pressed = false}  (release)
//     anything   → ignored
//     EOF (^D)   → reader thread calls app.stop()
//     SIGINT     → small signal handler calls app.stop()
//
// Threading model: one reader thread reads stdin and posts envelopes to the
// Debouncer via the public `app.post(...)` surface — the documented
// "callable from any thread (including foreign boundary-module threads)"
// path on runtime.hpp. The main thread blocks in `app.run()` until either
// the reader observes EOF or SIGINT trips, at which point `app.stop()` wakes
// the loop, `run()` returns, the reader thread is joined, and `shutdown()`
// drains.
//
// Signal delivery: SIGINT is blocked in the main thread and unblocked only
// in the reader. The kernel therefore delivers the signal to the reader
// thread, which is normally parked inside `read()` for stdin. With the
// `sigaction` flags below (no `SA_RESTART`) the syscall returns EINTR, the
// C++ stream goes into a failed state, and the reader exits its loop on the
// next iteration. (If `SIGINT` were instead allowed on the main thread, a
// reader blocked in `read()` would not see EINTR and `reader.join()` below
// would deadlock.)
//
// Caveat — mirrors examples/minimal_app/main.cpp: under the default
// SteadyClock v1 has no real-time backend wired in to fire timers, so the
// Debouncer's lockout timer and the ClickClassifier's long-press /
// double-click timers will not expire on host. The binary therefore
// demonstrates the *receive path* fully (RawTransition envelopes arrive,
// the cache flips, the Classifier's flow transitions on the resulting
// fanout), but the timer-fired gesture completion will only be observed
// under a future real-time backend or in the integration tests, which use
// `ManualClock::advance` to fire timers deterministically. See
// docs/architecture.md §9 and tests/integration/test_button_pipeline.cpp.

#include <atomic>
#include <iostream>
#include <thread>
#include <utility>

#include <pthread.h>
#include <signal.h>

#include <cortexflow/messaging.hpp>

#include "app.hpp"
#include "modules/debouncer.hpp"

namespace {

// Static back-pointer to the App for the SIGINT handler. Lifetime: set
// before `app.start()` and the handler installation, cleared after
// `app.shutdown()` returns. The handler only fires while `g_app` is
// non-null.
std::atomic<button_pipeline::App*> g_app{nullptr};

extern "C" void on_sigint(int /*signo*/) {
    auto* app = g_app.load(std::memory_order_acquire);
    if (app) {
        app->stop();
    }
}

void post_raw_transition(button_pipeline::App& app, bool pressed) {
    auto msg = cortexflow::make_message<
        button_pipeline::Debouncer::RawTransition>(
            button_pipeline::Debouncer::RawTransition{pressed});
    cortexflow::Envelope env(
        cortexflow::kNoSender,
        cortexflow::type_id<button_pipeline::Debouncer>(),
        std::move(msg));
    app.post(std::move(env));
}

void reader_loop(button_pipeline::App& app) {
    // Unblock SIGINT on this thread; main blocked it before spawning us.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);

    char c;
    while (std::cin.get(c)) {
        if (c == 'd') {
            post_raw_transition(app, true);
        } else if (c == 'u' || c == ' ') {
            post_raw_transition(app, false);
        }
        // Any other character (including '\n') is ignored.
    }
    // EOF (Ctrl-D), SIGINT (read returned EINTR with no SA_RESTART), or
    // any other stream failure: wake the main loop. `stop()` is idempotent
    // and safe to call after the signal handler has already done so.
    app.stop();
}

}  // namespace

int main() {
    button_pipeline::App app;
    g_app.store(&app, std::memory_order_release);

    // Block SIGINT here so the kernel delivers it to the reader thread
    // (which unblocks it after spawning). See header comment.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    struct sigaction sa;
    sa.sa_handler = &on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART — see header comment.
    sigaction(SIGINT, &sa, nullptr);

    app.start();

    std::thread reader([&app] { reader_loop(app); });

    app.run();

    reader.join();

    app.shutdown();

    struct sigaction sa_dfl;
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);
    sa_dfl.sa_flags = 0;
    sigaction(SIGINT, &sa_dfl, nullptr);
    pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
    g_app.store(nullptr, std::memory_order_release);
    return 0;
}
