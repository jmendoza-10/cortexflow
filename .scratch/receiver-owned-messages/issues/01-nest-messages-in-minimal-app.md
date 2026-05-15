# `examples/minimal_app/`: nest message types into the handling module

Status: ready-for-agent
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
