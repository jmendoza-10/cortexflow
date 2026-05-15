# CortexFlow

A C++ framework for building control-plane systems whose job is to manage state, not move bulk data. The programming surface is three composable primitives — modules, messages, and flows — plus a shared typed data store.

## Language

**Module**:
A unit of behaviour with a declared inbound message contract (`Inbox`), lifecycle hooks (`on_start`, etc.), and optionally a `Flow`. Identified by its C++ type; one instance per type per `Runtime`.

**Inbox**:
The `std::tuple` of message types a module declares it accepts. Declared as a member alias on the module class (`using Inbox = std::tuple<...>`). The framework's dispatch table is generated from this tuple.

**Message**:
A plain C++ struct posted into the runtime queue inside an **Envelope**. Defined as a `public` nested type in the module that handles it (see [ADR-0020](docs/adr/0020-receiver-owned-messages.md)).
_Avoid_: "event" (overloaded with cache notifications), "command" (implies request/response which is not the underlying primitive).

**Receiver-owned message**:
The convention that a message type is defined in the class that handles it (`Producer::Bump`, not free-standing `Bump`). A sender includes the receiver's header and constructs the message by its qualified name. Cross-module `send<X>()` calls live in `.cpp` files, never headers, to keep the include graph acyclic.

**Envelope**:
The wrapper `{to, from, MessagePtr}` that travels through the runtime queue. Dispatch resolution happens at the receiver, not the sender.

**Cache**:
The runtime's typed key-value store, used as a third communication channel alongside messages and direct method calls. Reads return `std::optional<value_type>`; writes that change the value fan out `KeyChanged<K>` envelopes to registered subscribers. Called "cache" because each key holds the most-recent value of a continuously-updated quantity (sensor reading, mode flag, derived state) that late readers can sample without waiting for the next update.
_Avoid_: "context" (collides with `FlowCtx`), "store" / "state store" (considered and skipped — see Flagged ambiguities).

**Cache key**:
A C++ type with a `value_type` alias, registered in the runtime's `CacheKeyList`. The type *is* the identity — no enums, no string keys.

**KeyChanged\<K\>**:
The canonical notification payload the cache posts to subscribers when key `K`'s value changes. Framework-owned (defined in `cache.hpp`), not subject to the receiver-owned convention.

**Subscription**:
A RAII handle returned by `cache.subscribe<K, Subscriber>()`. Destruction releases the slot in the cache's fixed-size pool synchronously.

**Owned\<K, M\>**:
A documentation marker in `CacheKeyList` declaring that module `M` is the writer of key `K`. In v1, parsed but not statically enforced (ADR-018).

**Flow**:
A per-module state machine. Each module has at most one Flow (v1), parameterised by a `StateList` of state types.

**State** (in a Flow):
A struct with a nested `Locals` type and a `static handle(FlowCtx&, Envelope&)` function. The framework constructs a per-state `Locals` on entry and destructs it on transition, providing RAII state-locals with framework-managed lifetime.

**FlowCtx**:
The per-callback context object handed to a state's `handle()` function. Provides `transition_to_now<State>()`, access to the timer service, and the post sink for outbound messages. Distinct from any notion of system-wide context.

**Runtime**:
The top-level coordinator. Owns the event loop, the message queue, the cache, the timer service, and every module instance. Composed at compile time via `Runtime<ModuleList, CacheKeyList, Config>`.

**Boundary module**:
By convention, a module that interacts with the outside world (sensors, sockets, hardware peripherals). Not an enforced base class — just a label for modules that bridge external events into the runtime queue (ADR-017).

## Relationships

- A **Module** declares an **Inbox** that lists the **Messages** it accepts
- A **Message** is defined as a `public` nested type in the **Module** that handles it (the **Receiver-owned message** convention)
- A **Module** sends a **Message** by wrapping it in an **Envelope** and posting it to the **Runtime** queue
- A **Module** may optionally own one **Flow**, which sequences a set of **States**
- A **State** receives the same **Envelopes** the **Module** would, routed through `flow.step`
- A **Module** reads and writes the **Cache** via **Cache keys**; writes fan out **KeyChanged\<K\>** envelopes to **Subscriptions**
- A **Cache key** has at most one declared writer (the **Owned\<K, M\>** marker); any number of readers and subscribers

## Example dialogue

> **Dev:** "I'm adding a `ChargeController` module. It needs to ask `BatteryMonitor` for the current pack temperature. Where do I put the `TempQuery` message?"
>
> **Maintainer:** "`TempQuery` is something `BatteryMonitor` handles, so it lives in `BatteryMonitor`. Inside the class as `struct TempQuery {}`. Your `ChargeController` includes `battery_monitor.hpp` from its `.cpp` and posts `BatteryMonitor::TempQuery{}`."
>
> **Dev:** "And the response back? `TempReply` is something `ChargeController` handles."
>
> **Maintainer:** "Right — `TempReply` is a `public` nested struct in `ChargeController`. `BatteryMonitor` includes `charge_controller.hpp` from *its* `.cpp` to send the reply. Two cross-includes, both in `.cpp`, no header cycle."
>
> **Dev:** "Could I just put both in a shared `temperature_protocol.hpp`?"
>
> **Maintainer:** "Not today — the rule is strict receiver-owned. We deferred the shared-protocol hatch until a concrete case proves the strict rule painful. This one isn't there yet."

## Flagged ambiguities

- **"Context" as a name for the Cache.** Considered in a design session and rejected: collides with `FlowCtx` (the per-state callback context). A reader would have to disambiguate "system context" from "flow context" on every reference. The cache stays called *Cache*.
- **"StateStore" as a name for the Cache.** Considered as a follow-up alternative and skipped along with the rename — the existing name is acceptable and the churn cost isn't justified. The decision may be revisited if the framework grows responsibilities beyond typed K/V.
- **"Event".** Avoid as a synonym for **Message** — it's already overloaded with cache change notifications (`KeyChanged<K>`) and the framework's six-level trace events.
- **"State".** Disambiguate: a **State** in a **Flow** (a struct with `Locals` and `handle()`) is a different thing from "the runtime's state" (which isn't a defined concept — say *runtime status* or *cache contents* depending on what you mean).
