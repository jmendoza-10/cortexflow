# Fault path: `FRAMEWORK_ASSERT` + weak-linked platform handler

Status: ready-for-agent
PRD: `docs/prd.md` — Fault subsystem; user stories 49, 50, 51

## What to build

A single `FRAMEWORK_ASSERT(cond, reason)` macro that, on false condition, calls `[[noreturn]] fault(reason)`. `fault(...)` invokes a weak-linked platform handler so bare-metal builds can override behavior (typically: disable interrupts, log, reset). The default host handler aborts after writing the reason and source location through the trace sink.

Every framework-level error is a system invariant violation routed through this one channel — no error codes thread through `send`/`post`/`subscribe` APIs.

## Acceptance criteria

- [ ] `FRAMEWORK_ASSERT(cond, reason)` macro evaluates `cond` exactly once; reason is a `string_view`-compatible literal
- [ ] `[[noreturn]] fault(reason, source_location)` is the single sink for all assertion failures
- [ ] Platform handler is weak-linked; default host handler aborts and writes reason + location
- [ ] Override mechanism is demonstrated by a unit test that supplies an alternate strong symbol and observes it called
- [ ] Unit test that an assert in normal code triggers `fault(...)` and never returns
- [ ] Macro compiles to a single branch in optimized builds (no string allocation on the hot path)

## Blocked by

None — can start immediately.
