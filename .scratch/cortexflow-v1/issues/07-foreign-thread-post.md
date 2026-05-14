# Thread-safe `runtime.post(envelope)` from foreign threads

Status: ready-for-human
PRD: `docs/prd.md` — Runtime / Messaging; user stories 46, 47

## What to build

Make `runtime.post(envelope)` callable from any thread. On host targets the queue is guarded by `std::mutex` + `std::condition_variable` (already in slice 6). This slice adds: enforcement of thread-safe allocator use during foreign posts, wake semantics from the foreign-thread enqueue, and stress coverage for the concurrency path. Boundary modules (CAN adapter, sockets, etc.) are conventional modules that own internal threads and call `runtime.post(...)` to deliver foreign work into the loop.

## Acceptance criteria

- [x] `runtime.post(envelope)` is documented and tested as thread-safe
- [x] Allocator's foreign-thread path is exercised (lock primitive held during `make_message<T>(...)` from a foreign thread)
- [x] Foreign post correctly wakes the main thread blocked in `run()` via the CV
- [x] Wake-on-stop: a foreign-thread `post()` after `stop()` is a no-op (asserts? or silently dropped? — pick one and document) — chose **silent drop**, see `runtime.hpp` doc comment on `post()`
- [x] Stress test: a foreign thread posts N envelopes; main thread drains via `run_one`; all envelopes observed in FIFO order
- [ ] Stress test passes under TSan on host — **build wired, runtime blocked in this container**, see Comments

## Blocked by

- `06-runtime-skeleton-lifecycle.md`

## Comments

### 2026-05-14 — from sandcastle agent

**What I built**

- `AllocatorLock` RAII guard in `include/cortexflow/messaging.hpp`. Every `allocate`/`deallocate` call in `make_message`, `MessagePtr::destroy`, and `Envelope::destroy_impl` now holds it. The host `HeapAllocator` already serialises internally — the wrapper exists so non-thread-safe backends (bare-metal slab pools) work correctly under foreign-thread invocation.
- New `make_message_with<T>(MessageAllocator&, args...)` factory that takes an explicit allocator. The original `make_message<T>(args...)` is now a one-liner over it. This lets tests target a `TrackingAllocator` to assert on lock/unlock counters; it's also the natural seam for the future per-type slab factory.
- `runtime.post()` documented as thread-safe with the wake/stop semantics inline. Implemented **silent drop** for posts arriving after `stop()` was observed — re-checked under the queue mutex so `stop()`'s critical section is the unambiguous cutoff. Rationale captured in the comment: boundary threads commonly race with `stop()` (the producer hasn't yet seen its join request); turning that into an assert would make orderly teardown brittle.
- Unit tests (`tests/unit/test_messaging.cpp`): `AllocatorLock` acquire/release, `make_message_with` lock-during-allocate, `MessagePtr`/`Envelope` lock-during-deallocate, and a 64-iteration foreign-thread loop confirming every alloc and dealloc happened with the lock held.
- Integration tests (`tests/integration/test_runtime.cpp`):
  - 5000-envelope single-producer stress with main-thread `run_one` drainer; checks total FIFO ordering.
  - 4-producer × 1000-envelope stress; checks per-producer FIFO (interleaving allowed across producers, monotonic within each).
  - Foreign post wakes a `run()` blocked in CV.
  - Post-after-`stop()` is silently dropped; queue stays empty; runtime is recoverable via `shutdown()` + fresh `start()`.
- CMake: new `CORTEXFLOW_ENABLE_TSAN` option that adds `-fsanitize=thread -g -O1 -fno-omit-frame-pointer` to compile and link flags. Build succeeds with `-DCORTEXFLOW_ENABLE_TSAN=ON` under both `g++ 12.2` and `clang++ 14`.

**What I deferred / could not verify here**

- **TSan stress run**: the TSan-instrumented binary aborts at startup with `tsan_platform_linux.cpp:303 "personality(... | ADDR_NO_RANDOMIZE) != -1"` because the container's seccomp profile blocks the `personality()` syscall (confirmed with a 5-line C reproducer). This is independent of GCC vs Clang and of `TSAN_OPTIONS`. There is no way around it from inside the container without elevated privileges (`--security-opt seccomp=unconfined` or running on the host directly). The build target itself is correct and ready; reviewer needs to invoke it in an environment that allows TSan to run:
  ```sh
  cmake -S . -B build_tsan -DCORTEXFLOW_BUILD_TESTS=ON -DCORTEXFLOW_ENABLE_TSAN=ON
  cmake --build build_tsan -j
  ctest --test-dir build_tsan --output-on-failure
  ```

**What to pay attention to in review**

- The **double-check on `stop_requested_`** in `runtime.post()` is the load-bearing piece for the wake-on-stop semantic. The first check is the fast path (no lock); the second under the mutex is the correctness guarantee, since `stop()` sets the flag inside its own `lock_guard`. If you reorder either, the cutoff becomes racy.
- `AllocatorLock` is intentionally non-movable. If a future allocator backend benefits from a "transferable" lock (e.g., an interrupt-section scope on bare-metal), revisit then.
- `make_message_with` is exposed publicly. The PRD's "single `make_message<T>` factory" still holds in spirit — `make_message` remains the canonical user API. The `_with` variant exists for testability and for the eventual per-pool factory path; happy to rename to `detail::` if you'd rather hide it.
- The 5000-envelope stress test's `Config<DrainBudget<8192>>` is sized so `shutdown()` can finalise without warning. Lower drain budget would still pass the stress assertion but would WARN on shutdown.
- Existing tests (9/9) still pass. New tests added: 6 messaging + 4 runtime integration, all passing.
