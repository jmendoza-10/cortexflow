# Flow lifecycle: `transition_to_now` + `done` → `on_flow_done` + `terminate` + `restart`

Status: ready-for-human
PRD: `docs/prd.md` — Flow subsystem; user stories 31, 36, 37, 38

## What to build

The completion and re-entry paths for `Flow<S>`.

- **`transition_to_now(next)`** — re-enters `next` immediately with the *same* envelope, after destructing the outgoing locals and constructing the incoming. The next state may use or ignore the envelope. No chain-depth limit is enforced; flow designers own infinite-chain prevention.
- **`done()`** — destructs current locals and synchronously calls `on_flow_done()` on the owning module before any other dispatch. The flow is now inactive until `restart()`.
- **`flow.terminate()`** — force-end a flow from *outside* a `step()` call (e.g., from a module's `handle` for a different message). Terminating from inside a `step` is a `CORTEXFLOW_ASSERT`.
- **`flow.restart()`** — begin a fresh run from the initial state with newly constructed locals; the initial state again sees a synthetic init envelope.

## Acceptance criteria

- [x] `transition_to_now(StateFn next)` and `done()` added to `StateDirective::Kind`
- [x] `transition_to_now` reuses the current envelope when invoking `next`; verified via integration test
- [x] `done` destructs current locals and synchronously calls `on_flow_done()` on the owning module
- [x] `flow.terminate()` callable outside `step`; called inside `step` triggers `CORTEXFLOW_ASSERT`
- [x] `flow.restart()` constructs fresh locals and re-dispatches the synthetic init envelope
- [x] Integration test: chained `transition_to_now` runs N transitions on one envelope
- [x] Integration test: `on_flow_done` runs before any further enqueued message is processed
- [x] Integration test: `restart` after `done` re-enters the initial state cleanly

## Blocked by

- `14-flow-state-locals.md`

## Comments

### 2026-05-14 — implementation notes (from sandcastle agent)

**What I built**

- **`StateDirective::Kind` extended with `TransitionNow` and `Done`**;
  factories `transition_to_now<NextTag>()` and `done()` join the
  existing `stay()` / `transition_to<Tag>()`. The four directives match
  the PRD's encoding table exactly. `TransitionNow` reuses the same
  `next` slot in the directive as `Transition`.
- **`Flow::step` is now a loop**, not a single dispatch:
    - `Stay` → break.
    - `Transition` → destruct outgoing locals, construct incoming, break.
    - `TransitionNow` → destruct outgoing, construct incoming, *continue*
      the loop, invoking the new state's `handle` with the same envelope.
    - `Done` → destruct outgoing, mark inactive, exit the loop, then call
      `owner_->on_flow_done()` synchronously.
- **Owner reference plumbed through `flow.start(Owner&)`** so the flow
  can route `on_flow_done` back to the owning module. This is a small
  API break vs. slice 14's `flow.start()`; every slice 13/14 test
  module updated to `flow.start(*this)`.
- **`in_step_` flag** set across the entire `step` body (including the
  `TransitionNow` loop iterations). `terminate()` and `restart()`
  assert that `in_step_` is false — that's the "terminating inside a
  step is a CORTEXFLOW_ASSERT" guarantee from the issue. The flag is
  cleared *before* `owner_->on_flow_done()` is invoked, so a callback
  that calls `flow.restart()` (a common pattern for episode flows)
  does not trip the assert.
- **`flow.terminate()`** is idempotent: a no-op if the flow is not
  currently active. That makes it safe to call unconditionally from
  module-teardown paths without a "have we already terminated?"
  check at the call site.
- **`flow.restart()`** requires that `start()` was called once first
  (asserts on `owner_ != nullptr`). The shared entry path
  (`enter_initial_`) is factored out so `start` and `restart` cannot
  drift apart.
- **`~Flow` destructs live locals only when `active_`** — done /
  terminate already destructed eagerly, so the destructor handles only
  the "module destroyed mid-flow" case (slice 14's RAII Subscription
  test relies on this).

**Tests**

- `tests/unit/test_flow.cpp` — three new cases: `transition_to_now<Tag>`
  identity, `done()` identity, and pairwise-distinct directive Kinds.
- `tests/integration/test_flow.cpp` — eight new cases:
    1. Chained `transition_to_now` runs four states on one envelope;
       the `from` field is identical at every state (proves envelope
       reuse, not just equality).
    2. `transition_to_now` destructs outgoing locals and constructs
       incoming locals (RAII probe per state).
    3. `done()` → `on_flow_done` runs synchronously, before the next
       queued envelope is processed. The module's `handle` routes
       `WakeMe` to a direct path that doesn't go through `flow.step`,
       so the test can observe in-flow vs. out-of-flow dispatch
       interleaving.
    4. `done()` destructs locals *before* `on_flow_done` is invoked
       (RAII probe + on_flow_done samples the destruct counter).
    5. `restart()` after `done` re-constructs locals and re-dispatches
       the synthetic init envelope; the test confirms the init handler
       runs twice and the locals' destructor runs cleanly per episode.
    6. `terminate()` from outside `step` destructs locals and
       deactivates the flow without invoking `on_flow_done`.
    7. `terminate()` from inside `step` trips the assert with reason
       containing "inside a step". The test reaches the in-step
       boundary by stashing a Flow pointer through a global; production
       code can't do this from a `static handle(...)` because `FlowCtx`
       exposes no Flow access, but the assert remains the safety belt
       for any future API extension.
- All 17 ctest targets pass at the default `DISPATCH` trace level and
  at `FULL`.

**Choices the human reviewer should sanity-check**

- **`flow.start(Owner&)` vs. binding owner at construction.** I
  considered three shapes for owner-pointer wiring: (a) a
  `bind_owner(Owner&)` helper called from the module body, (b)
  `Flow flow{*this};` in the member-init list, (c) `flow.start(Owner&)`.
  I picked (c) because it keeps the owner-pointer setup co-located
  with the call that needs it, doesn't require special CRTP discipline
  in member-init order, and only changes one line per existing module.
  If the reviewer prefers (b) (owner bound at construction, start()
  back to no-arg), it's a mechanical refactor.
- **`in_step_` blocks `restart` and `terminate` uniformly.** The issue
  only requires the assert for `terminate`. I extended the same check
  to `restart` because re-entering the flow's state machine while it
  is mid-dispatch would discard the in-flight envelope mid-step — much
  more confusing to debug than the assert. A flow that wants to
  effectively "restart from inside" can return `done()` and call
  `restart()` from `on_flow_done` (the in_step_ flag is already
  cleared by the time on_flow_done runs).
- **`Done` callback timing.** The PRD says "synchronously calls
  `on_flow_done()` on the owning module before any other dispatch";
  the natural reading is "before any other queue drain", not
  "literally inside the step's call frame before any branch unwinds."
  I clear `in_step_` first, then call `on_flow_done`, so the callback
  is free to mutate flow state (restart/terminate) but still runs
  inside the same `runtime.dispatch(env)` call, which is what makes
  the "before any further enqueued message" integration test pass.
- **`terminate()` is a no-op when inactive**, not an assert. A flow
  that has already finished (done or earlier terminate) can be
  terminated again without complaint. The issue doesn't explicitly
  cover this case; the lenient choice avoids a "have I done this
  already?" cargo-cult in shutdown code.
- **No compile-time chain-depth limit on `transition_to_now`.** Issue
  text says this is the flow designer's responsibility; I did not add
  a runtime guard either. An infinite chain hangs the dispatch thread,
  visibly. If the reviewer wants a soft limit (say, 256 chained
  transitions before assert), it's a counter in the step loop.

**Deferred**

- `bind_owner`-style alternative ergonomics — not needed yet, can be
  added without breaking the current API.
- ADR entries 008/009/010 (still pending from slices 13/14).
- The architecture-doc example signature still uses `Flow&` as the
  state-fn parameter; this implementation uses `FlowCtx&`. Doc update
  not in this slice's scope.
