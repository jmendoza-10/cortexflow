# `tests/integration/test_runtime.cpp`: nest message types into owning modules

Status: ready-for-agent
PRD: `.scratch/receiver-owned-messages/PRD.md` — user stories 9, 10, 11, 19, 20, 21, 22
ADR: `docs/adr/0020-receiver-owned-messages.md`

## Parent

`.scratch/receiver-owned-messages/PRD.md`

## What to build

Apply the **Receiver-owned message** convention to the composed-app scenarios in `tests/integration/test_runtime.cpp`. Each free-standing message type in that file has exactly one owning module and migrates cleanly into it.

After this slice the integration tests model the convention end-to-end — a reader inspecting `PingResponder` sees `Ping` defined inside it, and the ping/pong scenario's `send<PongCatcher>(...)` call constructs a fully-qualified `PongCatcher::Pong{}`. The same applies to the counter+tick and sequence-recorder scenarios.

This slice **does not** touch the framework-primitive tests exempted by ADR-0020 (`tests/unit/test_module.cpp`, `tests/unit/test_type_name.cpp`, `tests/integration/test_flow.cpp`). Their free-standing fixture structs are deliberate — `test_module.cpp`'s `Ping` notably appears in two modules' inboxes to exercise the dispatch primitive's "same type, multiple receivers" capability. Leave them alone.

`tests/integration/test_cache.cpp` already uses only framework `KeyChanged<K>` in its Inboxes and is not affected.

## Acceptance criteria

- [ ] `Ping` is a `public` nested struct inside `PingResponder`
- [ ] `Pong` is a `public` nested struct inside `PongCatcher`
- [ ] `Tick` is a `public` nested struct inside `Counter`
- [ ] `SeqMsg` is a `public` nested struct inside `SeqRecorder`
- [ ] No free-standing `Ping`, `Pong`, `Tick`, or `SeqMsg` definitions remain at namespace scope in `test_runtime.cpp`
- [ ] All construction sites use the qualified name (e.g. `PongCatcher::Pong{msg.seq}`, `SeqRecorder::SeqMsg{...}`)
- [ ] Every existing test case in `test_runtime.cpp` (ping/pong round-trip, three-module ordering, counter+tick, sequence recorder) passes unchanged
- [ ] `tests/unit/test_module.cpp`, `tests/unit/test_type_name.cpp`, and `tests/integration/test_flow.cpp` are **not** modified
- [ ] `tests/integration/test_cache.cpp` is **not** modified
- [ ] Build is green under both `host` and `posix` backends, with both `gcc` and `clang`, and with and without `-DCORTEXFLOW_TRACE_LEVEL=FULL`

## Blocked by

None — can start immediately. Independent of issue `01`; the two slices touch disjoint files and may be picked up in parallel.
