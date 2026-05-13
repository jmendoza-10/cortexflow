# Messaging: `Envelope` + `MessagePtr` + `make_message` + pluggable allocator

Status: ready-for-agent
PRD: `docs/prd.md` — Messaging subsystem; user stories 11, 12, 14

## What to build

The messaging primitives. `Envelope` is a value type carrying `from` (nullable sender ID via a sentinel), `to`, the payload `type_id`, and the typed payload (move-only). `MessagePtr<T>` is a moveable handle to an allocated message; `make_message<T>(args...)` is the single construction factory routed through a pluggable allocator. The allocator interface includes locking primitives for thread-safe use from foreign threads. The default host backend is a heap allocator; bare-metal targets later add a per-type compile-time slab pool.

Messages move (never copy) through the system so move-only payloads (`unique_ptr` to OS buffers, etc.) are supported. The single FIFO queue with no priority lanes is owned by the runtime (this slice provides only the value types and allocator).

## Acceptance criteria

- [ ] `Envelope` is movable, not copyable; carries `from` (sentinel for "none"), `to`, `type_id`, payload
- [ ] `MessagePtr<T>` is move-only with RAII destruction routed through the originating allocator
- [ ] `make_message<T>(args...)` perfect-forwards args and returns `MessagePtr<T>`
- [ ] Allocator interface defines `allocate`, `deallocate`, and a lock primitive for thread-safe use
- [ ] Default host heap allocator implementation
- [ ] Unit tests: move-only payload round-trip, sentinel `from`, equality of envelopes by `type_id`
- [ ] Compile-fail test: attempting to copy an `Envelope` or `MessagePtr<T>` does not compile

## Blocked by

- `01-type-name-identity.md` (needs `type_id_t`)
