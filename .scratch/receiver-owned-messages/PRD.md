# Receiver-owned messages: nest message types in the handling module

Status: ready-for-agent
ADR: [`docs/adr/0020-receiver-owned-messages.md`](../../docs/adr/0020-receiver-owned-messages.md)

## Problem Statement

A framework user reading a CortexFlow **Module** today cannot see, from the module's own header alone, what messages it accepts. The module declares an `Inbox` tuple of message type names, but the message type *definitions* live in a sibling header (`examples/minimal_app/messages.hpp`) or at namespace scope in a test file. To learn the full inbound contract of `Producer`, the reader has to cross-reference `producer.hpp` with `messages.hpp` and reconstruct the relationship mentally.

That sibling header is a grab-bag. As more modules are added it grows into an undifferentiated list of `struct` definitions whose receivers must be inferred from the `Inbox` tuples elsewhere. Onboarding readers lose, debuggers lose (trace output shows bare `Bump`, not `Producer::Bump`), and new contributors have no encoded answer to the question "where does a new message go?"

## Solution

A message is defined as a `public` nested struct inside the **Module** class that handles it — the **Receiver-owned message** convention. A reader opening any module's header sees the entire inbound contract in one place: the nested message shapes, the `Inbox` tuple listing them, and the `on(...)` handler signatures.

To send a message to another module, the sender includes the receiver's header from a `.cpp` translation unit and constructs the message by its qualified name (`Producer::Done{}`). Header files reference only their own nested message types, keeping the include graph cycle-free without any per-module sibling headers or forward-declaration scaffolding.

The sibling `examples/minimal_app/messages.hpp` is deleted. The free-standing message structs in `tests/integration/test_runtime.cpp` are nested into the modules that own them. Framework-emitted messages (`KeyChanged<K>`, etc.) and free-standing fixtures inside framework-primitive tests are untouched.

## User Stories

1. As a framework user reading a module's header for the first time, I want every message type the module accepts defined inside the class, so the inbound contract is self-documenting.
2. As a framework user reading `minimal_app`, I want `Bump` and `Done` to live inside `Producer`, so I do not have to open `messages.hpp` to learn what Producer handles.
3. As a framework user reading `minimal_app`, I want `ProcessingTick` to live inside `Consumer`, so the self-sent timer message is colocated with its handler.
4. As a framework user inspecting `FULL`-level trace output, I want `type_name<T>()` to resolve to a fully-qualified name like `minimal_app::Producer::Bump`, so I can attribute a message to its receiver without reading the source.
5. As a framework user removing dead files, I want `examples/minimal_app/messages.hpp` deleted so the example has one fewer header to learn.
6. As a framework user adding a new message to a module I am writing, I want a single unambiguous rule — public nested struct inside the receiving class — so I do not have to weigh alternatives every time.
7. As a framework user writing a module that posts a message to another module, I want to `#include` the receiver's header from my `.cpp` and write `ReceiverModule::Foo{}` to construct the message, so the coupling direction matches the call direction.
8. As a framework user composing two modules that send messages to each other, I want headers to remain free of circular includes, with no per-module sibling messages header required, so the convention scales to bidirectional protocols.
9. As a framework user writing composed-app integration tests, I want the same convention applied to my test modules so the tests model real usage and onboarding readers see consistent patterns whether they open the example or a test.
10. As a framework contributor maintaining `tests/integration/test_runtime.cpp`, I want `Ping` nested inside `PingResponder` and `Pong` nested inside `PongCatcher`, so the ping/pong scenario follows the convention end-to-end.
11. As a framework contributor maintaining `tests/integration/test_runtime.cpp`, I want `Tick` nested inside `Counter` and `SeqMsg` nested inside `SeqRecorder`, so every uniquely-owned message in the integration suite follows the convention.
12. As a framework user, I want all nested message types declared `public`, so a test can inject a self-sent message directly (e.g. `runtime.post<Consumer>(Consumer::ProcessingTick{})`) to bypass timer waits.
13. As a framework user writing a unit test for the dispatch primitive itself, I want to continue defining free-standing message-shaped structs at namespace scope, so I can exercise capabilities like "same type in multiple inboxes" without forcing an arbitrary owner.
14. As a framework user writing a unit test for `type_name<T>()` or flow-state machinery, I want my free-standing fixture structs to remain unaffected, so primitive-level tests stay focused on the primitive under test.
15. As a framework user, I want the **Cache** subsystem and its `KeyChanged<K>` notification message to remain in `cache.hpp` unchanged, so framework-emitted messages stay owned by the subsystem that emits them.
16. As an onboarding contributor, I want the convention documented in `CONTEXT.md` under the **Receiver-owned message** glossary entry and explained in detail in `ADR-0020`, so I can learn the rule without reading code.
17. As a framework user reviewing `ADR-0020`, I want the rejected alternatives recorded (flat header, sender-owned, sibling per-module header, global escape hatch, private nested types), so future debates start from the same baseline.
18. As a future framework user who later encounters a true multi-module protocol, I want the *global escape hatch* (shared protocol header) to remain available as a future option, so the strict-now decision does not foreclose pragmatic later additions.
19. As a framework user, I want the runtime behavior of `minimal_app` and the integration test suite to be byte-for-byte identical before and after the refactor, so the change is provably structural and not behavioral.
20. As a framework user running CI, I want both `host` and `posix` builds (the two backends `minimal_app` is built against per issue 17) to continue to pass after the refactor.
21. As a framework user running the test suite with `-DCORTEXFLOW_TRACE_LEVEL=FULL` enabled, I want all existing test assertions to keep passing even though qualified type names appear in traces, so trace-output expectations remain stable.
22. As a sandcastle/AFK agent picking up this work, I want acceptance criteria stated as concrete file-level outcomes (file deleted, types nested, includes reorganized, suite green), so I can verify completion mechanically.
23. As a framework user, I want naming inside the class to be plain (`struct Bump`, not `struct BumpMessage`), so the qualifier comes from the enclosing class and not from a redundant type suffix.
24. As a framework user inspecting `examples/minimal_app/app.hpp` and `app.cpp`, I want any lingering references to free-standing message names updated so the example composes cleanly with the new nested types.
25. As a framework user reading the example's `README.md`, I want it to reflect the new layout if it referenced `messages.hpp`, so the onboarding narrative is internally consistent.

