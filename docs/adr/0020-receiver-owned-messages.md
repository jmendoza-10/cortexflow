# ADR-0020: Receiver-owned messages nested in the handling module

**Status:** Accepted
**Date:** 2026-05-15

## Context

Messages in CortexFlow are plain C++ types whose identity is derived from `type_name<T>()` (ADR-006). The framework places no constraint on *where* a message type is defined — only that the receiving module list it in its `Inbox` tuple and provide a matching `on(T&)` handler.

The convention emerging from `examples/minimal_app/` and the integration tests has been a flat per-app or per-test `messages.hpp` header (e.g. `minimal_app::Bump`, `minimal_app::Done`, `minimal_app::ProcessingTick`) sitting at namespace scope, with no encoded relationship to the modules that send or handle them. As more modules are added, this header becomes a grab-bag that doesn't tell a reader anything about the inbound contract of any specific module: to learn what messages a `Consumer` accepts, the reader has to cross-reference the `Inbox` tuple in `consumer.hpp` with the type definitions in `messages.hpp`.

Module classes already declare their inbound contract via `Inbox`. The message *type definitions* themselves are the missing piece — they belong with the handler, not in a sibling header.

## Decision

A message type is defined as a `public` nested struct inside the module class that handles it. The receiving module owns the type.

```cpp
class Producer : public cortexflow::Module<Producer> {
public:
    struct Bump {};
    struct Done {};
    using Inbox = std::tuple<Bump, Done>;

    void on_start() override;
    void on(Bump&);
    void on(Done&);
};
```

A module that needs to *send* a message to another module includes the receiver's header and constructs the message via its qualified name:

```cpp
// consumer.cpp
#include "producer.hpp"
// ...
ctx.send<Producer>(Producer::Done{});
```

To keep the include graph free of cycles, the rule is: **a module's header references only its own nested message types** (in its `Inbox` and `on(...)` signatures). Cross-module `send<X>(...)` calls live in `.cpp` translation units, where cross-includes cannot form header cycles.

This convention applies to **composed-app code** — example apps, and integration tests that compose multiple modules into application-shaped scenarios. Framework-emitted messages (e.g. `KeyChanged<K>` in `cache.hpp`) are owned by the framework subsystem that emits them and are not affected.

**Exempt: framework-primitive tests.** Tests whose purpose is to exercise a framework primitive in isolation — dispatch routing (`tests/unit/test_module.cpp`), `type_name<T>()` identity (`tests/unit/test_type_name.cpp`), flow-state machinery (`tests/integration/test_flow.cpp`) — may continue to define free-standing message-shaped structs at namespace scope. These structs are test fixtures for the primitive, not application messages. The defining example is `test_module.cpp`'s `Ping`, which appears in *two* modules' `Inbox` simultaneously to exercise the dispatch table's "same type, multiple receivers" capability; nesting it would force a choice of owner and break the test it was written to perform.

## Consequences

**Enables:**

- The module class becomes self-documenting. Opening `producer.hpp` shows the entire inbound contract — message shapes and handler signatures — in one place.
- Trace output gains precision. `type_name<Producer::Bump>()` produces `minimal_app::Producer::Bump`, fully attributing the message to its receiver in `FULL`-level traces (architecture §11).
- The coupling direction maps to the call direction. A sender depends on the receiver's interface, the same way a caller depends on a callee's public methods.

**Costs:**

- A reader unfamiliar with the convention may expect messages in a flat header. The convention must be documented (this ADR + `CONTEXT.md`) and the example must demonstrate it.
- Tests that want to inject a message by-name carry a slightly longer qualified type (`PingResponder::Ping` rather than `Ping`). The increase in clarity is judged to outweigh the verbosity.

**Forbids:**

- Defining a message type at any namespace scope outside the receiving module's class. There is no shared `messages.hpp` for application or test code.
- Referencing another module's nested message types from a `.hpp` file — this would force a header-level cross-include and risk cycles. Such uses must live in `.cpp`.

A future *global escape hatch* — a shared header for messages that genuinely form a multi-module protocol — is deliberately deferred. It will be reconsidered only when a concrete protocol proves the strict rule painful. Adding it later is mechanical; removing it once spread is not.

## Alternatives considered

- **Flat per-app `messages.hpp` (status quo).** A single namespace-scope header listing every message in the application. Rejected: gives no encoded relationship between a message and its handler; the file becomes a grab-bag as modules accumulate.

- **Receiver-owned with a global escape hatch from day one.** Same default as the chosen decision, but with a sibling `protocol_messages.hpp` permitted for request/response messages used between two specific modules. Rejected for now: none of the messages in `minimal_app` or the existing tests need it, and the hatch is easy to add later but hard to remove once code reaches for it on speculation.

- **Sender-owned messages.** Each message lives in the module that emits it. Rejected: contradicts the "modules accept and respond to messages" mental model — a reader inspecting a module would not see the messages it handles, only the ones it sends. Receivers would have to include senders to declare their `Inbox`, inverting the natural coupling direction.

- **Per-module sibling messages header (`producer_messages.hpp`).** Messages live in a file adjacent to the module's header rather than as nested types. Rejected: defeats the goal of putting messages *in the class*; adds a file-per-module overhead in exchange for cycle-immunity that the chosen .cpp-discipline already provides.

- **Private nested message types for self-sent messages.** Allow a message known to be self-sent only (e.g. `Consumer::ProcessingTick`) to be `private`. Rejected: adds a sub-classification readers must internalize; offers no mechanical isolation (all messages traverse the same queue); blocks tests from injecting a self-sent message directly to bypass timer waits.
