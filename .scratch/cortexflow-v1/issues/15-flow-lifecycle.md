# Flow lifecycle: `transition_to_now` + `done` → `on_flow_done` + `terminate` + `restart`

Status: ready-for-agent
PRD: `docs/prd.md` — Flow subsystem; user stories 31, 36, 37, 38

## What to build

The completion and re-entry paths for `Flow<S>`.

- **`transition_to_now(next)`** — re-enters `next` immediately with the *same* envelope, after destructing the outgoing locals and constructing the incoming. The next state may use or ignore the envelope. No chain-depth limit is enforced; flow designers own infinite-chain prevention.
- **`done()`** — destructs current locals and synchronously calls `on_flow_done()` on the owning module before any other dispatch. The flow is now inactive until `restart()`.
- **`flow.terminate()`** — force-end a flow from *outside* a `step()` call (e.g., from a module's `handle` for a different message). Terminating from inside a `step` is a `FRAMEWORK_ASSERT`.
- **`flow.restart()`** — begin a fresh run from the initial state with newly constructed locals; the initial state again sees a synthetic init envelope.

## Acceptance criteria

- [ ] `transition_to_now(StateFn next)` and `done()` added to `StateDirective::Kind`
- [ ] `transition_to_now` reuses the current envelope when invoking `next`; verified via integration test
- [ ] `done` destructs current locals and synchronously calls `on_flow_done()` on the owning module
- [ ] `flow.terminate()` callable outside `step`; called inside `step` triggers `FRAMEWORK_ASSERT`
- [ ] `flow.restart()` constructs fresh locals and re-dispatches the synthetic init envelope
- [ ] Integration test: chained `transition_to_now` runs N transitions on one envelope
- [ ] Integration test: `on_flow_done` runs before any further enqueued message is processed
- [ ] Integration test: `restart` after `done` re-enters the initial state cleanly

## Blocked by

- `14-flow-state-locals.md`
