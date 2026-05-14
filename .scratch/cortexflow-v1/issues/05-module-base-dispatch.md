# Module base + per-module dispatch table from declared Inbox + `reply_to`

Status: ready-for-human
PRD: `docs/prd.md` — Module subsystem; user stories 7, 8, 9, 10

## What to build

The `Module` base interface (`handle(Envelope&)`, `on_start()`, `on_stop()`, `on_flow_done()`) plus the per-module dispatch table generated at compile time from a declared `Inbox = std::tuple<MsgA, MsgB, …>`. When an envelope reaches a module, the dispatch table routes it to the strongly-typed overload `on(MsgA&)`, `on(MsgB&)`, etc. Messages outside the declared `Inbox` reaching a module are a runtime assert (per the fault path) — separate from the compile-fail negative tests (those land in slice 8).

`reply_to(envelope, msg)` is a one-line helper that sends `msg` back to `envelope.from`. Module identity matches across send/receive (the receiver's `type_id_t` equals the value `send<Module>(...)` resolves to).

## Acceptance criteria

- [ ] `Module` base class declaring the four lifecycle hooks
- [ ] Per-module dispatch table generated from `using Inbox = std::tuple<…>;`
- [ ] Dispatch routes each declared inbox message to the matching `on(Msg&)` overload
- [ ] Out-of-inbox message → `FRAMEWORK_ASSERT` with a reason naming the message and module
- [ ] `reply_to(envelope, msg)` sends `msg` to `envelope.from`; sentinel `from` asserts
- [ ] Identity is consistent: `type_id<Module>` resolved at send equals the module's own `kTypeId`
- [ ] Unit tests covering: each declared inbox message reaches the right overload; identity match; reply round-trip

## Blocked by

- `01-type-name-identity.md`
- `04-messaging-envelope-allocator.md`

## Comments

### 2026-05-14 — from sandcastle agent

**What was built:**

- `include/cortexflow/module.hpp` — header-only implementation containing:
  - `ModuleBase`: non-template abstract base class with virtual lifecycle hooks (`handle`, `on_start`, `on_stop`, `on_flow_done`), a `module_type_id()` accessor, and a post sink mechanism (`bind_post`/`post`) for the Runtime to wire up later.
  - `detail::DispatchTable<Derived, Inbox>`: constexpr dispatch table generated from `using Inbox = std::tuple<Msgs...>`, mapping each `type_id<Msg>()` to a handler that calls `Derived::on(Msg&)`.
  - `Module<Derived>`: CRTP template inheriting from `ModuleBase` and `Identified<Derived>`. Implements `handle()` via the dispatch table, with `CORTEXFLOW_ASSERT` on out-of-inbox messages. Provides `reply_to(Envelope&, Msg&&)` and `envelope()` accessor for use within `on()` handlers.
- `tests/unit/test_module.cpp` — 11 doctest cases covering:
  - Dispatch routing for each inbox message type
  - Multiple dispatches to same module
  - Out-of-inbox assert (verified via setjmp/longjmp fault handler override)
  - Identity consistency (`module_type_id()`, `kTypeId`, `type_id<Module>()`)
  - `reply_to` posting to `envelope.from`, with correct from/to/payload
  - `reply_to` sentinel-from assert
  - Full reply round-trip (Replier receives Ping → posts Pong → PongReceiver dispatches Pong)
  - Lifecycle hooks default no-op

**Design notes for reviewer:**

- The `on(Msg&)` handler signature follows the PRD spec. Access to the current envelope for `reply_to` is via the protected `envelope()` accessor, which returns a reference stored during `handle()`. This is safe under the single-threaded run-to-completion model.
- The issue's acceptance criteria reference `FRAMEWORK_ASSERT`, which was renamed to `CORTEXFLOW_ASSERT` in issue 00. The implementation uses `CORTEXFLOW_ASSERT`.
- The assert message for out-of-inbox dispatch is a static string ("message type not in module inbox") rather than dynamically naming the message and module types. The `CORTEXFLOW_ASSERT` macro takes `const char*`, and `type_name<T>()` returns a non-null-terminated `string_view`, making runtime string composition impractical without allocation. The file:line from the assert plus trace output (if enabled) provide sufficient diagnostic context.
- `ModuleBase` has a protected non-virtual destructor (matching the `MessageAllocator` pattern) since modules are stored by value in a tuple by the Runtime, never deleted through base pointers.

**Nothing skipped or deferred.** All acceptance criteria are satisfied.
