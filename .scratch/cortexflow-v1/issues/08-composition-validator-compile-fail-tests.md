# Composition validator `static_assert`s + `tests/compile_fail/` harness

Status: ready-for-agent
PRD: `docs/prd.md` — Runtime / composition shape; user stories 2, 55

## What to build

Two pieces:

1. **Composition `static_assert`s** in `Runtime<…>` that fail compilation with clearly-worded messages naming the offending type. Coverage:
   - Duplicate module type in `ModuleList`
   - `send<Target>(msg)` where `Target` is not in `ModuleList`
   - `send<Target>(msg)` where `Target` does not declare a handler for `Msg`
   - (Note: `Owned<K, M>` write-ownership enforcement is *out of scope* for v1 per the PRD — documentation-only.)

2. **`tests/compile_fail/` harness** that compiles each rejection snippet and verifies the build fails with the expected message substring. Each snippet is a small standalone `.cpp` file invoked via CMake/CTest with `expect_compile_fail`.

`static_assert` messages must be the front door for users — wording matters more than usual. Avoid template-error spew.

## Acceptance criteria

- [ ] `static_assert`s in `Runtime` cover the three documented cases with hand-crafted error strings
- [ ] `tests/compile_fail/` directory with one `.cpp` per failure case
- [ ] CTest target that asserts each snippet fails to compile AND that the assertion message includes the expected substring
- [ ] CI runs the compile-fail suite alongside the regular tests
- [ ] Each error message names the offending type and the rule violated (e.g., `"ChargeController declared duplicate in ModuleList"`)

## Blocked by

- `06-runtime-skeleton-lifecycle.md`