## Implementation Decisions

**Ownership rule.** Strict receiver-owned. A message lives in the class that handles it, defined as a `public` nested struct. The class's `Inbox` tuple references those nested types directly. No global escape hatch in this work — adding one later is mechanical; removing one once spread is not.

**Cross-module send discipline.** A module's `.hpp` references only its own nested message types. Any `send<OtherModule>(OtherModule::Foo{})` call lives in a `.cpp` translation unit, which is free to `#include` the receiver's header without risking a header cycle.

**Visibility.** All nested message types are `public`. This includes self-sent messages (e.g. `Consumer::ProcessingTick`). Private adds a sub-classification with no mechanical isolation benefit and blocks tests from injecting messages directly.

**Naming.** Plain class names (`struct Bump`), no `Message` suffix. The receiving class name provides the qualifier.

**Scope — composed-app code.** Applies to `examples/minimal_app/` and to `tests/integration/test_runtime.cpp`. Each free-standing message type in `test_runtime.cpp` maps to exactly one owning module (`Ping`→`PingResponder`, `Pong`→`PongCatcher`, `Tick`→`Counter`, `SeqMsg`→`SeqRecorder`) and migrates cleanly.

**Scope — exempt.** Framework-primitive tests stay untouched:
- `tests/unit/test_module.cpp` — `Ping` deliberately appears in two modules' inboxes to exercise the dispatch primitive's "same type, multiple receivers" capability.
- `tests/unit/test_type_name.cpp` — fixture types for the `type_name<T>()` utility, not messages.
- `tests/integration/test_flow.cpp` — `Trigger`/`Followup` are flow-transition test fixtures.

**Scope — framework messages.** `KeyChanged<K>` in `cache.hpp` and any future framework-emitted message types are owned by the framework subsystem that emits them. Out of scope for this refactor.

**Files to modify.**

| File | Change |
|---|---|
| `examples/minimal_app/messages.hpp` | Delete |
| `examples/minimal_app/modules/producer.hpp` | Add `public` nested `struct Bump {}` and `struct Done {}`; drop `#include "../messages.hpp"` |
| `examples/minimal_app/modules/producer.cpp` | Reference nested types unqualified (same class scope); drop `messages.hpp` include if present |
| `examples/minimal_app/modules/consumer.hpp` | Add `public` nested `struct ProcessingTick {}`; drop `#include "../messages.hpp"` |
| `examples/minimal_app/modules/consumer.cpp` | Add `#include "producer.hpp"`; replace any `Done{}` construction with `Producer::Done{}` |
| `examples/minimal_app/app.hpp`, `app.cpp` | Update any lingering free-standing message references |
| `examples/minimal_app/README.md` | Update if it referenced `messages.hpp` |
| `tests/integration/test_runtime.cpp` | Nest `Ping` into `PingResponder`, `Pong` into `PongCatcher`, `Tick` into `Counter`, `SeqMsg` into `SeqRecorder`; update construction sites to use qualified names |

**Architecture decision.** Recorded in `docs/adr/0020-receiver-owned-messages.md` (Status: Accepted). Glossary terms — **Module**, **Inbox**, **Message**, **Receiver-owned message** — documented in `CONTEXT.md` at the repo root.

## Testing Decisions

