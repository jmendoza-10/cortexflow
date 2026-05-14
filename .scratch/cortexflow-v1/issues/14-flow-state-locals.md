# Flow: state-locals aligned-storage + construct-in-place + RAII transition

Status: ready-for-agent
PRD: `docs/prd.md` — Flow subsystem; user stories 32, 33, 34

## What to build

Each state function declares a typed state-locals struct (e.g., via a `using Locals = …;` member of a state-tag type). CortexFlow allocates a single compile-time-sized buffer in the `Flow<S>` instance, sized to the largest `Locals` across the flow (computed at compile time via `std::max({sizeof(Ts::Locals)...})` over the state list). There is **no runtime allocation per transition**.

On entry to a state, CortexFlow constructs that state's `Locals` in place inside the buffer. On transition, it destructs the outgoing `Locals` and constructs the incoming. RAII members of `Locals` (subscriptions, timers, anything held by value) follow this lifetime automatically — no manual cleanup in state functions.

The compile-time state list is derived from a single declared tuple of state-tag types on the flow.

## Acceptance criteria

- [ ] `Flow<S>` carries `alignas(max-align) std::byte buffer[max-size]` sized at compile time
- [ ] Compile-time computation of buffer size = `max(sizeof(StateTag::Locals)...)` over the declared state list
- [ ] Compile-time computation of alignment = `max(alignof(StateTag::Locals)...)`
- [ ] Entry to a state constructs `Locals` in place
- [ ] Transition destructs the outgoing `Locals` and constructs the incoming
- [ ] Integration test using RAII probe types: destruction observed exactly once per transition; construction observed exactly once per entry
- [ ] Integration test: a state-local `Subscription` (from slice 10) is released on transition without explicit code

## Blocked by

- `13-flow-skeleton.md`
