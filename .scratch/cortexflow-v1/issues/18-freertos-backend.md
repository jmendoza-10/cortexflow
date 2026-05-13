# FreeRTOS backend: allocator, timer, queue, trace sink

Status: ready-for-human
PRD: `docs/prd.md` — Platform portability; user stories 44, 45

## HITL gate

Before implementation can start, a target sim/board choice is required:

- **QEMU + FreeRTOS** (e.g., the Cortex-M3 reference port)?
- **A specific MCU family** with native FreeRTOS support (STM32, NXP, etc.)?
- **POSIX FreeRTOS port** (good for CI; runs on Linux)?

The choice changes how integration tests run and what CI infrastructure is needed. Pick one as the v1 target; others can follow.

## What to build

A `platform/freertos/` backend tree implementing the four facility types:

- **Allocator** — heap or pool routed through FreeRTOS-aware allocation (configurable; default to FreeRTOS heap_4 or equivalent).
- **TimerBackend** — uses `xTimerCreate`/`xTimerStart` or a single-thread tick loop, mapped to the framework's `TimerService` semantics.
- **Queue** — native `xQueue` instead of `std::mutex` + CV. The runtime's `post()` becomes a wrapped `xQueueSend(ToFront/Back)FromISR`/`xQueueSend` depending on caller context.
- **TraceSink** — typically `vTaskList`-friendly logging or a per-target UART.

`FRAMEWORK_TARGET=freertos` selects this backend. The same module code from the host/posix builds compiles unchanged.

## Acceptance criteria

- [ ] HITL decision recorded in an ADR (e.g., `docs/adr/0011-freertos-target-choice.md`)
- [ ] `platform/freertos/` tree with the four facility implementations
- [ ] `cmake/targets/freertos.cmake` and any required toolchain file
- [ ] Minimal app builds and runs on the chosen target (in CI if a sim was selected)
- [ ] At least one integration test verifies foreign-thread `post()` from a FreeRTOS task
- [ ] No core framework code changed by this backend addition

## Blocked by

- `16-platform-typedef-swap-posix.md`
