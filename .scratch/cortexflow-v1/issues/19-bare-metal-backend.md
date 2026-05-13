# Bare-metal backend: slab pool allocator, critical-section queue, RTT/SWO trace sink

Status: ready-for-human
PRD: `docs/prd.md` — Platform portability / allocation strategy; user stories 13, 44, 45

## HITL gate

Before implementation can start, several decisions are required:

- **Target MCU family** — Cortex-M0/M3/M4/M7/M33? Different cores affect critical-section primitives, atomic guarantees, and WFI behavior.
- **Per-type slab pool sizing convention** — how does a user declare pool size per message type? (Suggested: a `using PoolSize = std::integral_constant<size_t, N>;` member or a separate `Pool<MsgT, N>` declaration in `Config`.) The PRD says pool overflow is a `FRAMEWORK_ASSERT`; this decision shapes the user-facing surface.
- **Trace transport** — RTT (Segger), SWO (ARM), UART, or all three swappable?

## What to build

A `platform/bare_metal/` backend tree:

- **Slab pool allocator** — per-type compile-time slab pools. Sizes declared in `Config` at composition. Overflow → `FRAMEWORK_ASSERT`. Lock primitives use `__disable_irq` / restore for foreign-context use.
- **Queue** — critical-section guarded ring buffer; `WFI` on idle in `run()`.
- **TimerBackend** — typically a hardware timer ISR pushing expired messages into the queue.
- **TraceSink** — pluggable RTT/SWO/UART implementations; defaults configurable per build.

`FRAMEWORK_TARGET=bare_metal` selects this backend. Foreign "thread" model is ISR context; the allocator and queue must be safe to call from ISR.

## Acceptance criteria

- [ ] HITL decisions recorded in ADRs (target MCU, pool-size declaration syntax, trace transport choice)
- [ ] `platform/bare_metal/` tree with slab pool allocator, critical-section queue, ISR-safe `post()`, hardware-timer-backed `TimerBackend`, trace sink
- [ ] `cmake/targets/bare_metal.cmake` and the toolchain file for the chosen MCU
- [ ] Slab pool overflow `FRAMEWORK_ASSERT` exercised by a unit test
- [ ] ISR-context `post()` exercised by a unit test (simulated via a critical-section helper)
- [ ] Minimal app builds for the chosen MCU; runs on hardware or a cycle-accurate sim (CI strategy decided in HITL)
- [ ] `FRAMEWORK_BUILD_TESTS=OFF` confirmed working for cross-compile builds

## Blocked by

- `16-platform-typedef-swap-posix.md`
