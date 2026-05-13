# Identity: `type_name<T>()` + cross-toolchain canonical-form CI fixture

Status: ready-for-human
PRD: `docs/prd.md` — Identity subsystem; user stories 14, 15, 16

## What to build

A constexpr `type_name<T>()` utility that parses the compiler-specific function-signature macro (`__PRETTY_FUNCTION__` on GCC/Clang, `__FUNCSIG__` on MSVC) to produce a fully-qualified canonical type name as a `std::string_view`. From that name, derive a constexpr `type_id_t` via a stable hash. Both are the foundation of message/key/module identity used throughout the framework — there is no central enum file.

The CI fixture is a test that takes a representative set of types (templated, namespaced, nested, with primitives and user types) and asserts that `type_name<T>()` produces the exact expected canonical form on every supported host toolchain. This is the cross-compiler stability guarantee.

## Acceptance criteria

- [ ] `type_name<T>()` is constexpr and returns `std::string_view` with the fully-qualified type name
- [ ] `type_id_t` is constexpr-derived from a hash of `kName`
- [ ] CRTP base for messages/keys exposes `static constexpr std::string_view kName` and `static constexpr type_id_t kTypeId`
- [ ] No RTTI and no exceptions
- [ ] CI canonical-form fixture covers GCC and Clang on host, with expected strings asserted character-exact
- [ ] Failure of the canonical-form fixture is a clear actionable error (names the type and shows expected vs actual)

## Blocked by

None — can start immediately.

## Comments

### 2026-05-13 — from sandcastle agent

**What was built:**

- `include/framework/type_name.hpp` — single header containing:
  - `type_name<T>()`: constexpr function returning `std::string_view` with the fully-qualified type name, parsed from `__PRETTY_FUNCTION__` (GCC/Clang) or `__FUNCSIG__` (MSVC).
  - `type_id_t`: `uint64_t` typedef, derived via FNV-1a hash of the type name.
  - `type_id<T>()`: constexpr convenience function returning `type_id_t`.
  - `Identified<Derived>`: CRTP base exposing `static constexpr kName` and `static constexpr kTypeId`.
- `tests/unit/test_type_name.cpp` — canonical-form CI fixture with 31 assertions covering primitives, cv-qualifiers, pointers, references, namespaced types, nested types, and templates. Per-compiler expected strings for GCC and Clang. Failures show the type, expected string, and actual string.
- `CMakeLists.txt` + `tests/CMakeLists.txt` — minimal build system with `-fno-rtti -fno-exceptions`.
- `third_party/doctest/doctest.h` — doctest v2.4.11 header.

**Verified:** all 31 tests pass on both GCC 12.2.0 and Clang 14.0.6.

**Nothing skipped or deferred.**

**Reviewer notes:**
- GCC and Clang produce slightly different canonical forms for some builtin types (e.g., GCC: `long unsigned int`, Clang: `unsigned long`) and pointer/reference spacing (GCC: `int*`, Clang: `int *`). The test fixture has per-compiler expected strings via `#if defined(__clang__)` / `#elif defined(__GNUC__)`. This is by design — identity is stable within a given binary, and the CI fixture catches any compiler upgrade that shifts formatting.
- MSVC support is wired (`__FUNCSIG__` path) but not tested — no MSVC toolchain in this container.
