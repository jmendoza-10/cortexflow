# Flow skeleton: state fns + `StateDirective` + synthetic init envelope

Status: ready-for-agent
PRD: `docs/prd.md` — Flow subsystem; user stories 29, 30, 35, 39

## What to build

The core flow execution model. A `StateFn` is a free function with the signature `StateDirective(FlowCtx&, Envelope&)`. `StateDirective` is the directive a state returns — this slice covers `stay()` and `transition_to(next)`. (`transition_to_now` and `done` land in slice 15.) Any state function can be a legal "next" for any other state function — flow shapes are not constrained to a static graph.

A module owns a single `Flow<S>` (one flowchart per module is the v1 hard rule). The module's `handle(Envelope&)` delegates to `flow.step(env)`. On `flow.start()`, the framework dispatches a synthetic init envelope (`from = ModuleId::system()`, no payload) into the initial state so it can register its first subscriptions/timers immediately.

State-locals storage is *not* in this slice — see slice 14. For now, state functions carry only the directive return value; the framework allocates a placeholder buffer of fixed size.

## Acceptance criteria

- [ ] `StateFn` alias and `StateDirective` struct with `Kind::{Stay,Transition}`
- [ ] `stay()`, `transition_to(StateFn next)` factory functions
- [ ] `Flow<S>` template owned by a module; `flow.start()` dispatches the synthetic init envelope to the initial state
- [ ] Module's `handle(Envelope&)` delegates to `flow.step(env)`
- [ ] Synthetic init envelope has `from = ModuleId::system()` and is observable by the initial state
- [ ] Any `StateFn` can be passed as `next` to any other — no static graph constraint
- [ ] Integration test: two-state flow transitions on a triggering message; subsequent message handled by the new state
- [ ] Integration test: initial state sees the synthetic init envelope before any external send

## Blocked by

- `05-module-base-dispatch.md`
