# ADR-0022: PRIVATE compile flags for `-fno-rtti` and `-fno-exceptions`

**Status:** Accepted
**Date:** 2026-05-18

## Context

CortexFlow is designed to be exception-free and RTTI-free internally — no `throw`, no `dynamic_cast`, no `typeid`. Failure handling goes through `CORTEXFLOW_ASSERT` (ADR-016, planned), not exceptions. The compile flags `-fno-rtti -fno-exceptions` are applied to the `cortexflow` static library to enforce that internally.

Until this decision, those flags were attached as **PUBLIC** compile options on the `cortexflow` CMake target (CMakeLists.txt, pre-change). PUBLIC propagates the flags up to every target in the consumer's build that links `cortexflow` — so a consumer who links `cortexflow` cannot compile any of *its* code with exceptions or RTTI, even code that has nothing to do with CortexFlow.

This decision came up while preparing the v0.1.0 release for FetchContent consumption from a separate, exception-using codebase (see [ADR-0023](0023-release-packaging-strategy.md)). The PUBLIC posture would have forced that codebase to either disable exceptions throughout or abandon CortexFlow.

## Decision

`-fno-rtti -fno-exceptions` are **PRIVATE** compile options on the `cortexflow` target. They apply when compiling `src/cortexflow/*.cpp` and the selected platform backend's sources; they do not propagate to consumers.

The "no exceptions, no RTTI" contract becomes a **documentation invariant**, not a compile-flag enforcement: framework code does not throw and does not depend on RTTI, and callbacks invoked by the framework (module `on(...)` handlers, flow `handle(...)` functions, cache subscribers) must not throw across the framework boundary.

`-Wall -Wextra -Wpedantic` remain PUBLIC — they are warning flags, not behavior-changing flags, and propagating them to consumers is harmless (and arguably good citizenship for a library asking to be embedded in code that should also be warning-clean).

## Consequences

**Enables:**

- A consumer using exceptions and RTTI in their own code can link `cortexflow` without conflict. This is the common case for application codebases consuming CortexFlow as a state-machine framework.
- The internal invariant (`cortexflow` library code itself does not throw and does not use RTTI) is preserved — PRIVATE still applies the flags when compiling our `.cpp` files.

**Costs:**

- Loses the compile-time wall that PUBLIC provided. A consumer who writes a throwing module `on(...)` handler will compile cleanly; the throw will only surface at runtime, where it crosses back into a framework caller that did not unwind. Behavior is undefined.
- This is a documentation-and-discipline contract now, not a mechanical one.

**Forbids (by convention, not by compile flag):**

- A module handler, flow state `handle()`, or cache subscriber must not throw across the framework boundary. The framework calls these via plain function calls; there is no try/catch boundary to absorb a thrown exception, and the runtime's invariants (queue ownership, subscription pool, in-flight envelope) would be left half-updated.

## Alternatives considered

- **PUBLIC compile flags (status quo before this ADR).** Force consumers to also compile without exceptions and RTTI. Rejected: hostile to consumer codebases that use exceptions for their own concerns; uses a compile flag to enforce a runtime invariant it does not actually enforce (a consumer can still write a throwing lambda or callback even with `-fno-exceptions` — it just fails to compile rather than fails to unwind correctly). Wrong tool for the job.

- **INTERFACE compile flags.** Apply flags to consumers only, not when building cortexflow itself. Rejected: nonsensical — we want the internal invariant enforced, which is exactly what PRIVATE does.

- **PRIVATE flags plus a runtime "exceptions disabled" assert.** Add a build-time check that the consumer's compile flags include `-fno-exceptions`, failing configure otherwise. Rejected: same hostility as PUBLIC, expressed differently.

- **No flags at all.** Drop `-fno-rtti -fno-exceptions` from the target entirely; rely purely on coding convention. Rejected: loses the internal compile-time wall too. Library code that accidentally adds a `throw` would silently compile and only manifest in unwind-vs-non-unwind environments.
