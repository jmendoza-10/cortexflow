# Messaging: `Envelope` + `MessagePtr` + `make_message` + pluggable allocator

Status: ready-for-human
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

## Comments

### 2026-05-13 — from sandcastle agent

**What was built:**

All acceptance criteria are satisfied:

- **`Envelope`** (`include/framework/messaging.hpp`): move-only value type carrying `from` (sentinel `kNoSender = 0`), `to`, `type_id` (auto-derived from payload type via `type_id<T>()`), and a type-erased payload with RAII destruction routed through the originating allocator. Copy constructor/assignment are `= delete`.
- **`MessagePtr<T>`** (`include/framework/messaging.hpp`): move-only RAII handle storing a `T*` and the originating `MessageAllocator*`. Destruction calls `T::~T()` then `allocator->deallocate()`. Copy is deleted.
- **`make_message<T>(args...)`** (`include/framework/messaging.hpp`): perfect-forwarding factory that allocates via `default_allocator()`, placement-news the object, and returns `MessagePtr<T>`.
- **`MessageAllocator`** (`include/framework/messaging.hpp`): abstract interface with `allocate`, `deallocate`, `lock`, `unlock`. Protected non-virtual destructor (allocators are singletons, never deleted through base pointer).
- **`HeapAllocator`** (`src/framework/messaging.cpp`): host default using aligned `::operator new` (nothrow + `FRAMEWORK_ASSERT`). Mutex stored as file-local static to keep `<mutex>` out of the public header.
- **Unit tests** (`tests/unit/test_messaging.cpp`): 20 test cases covering move-only payload round-trip, sentinel `from`, type_id equality/inequality across envelopes, allocator routing via `TrackingAllocator`, static non-copyability assertions, and `HeapAllocator` lock/unlock.
- **Compile-fail tests** (`tests/compile_fail/copy_envelope.cpp`, `copy_message_ptr.cpp`): verify the compiler rejects copy of `Envelope` and `MessagePtr<T>`.

All 7 tests pass (including existing tests for type_name, assert, and trace).

**Design decisions:**

- `kNoSender = 0` is safe as a sentinel because FNV-1a of any real type name is astronomically unlikely to be zero.
- `MessageAllocator` uses virtual dispatch (works fine with `-fno-rtti`; only `dynamic_cast`/`typeid` require RTTI). This allows `MessagePtr<T>` to store an allocator pointer for proper RAII cleanup across different allocator instances.
- `default_allocator()` is a non-weak function returning `HeapAllocator::instance()`. Platform backends will override by linking a different source file or cmake target, consistent with the typedef-swap pattern.
- `Envelope` stores a function pointer (`destroy_fn_`) for type-erased payload destruction rather than using `std::function` or virtual dispatch on the payload — zero overhead, no allocation.

**Nothing was skipped or deferred.**
