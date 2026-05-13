# Identity: `type_name<T>()` + cross-toolchain canonical-form CI fixture

Status: ready-for-agent
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
