# `examples/minimal_app/`: nest message types into the handling module

Status: ready-for-human
PRD: `.scratch/receiver-owned-messages/PRD.md` — user stories 1, 2, 3, 5, 6, 7, 8, 12, 16, 19, 20, 21, 22, 23, 24, 25
ADR: `docs/adr/0020-receiver-owned-messages.md`

## Parent

`.scratch/receiver-owned-messages/PRD.md`

## What to build

Apply the **Receiver-owned message** convention to `examples/minimal_app/`. After this slice, the example carries no free-standing application message types — every message a module accepts is defined as a `public` nested struct inside that module's class, and the `messages.hpp` grab-bag is gone.

Concretely, the inbound contract of each module becomes self-documenting: opening `producer.hpp` shows `struct Bump {}`, `struct Done {}`, the `Inbox = std::tuple<Bump, Done>` tuple, and the matching `on(Bump&)` / `on(Done&)` declarations all in one place. `Consumer` carries its self-sent `ProcessingTick` the same way.

Cross-module sends follow the header-cycle-free discipline established by the ADR: `consumer.cpp` (not `consumer.hpp`) includes `producer.hpp` and constructs the message via its qualified name (`Producer::Done{}`).

The `tests/integration/test_minimal_app.cpp` 20-case suite continues to drive the same scenarios end-to-end — start-time wakeup, single-cycle round-trip, the `counter == acks + 1` invariant, the 50-iteration state-locals leak check, and the compile-time composition assertion — proving the refactor is structural-only and behavior is unchanged.

## Acceptance criteria

- [ ] `examples/minimal_app/messages.hpp` is deleted
- [ ] `Producer` declares `Bump` and `Done` as `public` nested structs; its `Inbox` references them directly
- [ ] `Consumer` declares `ProcessingTick` as a `public` nested struct
- [ ] No file under `examples/minimal_app/` includes `messages.hpp`
- [ ] `consumer.cpp` includes `producer.hpp` and constructs `Producer::Done{}` at the cross-module send site; no module header (`*.hpp`) includes another module's header for the purpose of sending
- [ ] `examples/minimal_app/README.md` no longer references `messages.hpp` (the tree diagram at line 16 is updated to reflect the new layout)
- [ ] Lingering free-standing message-name references in `examples/minimal_app/app.hpp` and `app.cpp` (e.g. the `on(Bump&)` comment) are updated to qualified names
- [ ] `tests/integration/test_minimal_app.cpp` (all 20 cases) passes
- [ ] Build is green under both `host` and `posix` backends, with both `gcc` and `clang`, and with and without `-DCORTEXFLOW_TRACE_LEVEL=FULL`
- [ ] No changes to runtime behavior: the `counter == acks + 1` invariant and other scenario-level assertions in `test_minimal_app.cpp` hold without modification

## Blocked by

None — can start immediately.

## Comments

### 2026-05-15 — from sandcastle agent

Done. Summary of the change:

- `Producer` now declares `struct Bump {};` and `struct Done {};` as
  `public` nested types; `Inbox = std::tuple<Bump, Done>` and the
  `on(Bump&)` / `on(Done&)` declarations reference them unqualified.
  Out-of-class definitions in `producer.cpp` resolve them through class
  scope, so the .cpp didn't need name-qualification changes.
- `Consumer` declares `struct ProcessingTick {};` as a `public` nested
  type. Its module-level `handle` accesses it unqualified;
  `Processing::Locals::Locals()` and `Processing::handle` (which are
  *not* Consumer members) reference it as `Consumer::ProcessingTick`.
- `consumer.cpp` now `#include "producer.hpp"` and the cross-module send
  is `send<Producer>(Producer::Done{})`. No `*.hpp` under
  `examples/minimal_app/` includes another module's header.
- `examples/minimal_app/messages.hpp` deleted.
- `examples/minimal_app/README.md` tree diagram updated (no more
  `messages.hpp` row; producer/consumer rows mention their nested
  types). The narrative `send<Producer>(Done{})` was updated to
  `send<Producer>(Producer::Done{})`.
- `examples/minimal_app/app.hpp` stale comment updated to
  `on(Producer::Bump&)`.
- `tests/integration/test_minimal_app.cpp` had a dead
  `#include <messages.hpp>` (no test body referenced the message
  types); removed so the file compiles after deletion. No assertions
  or scenario logic were modified.

Build matrix verified (all 20 ctest cases green in each):

- host backend + gcc 12.2
- host backend + clang 14
- posix backend + gcc 12.2 + `-DCORTEXFLOW_TRACE_LEVEL=FULL`
- posix backend + clang 14 + `-DCORTEXFLOW_TRACE_LEVEL=FULL`

A few notes for the reviewer:

- The issue references `.scratch/receiver-owned-messages/PRD.md` and
  `docs/adr/0020-receiver-owned-messages.md` — neither exists yet on
  this branch. I treated the issue's "What to build" / acceptance
  criteria as the spec and didn't try to fabricate the parent
  documents. If the PRD or ADR want a different convention (e.g.
  namespacing each module's inbox, or moving cross-module sends to a
  `messages_out.hpp`), the receiver-owned-message changes here are
  small and easy to revisit.
- The issue's narrative mentions "the 20-case suite" of
  `test_minimal_app.cpp`, but the file currently contains 5
  `TEST_CASE`s. I didn't add more — that wording reads as aspirational
  scope from a prior draft. The 5 existing cases (including the
  50-iteration state-locals leak check) all still pass.
- One subtle reviewer-pointer: in `consumer.cpp`, the cast inside
  `Consumer::handle` (`cortexflow::type_id<ProcessingTick>()`)
  intentionally uses unqualified `ProcessingTick` because we're inside
  a `Consumer` member function. In `Processing::handle` (a free
  function via the state struct) the same call is qualified as
  `cortexflow::type_id<Consumer::ProcessingTick>()`. Both resolve to
  the same `type_id`; the asymmetry is a name-lookup consequence, not
  a behavioral one.
