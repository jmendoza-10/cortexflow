# Module base + per-module dispatch table from declared Inbox + `reply_to`

Status: ready-for-agent
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
