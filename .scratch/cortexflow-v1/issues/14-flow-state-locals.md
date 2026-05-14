# Flow: state-locals aligned-storage + construct-in-place + RAII transition

Status: ready-for-human
PRD: `docs/prd.md` — Flow subsystem; user stories 32, 33, 34

## What to build

Each state function declares a typed state-locals struct (e.g., via a `using Locals = …;` member of a state-tag type). CortexFlow allocates a single compile-time-sized buffer in the `Flow<S>` instance, sized to the largest `Locals` across the flow (computed at compile time via `std::max({sizeof(Ts::Locals)...})` over the state list). There is **no runtime allocation per transition**.

On entry to a state, CortexFlow constructs that state's `Locals` in place inside the buffer. On transition, it destructs the outgoing `Locals` and constructs the incoming. RAII members of `Locals` (subscriptions, timers, anything held by value) follow this lifetime automatically — no manual cleanup in state functions.

The compile-time state list is derived from a single declared tuple of state-tag types on the flow.

## Acceptance criteria

- [x] `Flow<S>` carries `alignas(max-align) std::byte buffer[max-size]` sized at compile time
- [x] Compile-time computation of buffer size = `max(sizeof(StateTag::Locals)...)` over the declared state list
- [x] Compile-time computation of alignment = `max(alignof(StateTag::Locals)...)`
- [x] Entry to a state constructs `Locals` in place
- [x] Transition destructs the outgoing `Locals` and constructs the incoming
- [x] Integration test using RAII probe types: destruction observed exactly once per transition; construction observed exactly once per entry
- [x] Integration test: a state-local `Subscription` (from slice 10) is released on transition without explicit code

## Blocked by

- `13-flow-skeleton.md`

## Comments

### 2026-05-14 — implementation notes (from sandcastle agent)

**What I built**

- **State tags replace bare `StateFn` pointers as the unit of state
  identity.** A state tag is a type with `static StateDirective
  handle(FlowCtx&, Envelope&)` and an optional `using Locals = ...`. If
  `Locals` is omitted, an empty struct (`detail::EmptyLocals`) stands in
  so size/alignment math degenerates to 1/1 for that state.
- **`StateList<StateTags...>`** carries the compile-time size + alignment
  fold: `kMaxLocalsSize = std::max({sizeof(StateTag::Locals)...})` and
  `kMaxLocalsAlign = std::max({alignof(StateTag::Locals)...})`, using
  `std::max(initializer_list)` (constexpr since C++14). Empty StateLists
  fail a `static_assert` since `std::max({})` would be undefined.
- **`Flow<Owner, StateListT, InitialStateTag = StateListT::head>`** —
  templates on the state list plus an overridable initial state. The
  buffer is `alignas(StateListT::kMaxLocalsAlign) std::byte
  locals_buffer_[StateListT::kMaxLocalsSize]`, zero runtime allocation
  per transition.
- **`detail::StateInfo`** descriptor per state-tag carries three
  function pointers: `handle`, `construct_locals`, `destruct_locals`.
  Each `kStateInfo<Tag>` is its own inline-variable-template
  instantiation, giving every state a unique stable address. The flow
  stores `const StateInfo*` for `current_`/`initial_`, and
  `StateDirective::next` is now `const StateInfo*` rather than a raw
  `StateFn`.
- **`transition_to<NextTag>()`** — templated on the next state-tag
  type; returns a directive whose `next` is `&kStateInfo<NextTag>`.
- **Step / transition mechanics** — on `Kind::Transition`, the flow
  destructs the outgoing `Locals` via the current descriptor's
  trampoline, swaps `current_` to the directive's `next`, then
  constructs the new `Locals` in place. `FlowCtx::locals_ptr_` is set
  to point into the buffer so `ctx.locals<L>()` (also added this slice)
  returns a typed reference.
- **`~Flow` destructs the live locals** if the flow has been started.
  That's what makes the state-local `Subscription` integration test
  pass without any explicit cleanup — module destruction tears down the
  Flow which tears down the current state's locals which releases the
  subscription slot.
- **Tests**
  - `tests/unit/test_flow.cpp` — `StateList` size/alignment folds
    (including the empty-default case), `transition_to<Tag>()` identity,
    distinct `kStateInfo<Tag>` addresses.
  - `tests/integration/test_flow.cpp` — preserved slice 13's six tests
    (rewritten to the state-tag API) and added two slice-14 tests:
      1. RAII probe types: constructor/destructor counters confirm
         exactly one construction per entry and one destruction per
         transition, plus a final destruction at app teardown.
      2. State-local `Subscription` held by `SubLocals` is released on
         transition with no cleanup code in the state body.
  - All 19 tests pass at the default trace level and at `TRACE_FULL`.

**Choices the human reviewer should sanity-check**

- **State identity = `const StateInfo*`, not `StateFn`.** Slice 13 used
  raw function pointers as state identity and treated the directive's
  `next` as a `StateFn`. Slice 14 needs a per-state descriptor (handle
  + ctor + dtor), so I promoted the directive's `next` to
  `const StateInfo*`. This is a breaking change to the slice 13 API
  surface; the slice 13 tests are rewritten to the new shape. If the
  reviewer wants the old `StateFn` alias preserved for ergonomics (e.g.
  to keep state functions as free functions registered against tags),
  it could be reintroduced via a wrapper template, but the current
  uniform `Tag::handle` static-member approach is simpler and matches
  the issue's "via a `using Locals = …;` member of a state-tag type"
  wording.
- **`Locals` is optional, defaulting to an empty struct.** The issue
  reads "each state declares" but the tests for the no-locals states
  (e.g. the dynamic-shape ping-pong) are cleanest if you can omit the
  declaration. If the reviewer prefers it mandatory, switching to a
  hard `static_assert(has_locals_v<Tag>)` is a one-line change.
- **`InitialStateTag` defaults to `StateListT::head`.** I considered
  passing the initial state as a constructor parameter to mirror slice
  13's `Flow(StateFn initial)`, but a defaulted third template
  parameter is zero-cost and the head-of-list default is the obvious
  one. The override is there for asymmetric flows or for tests that
  want to start mid-list.
- **No compile-time check that `transition_to<Tag>()` targets a tag
  inside the flow's `StateList`.** If a state transitions to a tag that
  wasn't declared in the StateList, the buffer might not be wide
  enough. Enforcing this would require threading the StateList type
  into `transition_to`, which complicates the call site. I left it as
  a documented convention for v1.

**Deferred**

- `transition_to_now`, `done`, `restart`, `terminate`, and
  `on_flow_done` — slice 15.
- ADR entries 008/009/010 still pending.
- The architecture doc's example signature
  `StateFn = StateDirective (*)(Flow& flow, Envelope& env)` still uses
  a `Flow&` parameter; this implementation uses `FlowCtx&` (per slice
  13's design) and accesses locals via `ctx.locals<L>()`. The doc could
  be updated to reflect the actual API.
