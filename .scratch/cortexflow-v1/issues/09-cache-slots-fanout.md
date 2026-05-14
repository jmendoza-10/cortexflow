# Cache: typed slots + get/set + change detection + `KeyChanged<K>` fanout

Status: ready-for-human
PRD: `docs/prd.md` — Cache subsystem; user stories 20, 21, 22, 23, 27

## What to build

The typed key-value cache integrated into the runtime. Each cache key is a class declaring `using value_type = …;`. Reads return `std::optional<value_type>` — empty until first write — so callers can distinguish "unset" from a real value. Writes compare to the previous value via `operator==`; only a real change fires notifications. On change, `KeyChanged<K>` is posted as a normal envelope through the queue to all subscribers in registration order (the subscription registry itself is a placeholder until slice 10 — provide a minimal in-place registration mechanism here that slice 10 replaces).

`Owned<K, M>` is parsed by the composition but is documentation-only in v1 (no `static_assert`). Subscribers always receive the canonical `KeyChanged<K>`; no custom subscription→message mapping.

## Acceptance criteria

- [ ] Each cache key type declares `value_type`; cache infers slot type from the key
- [ ] `cache.get<K>()` returns `std::optional<value_type>`; empty until first write
- [ ] `cache.set<K>(value)` compares to the previous slot value via `operator==`; equal writes are silent
- [ ] On real change, `KeyChanged<K>` envelopes are enqueued to all subscribers in registration order
- [ ] Subscribers receive `KeyChanged<K>` asynchronously through the queue (never as a synchronous callback during a writer's handler)
- [ ] `Owned<K, M>` accepted in `CacheKeyList` and exposed for documentation purposes
- [ ] Integration tests covering: read-before-write returns empty, set round-trips, idempotent set does not notify, fanout order matches registration order

## Blocked by

- `06-runtime-skeleton-lifecycle.md`

## Comments

2026-05-14 — from sandcastle agent

Built:
- `include/cortexflow/cache.hpp` — new header containing `Owned<K, M>`,
  `CacheKeyList<...>`, `KeyChanged<K>`, and the `Cache<CacheKeyListT,
  MaxSubscriptions>` template. `Owned<K, M>` and `CacheKeyList<...>` moved
  here from `runtime.hpp`; both still flow through the composition
  unchanged.
- `Cache::get<K>()` returns `std::optional<typename K::value_type>` (empty
  until the first write). `Cache::set<K>(value)` compares to the prior
  value with `operator==`; equal writes are silent.
- On a real change, `Cache::fanout<K>` enqueues a `KeyChanged<K>` envelope
  for each subscriber for that key, in registration order, via the
  runtime's normal `post()` path. Subscribers always receive the canonical
  message asynchronously through the queue.
- `KeyChanged<K>` carries the new value plus `std::optional<value_type>
  old_value` (empty on first write). Architecture §7.6 names both fields.
- `Owned<K, M>` is accepted in `CacheKeyList<...>` and the cache uses a
  `key_of_t<>` trait to extract `K` transparently. No `static_assert` on
  ownership (deliberately deferred per PRD Out-of-Scope and architecture
  §7.4).
- Runtime now owns the cache as `std::optional<cache_type>`, constructs it
  in `start()` before module construction so `on_start()` may subscribe or
  seed values, and resets it in `shutdown()` for symmetric reuse across
  start/shutdown cycles. Cache's post sink is bound to
  `Runtime::post_trampoline` so envelopes route through the same FIFO and
  thread-safe ingress as any other producer.

Subscription registry — minimal in-place mechanism as the issue scope
asks. Backed by a fixed-size `Subscriber[MaxSubscriptions]` array keyed
on `type_id<K>`; overflow asserts. Two `subscribe<K, …>` forms are
exposed: a templated `subscribe<K, Subscriber>()` that reads the
subscriber's `type_id` at compile time, and `subscribe<K>(type_id_t)` for
hand-rolled IDs in tests. Slice 10 will replace this with the RAII
`Subscription` handle backed by a slot pool.

Tests (in `tests/integration/test_cache.cpp`, all passing under both
`DISPATCH` and `FULL` trace levels):
- read-before-write returns empty,
- set round-trips through `get`,
- first write fires `KeyChanged<K>` with empty `old_value`,
- idempotent set does not enqueue,
- delivery is asynchronous (handler not invoked before `set` returns),
- fanout order matches subscription registration order (not module
  declaration order — verified by registering C, A, B and checking
  receipt order),
- fanout targets only subscribers of the changed key (multi-key
  isolation),
- cache slots reset between `shutdown()` and a subsequent `start()`.

Skipped / deferred:
- No FULL-level trace point for cache writes — architecture §12 lists
  it under FULL coverage but it falls outside this slice's acceptance
  criteria. Easy one-liner to add later.
- "Subscribe-during-write does not see this write" (user story 28) is
  naturally satisfied by snapshotting `num_subs_` at fanout start, but
  there is no test for it in this slice — it pairs naturally with the
  RAII Subscription work in slice 10 where the path becomes
  user-reachable.
- No subscription-pool overflow test in this slice (the trip-asserts
  pattern from `test_runtime.cpp` would work; left for slice 10 which
  owns the RAII handle and its pool semantics).

Reviewer notes:
- `Cache::set` takes `typename K::value_type` by value (move-friendly,
  one copy when the value is preserved into the slot). The `==` short
  circuit happens before the move, so equal writes don't pay the move.
- The fanout snapshot of `num_subs_` is defensive — there is no in-loop
  re-entry to user code via `make_message` or `post()` in the current
  design, but I kept the snapshot so the subscribe-during-write
  invariant cannot regress silently when slice 10 adds the RAII path.
- `Cache` is move/copy-deleted to match `Runtime`'s policy.
