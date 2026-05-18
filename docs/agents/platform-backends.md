# Platform backends

How CortexFlow's platform layer is structured and how to add a new target.

## The typedef-swap mechanism

CortexFlow is portable without `#ifdef` walls in module code (PRD US 44). The
mechanism is **typedef-swap**: every facility a module names is exposed under
`cortexflow::platform::*` and resolves at build time to the implementation
the chosen backend supplies. The composition declares the system shape using
those names; the build picks the backend:

```cpp
#include <cortexflow/platform.hpp>

using App = cortexflow::Runtime<
    cortexflow::ModuleList<
        IgnitionMonitor,
        ChargeController,
        platform::CanAdapter      // typedef-swapped per target
    >,
    /* ... */>;

App rt{ platform::TimerBackend{} };  // typedef-swapped clock
```

There is no central `#if CORTEXFLOW_TARGET == ...` switch. The selection
happens by which translation units are linked and which include directory is
on the search path — both controlled by `cmake/targets/<target>.cmake`.

## The three named facilities

Every backend's `<cortexflow/platform.hpp>` exposes the same names:

| Name                         | What it is                                                                          |
|------------------------------|-------------------------------------------------------------------------------------|
| `platform::Allocator`        | A `cortexflow::MessageAllocator` subclass. The runtime's `make_message<T>(...)` calls `default_allocator()`, which the backend defines to return this. |
| `platform::TimerBackend`     | A `cortexflow::Clock` subclass. Used as the monotonic source for `TimerService`. ManualClock (in core) is the test counterpart. |
| `platform::TraceSink`        | A tag struct documenting which trace-sink implementation is linked. The actual dispatch goes through the weak `platform_trace_sink` symbol the backend defines. |

Boundary module typedefs (e.g. `platform::CanAdapter`) follow the same
pattern when each target supplies one; they aren't in scope for issue 16.

## In-tree backends

| Target  | Allocator                                | Clock                                 | Trace sink                             |
|---------|------------------------------------------|---------------------------------------|----------------------------------------|
| `host`  | `::operator new` / `delete` + `std::mutex` | `std::chrono::steady_clock`           | `fprintf(stderr, ...)` with timestamps |
| `posix` | `posix_memalign` / `free` + `pthread_mutex_t` | `clock_gettime(CLOCK_MONOTONIC)`     | `write(STDERR_FILENO, ...)` single-line records |

Both produce identical observable behaviour for the v1 surface; the choice
of primitives is the demonstration that the swap works end-to-end. FreeRTOS
and bare-metal backends are deferred to slices 18 and 19.

## Repo layout

```
platform/
├── host/
│   ├── cortexflow/platform.hpp     ← exports platform:: typedefs (host)
│   ├── heap_allocator.cpp          ← HeapAllocator + default_allocator()
│   ├── steady_clock.cpp            ← SteadyClock::now()
│   └── trace_sink.cpp              ← weak platform_trace_sink default
├── posix/
│   ├── cortexflow/platform.hpp     ← exports platform:: typedefs (POSIX)
│   ├── heap_allocator.cpp          ← HeapAllocator + default_allocator()
│   ├── steady_clock.cpp            ← SteadyClock::now()
│   └── trace_sink.cpp              ← weak platform_trace_sink default
└── (future: freertos/, bare_metal/)

cmake/targets/
├── host.cmake                      ← wires platform/host/ into cortexflow
└── posix.cmake                     ← wires platform/posix/ into cortexflow
```

Core (`include/cortexflow/`, `src/cortexflow/`) declares the classes
(`HeapAllocator`, `SteadyClock`) and the weak symbols (`platform_trace_sink`,
`platform_fault_handler`). The backend supplies the implementations.

## Adding a new platform

The contract for a new target `<X>` is two files:

1. **`platform/<X>/cortexflow/platform.hpp`** — a header that defines

       namespace cortexflow {
       namespace platform {
       using Allocator    = /* your MessageAllocator subclass */;
       using TimerBackend = /* your Clock subclass */;
       struct <X>TraceSink {};
       using TraceSink    = <X>TraceSink;
       }
       }

2. **`cmake/targets/<X>.cmake`** — adds your backend's sources to the
   cortexflow library and your `platform/<X>/` directory to the public
   include path:

       target_sources(cortexflow PRIVATE
           ${PROJECT_SOURCE_DIR}/platform/<X>/heap_allocator.cpp
           ${PROJECT_SOURCE_DIR}/platform/<X>/steady_clock.cpp
           ${PROJECT_SOURCE_DIR}/platform/<X>/trace_sink.cpp
       )
       target_include_directories(cortexflow PUBLIC
           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/platform/<X>>
       )

Implement, in those `.cpp` files:

- `HeapAllocator::allocate / deallocate / lock / unlock / instance` — the
  class is declared once in `<cortexflow/messaging.hpp>`; you supply the
  bodies using your platform's memory and locking primitives.
- `MessageAllocator& cortexflow::default_allocator()` — typically returns
  `HeapAllocator::instance()`. The runtime's `make_message<T>(...)` factory
  calls this.
- `SteadyClock::now()` — declared in `<cortexflow/clock.hpp>`; your
  implementation reads whatever monotonic source the target offers.
- `extern "C" __attribute__((weak)) void platform_trace_sink(...)` — weak
  so tests and applications can override with a strong symbol. Default
  behaviour should be the most useful one-line-per-event sink the target
  can offer.

Optionally:
- A strong (or weak) `platform_fault_handler(...)` — the core ships a weak
  stderr-and-abort default in `src/cortexflow/fault.cpp`. Bare-metal
  targets override this to disable interrupts and reset (architecture §13.2).

Build with `cmake -DCORTEXFLOW_TARGET=<X>` and run the existing test suite.
Every test should pass without modification — the test code names only the
core types and `cortexflow::platform::*`, never the backend directly.

## What core may NOT do

Core code (`include/cortexflow/`, `src/cortexflow/`) and module code is
forbidden from using `#ifdef CORTEXFLOW_TARGET_*` or similar target macros.
The `no_target_ifdefs` CTest entry greps for that pattern and fails the
build. The swap point is the typedef in `<cortexflow/platform.hpp>` and the
linked translation unit — not preprocessor branches at the call site.

If you find yourself wanting an `#ifdef`, you actually want either:

1. A new typedef in `<cortexflow/platform.hpp>` whose definition differs
   per target, OR
2. A new function in the platform layer whose implementation lives in
   `platform/<target>/`.

That additional surface is small enough to add deliberately rather than
absorb into module code.
