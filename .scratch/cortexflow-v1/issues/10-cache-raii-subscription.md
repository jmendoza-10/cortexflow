# Cache: RAII `Subscription` + pool + overflow assert + subscribe-during-write semantics

Status: ready-for-human
PRD: `docs/prd.md` — Cache subsystem; user stories 24, 25, 26, 28

## What to build

The full subscription lifecycle. `Subscription` is a RAII handle returned by `cache.subscribe<K>(subscriber_id)`; its destructor releases the slot. The subscription pool size is the `MaxSubscriptions<N>` declared in `Config`; pool overflow is a `CORTEXFLOW_ASSERT` (system failure, not a runtime error).

Re-entrancy semantics: a subscription created during a writer's handler does NOT observe that write — the subscriber list is captured at the start of the writer's `set`, and any subscriptions added afterwards only see subsequent changes. This keeps the run-to-completion contract clean.

This slice replaces the placeholder registration mechanism from slice 9 with the real RAII-backed registry.

## Acceptance criteria

- [x] `cache.subscribe<K>(subscriber_id)` returns a `Subscription` RAII handle
- [x] Destruction of the `Subscription` releases the slot synchronously
- [x] Subscription pool size is `Config::MaxSubscriptions::value`
- [x] Overflow on `subscribe` triggers `CORTEXFLOW_ASSERT` naming the cap and the key
- [x] Subscribe-during-write does not see the in-flight write (captured list semantics)
- [x] Integration tests: RAII drop frees the slot (subsequent subscribe succeeds); overflow asserts; subscribe-during-write contract
- [x] `Subscription` is movable, not copyable; moving transfers ownership cleanly

## Blocked by

- `09-cache-slots-fanout.md`

## Comments

### 2026-05-14 — from sandcastle agent

**What I built**

- New header `include/cortexflow/subscription.hpp` with a non-template, move-only `Subscription` class. It holds an opaque release trampoline pointer, a context pointer, and a slot index; the destructor calls the trampoline iff the handle is still live. Default-constructed and moved-from handles are inert (destructor is a no-op), which is what makes move-assignment-overwrite work cleanly.
- Reworked `cortexflow/cache.hpp` subscription pool from append-only registration to a stable-slot pool. Slots in `subs_[N]` carry an `active` flag and never move; an auxiliary `order_[N]` array tracks active slot indices in registration order. `subscribe<K>` finds the lowest free slot and appends to the order array; `release_slot` clears the slot and shifts the trailing entries of the order array down. Fanout iterates `order_[0..num_active_]` so registration-order semantics survive drop/reuse cycles.
- Overflow path: when `num_active_ >= MaxSubscriptions`, the cache formats `"Cache subscription pool overflow (capacity=%zu, key=%.*s)"` into a `static` buffer with `std::snprintf` (using `type_name<K>()` for the key portion) and passes the resulting `const char*` to `CORTEXFLOW_ASSERT(false, ...)`. The static buffer outlives the asserting stack frame, which matters because the test fault handler `longjmp`s back to the test and then reads the reason pointer.
- Fanout now snapshots `num_active_` at the start of the loop. Iteration is bounded by that snapshot, so subscriptions added after `set()` enters fanout but before it returns do not see the in-flight write. In practice this also covers the user-visible "subscribe after `set` returns, before `run_one`" case because `set` has already enqueued only the original subscribers' envelopes by the time it returns.
- `tests/integration/test_cache.cpp` now exercises slice 10. New cases: subscribe returns an active handle; destructor releases the slot; `reset()` is eager and idempotent; move-construct / move-assign transfer ownership; move-assigning over a live handle releases the prior slot first; drop-then-re-subscribe preserves fanout order; pool overflow asserts with `capacity=3` and `Speed` in the reason string; subscribe-during-write doesn't deliver KeyChanged for the in-flight write. Existing cases were updated to capture the returned `Subscription` (the API now `[[nodiscard]]`-returns it).
- The fault handler in `test_cache.cpp` was rewritten to support both abort (default) and longjmp-capture (set `s_capture_fault = true` before the asserting call). The pattern follows `tests/unit/test_assert.cpp`.

**Verification**

- Default-trace build (`DISPATCH`): `ctest` shows 13/13 green; `test_cache` runs 18 cases / 93 assertions.
- `FULL`-trace build: 13/13 green; `test_cache` 18/93.
- Clang build: 13/13 green.
- TSAN build failed with `ThreadSanitizer: CHECK failed: tsan_platform_linux.cpp:315 "personality(...) != -1"` — that's a container/ASLR issue unrelated to this change (TSAN can't disable ASLR on this kernel). Non-TSAN builds are clean.

**Notes for the reviewer**

- `find_free_slot` is `O(MaxSubscriptions)` per subscribe. With the default `MaxSubscriptions<16>` (or the design-doc example `<32>`) this is fine; if/when the pool grows large enough to matter, a freelist head index would drop it to O(1).
- The order-array shift on release is `O(num_active_)`. Same tradeoff — small N, in-order layout, no allocation churn.
- The overflow message uses a `static` (process-global, not thread-local) buffer. Subscription overflow is a system invariant violation (architecture §13.1), so the assumption is that exactly one fault gets emitted before the platform handler aborts (or, in tests, `longjmp`s back). If a future host platform handler is reentrant, that buffer would need to become thread-local.
- The `subscribe<K>(type_id_t)` overload is still public so non-module callers (e.g. test code) can pass a synthetic `type_id`. The convenience `subscribe<K, Subscriber>()` overload forwards through it.
- I left a defensive `for(;;){}` after the assert in `overflow_fault` because `CORTEXFLOW_ASSERT(false, runtime_str)` isn't statically `[[noreturn]]` from the compiler's perspective (the conditional shape `(cond) ? (void)0 : fault(...)` returns void). Some clean-build configurations would otherwise complain about a missing return path in a `[[noreturn]]` function. It compiles to nothing under `-O1` and up.
- `Subscription` is intentionally untyped on the cache's template parameters so flow state-locals (slice 14) can hold one as a member without dragging the cache instantiation through the flow type. The trampoline-pointer approach was chosen over a templated-friend pattern for the same reason.
