# Thread-safe `runtime.post(envelope)` from foreign threads

Status: ready-for-agent
PRD: `docs/prd.md` — Runtime / Messaging; user stories 46, 47

## What to build

Make `runtime.post(envelope)` callable from any thread. On host targets the queue is guarded by `std::mutex` + `std::condition_variable` (already in slice 6). This slice adds: enforcement of thread-safe allocator use during foreign posts, wake semantics from the foreign-thread enqueue, and stress coverage for the concurrency path. Boundary modules (CAN adapter, sockets, etc.) are conventional modules that own internal threads and call `runtime.post(...)` to deliver foreign work into the loop.

## Acceptance criteria

- [ ] `runtime.post(envelope)` is documented and tested as thread-safe
- [ ] Allocator's foreign-thread path is exercised (lock primitive held during `make_message<T>(...)` from a foreign thread)
- [ ] Foreign post correctly wakes the main thread blocked in `run()` via the CV
- [ ] Wake-on-stop: a foreign-thread `post()` after `stop()` is a no-op (asserts? or silently dropped? — pick one and document)
- [ ] Stress test: a foreign thread posts N envelopes; main thread drains via `run_one`; all envelopes observed in FIFO order
- [ ] Stress test passes under TSan on host

## Blocked by

- `06-runtime-skeleton-lifecycle.md`
