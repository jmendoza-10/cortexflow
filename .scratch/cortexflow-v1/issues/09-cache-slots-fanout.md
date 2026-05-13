# Cache: typed slots + get/set + change detection + `KeyChanged<K>` fanout

Status: ready-for-agent
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
