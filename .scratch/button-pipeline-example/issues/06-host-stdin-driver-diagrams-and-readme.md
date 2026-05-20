# Host stdin driver, committed diagrams, and README polish

Status: ready-for-agent

## Parent

[PRD: button_pipeline](../PRD.md)

## What to build

Turn the `button_pipeline` binary into a genuinely interactive demo, commit its generated diagrams, and update the documentation so a first-time reader can find the new example and run everything CI runs.

### Host stdin driver

`main.cpp` is replaced. The new main:

1. Constructs `button_pipeline::App app;` (default `SteadyClock`).
2. Calls `app.start()`.
3. Spawns one detached-but-joinable reader thread that reads characters from `std::cin` in a loop. Each character maps to a `Debouncer::RawTransition` envelope posted via `app.post(...)`. A simple scheme: `'d'` (down) → `RawTransition{pressed=true}`, `' '` or `'u'` (up) → `RawTransition{pressed=false}`. Anything else is ignored. EOF (Ctrl-D) breaks the loop and calls `app.stop()`.
4. Calls `app.run()` on the main thread; this blocks until either the reader thread calls `app.stop()` (on EOF) or the user sends SIGINT (a small signal handler installed at startup calls `app.stop()`).
5. Joins the reader thread.
6. Calls `app.shutdown()`.

The envelope posted from the reader thread is constructed with `cortexflow::make_message<Debouncer::RawTransition>(...)` and `cortexflow::Envelope{cortexflow::kNoSender, cortexflow::type_id<Debouncer>(), std::move(msg_ptr)}`, then handed to `app.post(std::move(env))`. This is the documented "callable from any thread (including foreign boundary-module threads)" path from `runtime.hpp`.

Under the default `SteadyClock` the Debouncer's lockout timer and the Classifier's long-press / double-click timers do not fire on host in v1 (see the existing note in `minimal_app/main.cpp` and `architecture.md §9` about no real-time backend wired in). The binary therefore demonstrates the *receive path* fully — envelopes arrive, the cache changes, the Flow transitions — but the timer-fired gesture completion will only be observed under a future real-time backend or in tests under `ManualClock`. The binary's `main.cpp` carries a header comment explaining this, mirroring `minimal_app`'s analogous note.

### Diagrams

`scripts/gen-diagrams.py` is invoked against `examples/button_pipeline/app.hpp`. The two generated Mermaid diagram files (one flow diagram, one modules diagram) land under `docs/diagrams/flows/` and `docs/diagrams/modules/` matching the layout used for `minimal_app`. Both files are committed.

### READMEs

The example's own README at `examples/button_pipeline/README.md` is written. It contains:

- A top-of-file ASCII diagram of the four-module pipeline showing what travels between modules (messages vs cache fan-outs).
- A short "how it runs" walkthrough mirroring the structure of `minimal_app/README.md`.
- A paragraph titled "Messages vs cache: what travels where" that articulates the channel-choice rule of thumb (messages for events, cache for levels/state), citing the Debouncer's split (raw transitions = message, debounced state = cache) as the canonical illustration.
- A "Driving the example" section explaining both paths: stdin in the binary, `ManualClock::advance` + `app.post` in tests.
- A "Where to look in the framework" table mirroring `minimal_app/README.md`'s structure, with rows pointing at `Flow::step`, `Cache::subscribe`, `TimerService::arm`, and `runtime.post` for foreign-thread input.

The root `README.md` is updated in two places:

1. The "Where to start" section gains a one-line "which example should I read?" cue that distinguishes `minimal_app` (smallest end-to-end) from `button_pipeline` (multi-stage pipeline, multiple Flows, two cache keys, hardware-adjacent).
2. The "Run everything CI runs" snippet's final line is extended to invoke `gen-diagrams.py` against `examples/button_pipeline/app.hpp` in addition to `minimal_app/app.hpp`. The `git diff --exit-code docs/diagrams/` check at the end remains a single line — it already covers the whole directory.

## Acceptance criteria

- [ ] `examples/button_pipeline/main.cpp` spawns one reader thread that posts `Debouncer::RawTransition` envelopes via `app.post(...)`, blocks on `app.run()` in main, joins the reader thread on exit, then `app.shutdown()`.
- [ ] EOF on stdin triggers `app.stop()`; SIGINT also triggers `app.stop()` via a small signal handler installed in `main()`.
- [ ] The character → transition mapping is documented in a header comment in `main.cpp` (`'d'` → press, `' '`/`'u'` → release, others ignored, EOF → stop).
- [ ] `main.cpp` header comment notes the same SteadyClock / real-time-backend caveat that `minimal_app/main.cpp` carries.
- [ ] `examples/button_pipeline/README.md` exists with: top diagram, walkthrough, "Messages vs cache" paragraph, "Driving the example" section, "Where to look in the framework" table.
- [ ] Root `README.md` "Where to start" gains the "which example?" cue distinguishing the two examples.
- [ ] Root `README.md` "Run everything CI runs" snippet is extended to invoke `gen-diagrams.py` against `examples/button_pipeline/app.hpp` in addition to the `minimal_app` invocation. The chained `git diff --exit-code docs/diagrams/` remains correct.
- [ ] Generated Mermaid diagram files for `button_pipeline` are committed under `docs/diagrams/flows/` and `docs/diagrams/modules/` (matching the layout `minimal_app`'s diagrams use).
- [ ] The drift guard line — running `gen-diagrams.py` for both examples and checking `git diff --exit-code docs/diagrams/` — reports no diff on a clean checkout.
- [ ] All existing tests still pass; build clean under both `host` and `posix` targets.
- [ ] `pytest` still passes (the diagram-tooling tests under `scripts/tests/` are not perturbed by adding a second target).

## Blocked by

- [05-add-double-click-branch](05-add-double-click-branch.md)
