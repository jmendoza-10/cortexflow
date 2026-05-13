# Runtime skeleton: composition, FIFO queue, two-phase init, `run_one`/`run`/`stop`

Status: ready-for-agent
PRD: `docs/prd.md` — Runtime subsystem; user stories 1, 3, 4, 5, 6, 52, 53

## What to build

The `Runtime<ModuleList, CacheKeyList, Config>` template that owns the event loop, the single FIFO queue, module tuple storage, and lifecycle. This slice excludes the cache and timer surfaces (placeholder/empty until later slices) but wires up enough composition for end-to-end send/receive between two toy modules.

Two-phase init: construct all modules in declaration order, then call `on_start()` on each in declaration order. Modules may not interact during construction. `run()` blocks pumping the queue. `run_one()` drains the queue once and returns. `stop()` initiates graceful shutdown with a bounded drain budget; remaining handlers are skipped after budget exhaustion. `on_stop()` runs in reverse declaration order; destruction follows reverse order.

A `main()` against this slice is just `App app{…}; app.start(); app.run(); app.shutdown();` for production, or `app.start(); while(!done) app.run_one(); app.shutdown();` for tests.

## Acceptance criteria

- [ ] `Runtime<ModuleList, CacheKeyList, Config>` template with the type-level composition shape
- [ ] `start()` constructs modules in declaration order, then calls `on_start()` in declaration order
- [ ] `run()` pumps the queue blocking on a host CV when idle
- [ ] `run_one()` drains the queue exactly once and returns (correct behavior on empty queue: returns without blocking)
- [ ] `stop()` sets a stopping flag; `shutdown()` drains queue (bounded by configurable drain budget), then `on_stop()` reverse order, then destruct reverse order
- [ ] Drain budget exhaustion logs a `WARN` trace and aborts remaining drain (does not assert)
- [ ] Integration test: two toy modules exchange a message via `send`, observed via `run_one`
- [ ] Integration test: `on_start` / `on_stop` invocation order verified across three toy modules

## Blocked by

- `05-module-base-dispatch.md`
