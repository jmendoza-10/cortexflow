# Flow skeleton: state fns + `StateDirective` + synthetic init envelope

Status: ready-for-human
PRD: `docs/prd.md` — Flow subsystem; user stories 29, 30, 35, 39

## What to build

The core flow execution model. A `StateFn` is a free function with the signature `StateDirective(FlowCtx&, Envelope&)`. `StateDirective` is the directive a state returns — this slice covers `stay()` and `transition_to(next)`. (`transition_to_now` and `done` land in slice 15.) Any state function can be a legal "next" for any other state function — flow shapes are not constrained to a static graph.

A module owns a single `Flow<S>` (one flowchart per module is the v1 hard rule). The module's `handle(Envelope&)` delegates to `flow.step(env)`. On `flow.start()`, CortexFlow dispatches a synthetic init envelope (`from = ModuleId::system()`, no payload) into the initial state so it can register its first subscriptions/timers immediately.

State-locals storage is *not* in this slice — see slice 14. For now, state functions carry only the directive return value; CortexFlow allocates a placeholder buffer of fixed size.

## Acceptance criteria

- [x] `StateFn` alias and `StateDirective` struct with `Kind::{Stay,Transition}`
- [x] `stay()`, `transition_to(StateFn next)` factory functions
- [x] `Flow<S>` template owned by a module; `flow.start()` dispatches the synthetic init envelope to the initial state
- [x] Module's `handle(Envelope&)` delegates to `flow.step(env)`
- [x] Synthetic init envelope has `from = ModuleId::system()` and is observable by the initial state
- [x] Any `StateFn` can be passed as `next` to any other — no static graph constraint
- [x] Integration test: two-state flow transitions on a triggering message; subsequent message handled by the new state
- [x] Integration test: initial state sees the synthetic init envelope before any external send

## Blocked by

- `05-module-base-dispatch.md`

## Comments

### 2026-05-14 — implementation notes (from sandcastle agent)

**What I built**

- `include/cortexflow/flow.hpp` — header-only Flow subsystem:
  - `StateDirective { Kind::{Stay,Transition}, StateFn next }` plus
    `stay()` / `transition_to(next)` factories. `transition_to(nullptr)`
    asserts.
  - `using StateFn = StateDirective (*)(FlowCtx&, Envelope&);` — uniform
    free-function signature so any `StateFn` is a legal `next` for any
    other. `FlowCtx` is currently an empty class reserved as the access
    point for slice 14's state-locals.
  - `Flow<Owner>` owned by a module. `start()` synchronously dispatches a
    synthetic init envelope into the initial state; `step(env)` invokes
    the current state and applies the returned directive.
- `kSystemSender = ~uint64_t(0)` — sentinel `from` for the synthetic init
  envelope, distinct from `kNoSender = 0`. I added this constant to
  `flow.hpp` rather than `messaging.hpp` because it is a flow-specific
  concept; if more system-injected envelope kinds appear, it could move.
- `FlowInit` — empty payload struct allocated through `make_message`. The
  envelope must carry a typed payload, so this stands in for the "no
  payload" wording in the issue. State functions detect first entry via
  `env.from() == kSystemSender`, not via the payload type.
- Tests: `tests/unit/test_flow.cpp` covers the StateDirective value type;
  `tests/integration/test_flow.cpp` covers the two issue-mandated
  integration scenarios (init visibility, two-state transition) plus the
  dynamic-shape claim and the two negative paths (double-start,
  step-before-start). All 17 tests pass, also at `TRACE_FULL`.

**Choices the human reviewer should sanity-check**

- **`Flow<Owner>` vs `Flow<StateList>`** — I templated `Flow` on the
  owning module type because the synthetic envelope needs a `to` field
  and `type_id<Owner>()` is the natural value. Slice 14 will introduce a
  state-list template parameter for the locals-buffer size computation;
  the obvious shape there is `Flow<Owner, StateList>`. If the reviewer
  prefers `Flow<StateList>` (with the owner threaded through some other
  mechanism), this is the spot to fix it before slice 14 builds on top.
- **Synchronous init dispatch** — `flow.start()` calls the initial state
  directly rather than posting the init envelope to the queue. That's
  consistent with the PRD wording ("dispatch a synthetic init envelope
  into the initial state") and means the initial state has registered
  its subscriptions/timers by the time `app.start()` returns. The
  alternative — posting to the queue and letting `run_one()` deliver it
  — also works but defers the registration until the first drain.
- **Module's `handle` override** — modules that own a Flow are expected
  to override `handle(Envelope&)` to delegate to `flow.step(env)`,
  bypassing the inbox-based dispatch in `Module<Derived>::handle`. The
  modules in the test file all do this with `using Inbox = std::tuple<>;`
  to keep the parent template happy. Worth confirming this is the
  intended pattern before slice 15 cements it; an alternative would be
  to add a `FlowModule<Derived>` helper that wires `handle` to
  `flow.step` for you.
- **Placeholder locals buffer** — `Flow` holds a 64-byte
  `alignas(max_align_t)` buffer marked `[[maybe_unused]]`. Slice 14 will
  size this at compile time from the state list. I picked 64 arbitrarily
  to keep `Flow` non-empty; if the reviewer prefers a different
  placeholder size (or no buffer at all in this slice), it is a
  one-line change.

**Deferred**

- `transition_to_now` and `done` directives, `on_flow_done`,
  `flow.terminate()`, and `flow.restart()` — all in slice 15 per the
  issue.
- State-locals storage and the `FlowCtx::locals<L>()` accessor — slice
  14.
- ADR entries 008/009/010 still pending; this slice doesn't write them.
