# Cache: RAII `Subscription` + pool + overflow assert + subscribe-during-write semantics

Status: ready-for-agent
PRD: `docs/prd.md` — Cache subsystem; user stories 24, 25, 26, 28

## What to build

The full subscription lifecycle. `Subscription` is a RAII handle returned by `cache.subscribe<K>(subscriber_id)`; its destructor releases the slot. The subscription pool size is the `MaxSubscriptions<N>` declared in `Config`; pool overflow is a `CORTEXFLOW_ASSERT` (system failure, not a runtime error).

Re-entrancy semantics: a subscription created during a writer's handler does NOT observe that write — the subscriber list is captured at the start of the writer's `set`, and any subscriptions added afterwards only see subsequent changes. This keeps the run-to-completion contract clean.

This slice replaces the placeholder registration mechanism from slice 9 with the real RAII-backed registry.

## Acceptance criteria

- [ ] `cache.subscribe<K>(subscriber_id)` returns a `Subscription` RAII handle
- [ ] Destruction of the `Subscription` releases the slot synchronously
- [ ] Subscription pool size is `Config::MaxSubscriptions::value`
- [ ] Overflow on `subscribe` triggers `CORTEXFLOW_ASSERT` naming the cap and the key
- [ ] Subscribe-during-write does not see the in-flight write (captured list semantics)
- [ ] Integration tests: RAII drop frees the slot (subsequent subscribe succeeds); overflow asserts; subscribe-during-write contract
- [ ] `Subscription` is movable, not copyable; moving transfers ownership cleanly

## Blocked by

- `09-cache-slots-fanout.md`