**A good test for this work asserts external behavior, not the rename mechanic.** No new test is added. The proof of correctness is that the existing test suite continues to pass after the structural rename — same scenarios, same module composition, same fanout/dispatch/state-transition behavior, with type definitions relocated.

**Tests that must stay green:**

- `tests/integration/test_minimal_app.cpp` — the 20-case suite added in issue 17. Covers start-time wakeup, single-cycle round-trip, 10-cycle invariant (`counter == acks + 1`), 50-iteration state-locals leak check, compile-time composition assertion. All five scenarios depend on the rewired `Producer`/`Consumer` modules.
- `tests/integration/test_runtime.cpp` — ping/pong, three-module ordering, counter+tick, sequence recorder. Every scenario whose module messages move into nested types must continue to dispatch and execute identically.
- `tests/integration/test_cache.cpp` — uses framework `KeyChanged<K>` only; unaffected, but must still pass.
- `tests/integration/test_flow.cpp` — out of scope per the exemption, but must still pass.
- `tests/unit/test_module.cpp`, `tests/unit/test_type_name.cpp`, `tests/unit/test_timer.cpp`, `tests/unit/test_clock.cpp` — out of scope per the exemption; must still pass.

**Build configurations that must stay green:**

- `host` and `posix` backends.
- `-DCORTEXFLOW_BUILD_TESTS=ON` with and without `-DCORTEXFLOW_TRACE_LEVEL=FULL`.
- Both `gcc` and `clang`.

(Mirroring the matrix exercised in issue 17 — that is the established CI surface for `minimal_app` and the integration suite.)

**Prior art for the test discipline.** Per the issue-tracker convention, structural refactors in this repo are validated by the existing functional suite continuing to pass — issue 00 (`rename-framework-to-cortexflow`) followed the same approach: rename mechanic, no new tests, green suite is the proof.

**No new test is added** asserting "the type name of `Producer::Bump` contains `Producer`." That would test the convention's surface rather than the framework's behavior, and would couple a test to the convention itself — making convention drift expensive without protecting any user-visible property.

## Out of Scope

- **Renaming the `Cache` subsystem.** Considered (candidates: `Context`, `SystemContext`, `AppContext`, `StateStore`, `Blackboard`) and rejected. `Context` collides with the existing `FlowCtx` per-state context object; the alternatives did not justify the churn at this stage. The `Cache`/`CacheKeyList`/`runtime.cache()` symbols remain unchanged.
- **Global escape hatch for protocol messages.** Deliberately deferred. To be reconsidered only when a concrete multi-module request/response protocol proves the strict receiver-owned rule painful.
- **Nesting flow states into the module class.** `Idle` and `Processing` in `consumer.hpp` are at namespace scope due to an incomplete-type constraint at the `Flow<Consumer, StateList<...>>` member declaration site. Possibly addressable via a static-assert or layout change, but a separate decision — not in scope here.
- **Framework-emitted messages.** `KeyChanged<K>` and any future framework-internal message types are owned by the framework subsystem that emits them. Not rehomed by this work.
- **Framework-primitive tests.** `tests/unit/test_module.cpp`, `tests/unit/test_type_name.cpp`, `tests/integration/test_flow.cpp`. Their free-standing fixture structs are deliberate — exempt per ADR-0020.
- **Compile-time enforcement of the receiver-owned convention.** Code-review enforcement only, consistent with how `Owned<K, M>` is treated in v1 (ADR-018).
- **Renaming `messages.hpp` to a per-module sibling header.** The file is deleted outright, not renamed or restructured. No `producer_messages.hpp` / `consumer_messages.hpp` is created.
- **Any change to the `Cache` API or behavior, the timer service, the runtime lifecycle, or the dispatch table machinery.** This is a structural rehoming of type definitions only.

## Further Notes

The architectural decision is recorded in [`docs/adr/0020-receiver-owned-messages.md`](../../docs/adr/0020-receiver-owned-messages.md), including the rejection rationale for each alternative considered. The glossary in [`CONTEXT.md`](../../CONTEXT.md) defines the new vocabulary (**Receiver-owned message**) and the surrounding terms (**Module**, **Inbox**, **Envelope**) needed to make the ADR readable.

`docs/adr/README.md` was updated to list ADR-0020 under a new "Written" section, distinct from the reserved slots 001–019 for the v1 architectural spine.

The session that produced this PRD also surfaced and *did not* take action on:
- An existing inconsistency in `docs/adr/README.md` between the four-digit ID convention (`NNNN`) and the three-digit IDs in the planned ADR table. Cosmetic; out of scope here.
- The possibility of a `static_assert` in `Flow<>` to catch the incomplete-state-type bug noted in the issue 17 sandcastle review. Separate decision worth raising independently.
- The flow-state nesting question (see Out of Scope).
