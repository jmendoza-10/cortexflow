# Build surface: `cortexflow::cortexflow` ALIAS, declared C++17 feature, reconciled docs

Status: ready-for-agent
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 1, 8, 12, 20, 30
ADR: `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

Two CMake additions and a documentation reconciliation. Together they make the **Build surface** (CONTEXT.md → *Build surface*) actually match what the docs already claim about it, and ensure the C++17 requirement is expressed on the `cortexflow` target itself rather than depending on whatever global default the consumer's project happens to use.

### 1. Add the `cortexflow::cortexflow` ALIAS target

Today the only valid target name is `cortexflow`. CONTEXT.md's Build-surface definition references `cortexflow::cortexflow`, but no `add_library(... ALIAS ...)` exists. Consumers writing `target_link_libraries(my_app PRIVATE cortexflow::cortexflow)` would currently fail.

Add `add_library(cortexflow::cortexflow ALIAS cortexflow)` immediately after the `add_library(cortexflow STATIC ...)` declaration in the top-level `CMakeLists.txt`. After this change, the namespaced form is the canonical consumer interface: typos in the namespaced form fail at configure time (CMake errors on missing `::`-prefixed targets) instead of silently falling through to a `-l<name>` link directive at link time, which is the conventional CMake-library hygiene rationale.

The un-namespaced `cortexflow` target remains valid — the ALIAS is additive, not a replacement. Internal use sites within the CortexFlow build (the existing `add_subdirectory(examples/minimal_app)` and the tests under `tests/`) need not be changed; they reference `cortexflow` directly today and that continues to work.

### 2. Declare C++17 as a PUBLIC compile feature on the target

Currently `CMAKE_CXX_STANDARD 17` is set at the top-level CMakeLists.txt's directory scope. That works when CortexFlow is the top-level project but does not propagate to consumers under sub-project consumption — `CMAKE_CXX_STANDARD` is a directory-scoped variable, not a target property, so a consumer whose default standard is C++14 or C++23 could compile CortexFlow's headers under a mismatched standard.

Add `target_compile_features(cortexflow PUBLIC cxx_std_17)` to the top-level `CMakeLists.txt` immediately after the `add_library(cortexflow STATIC ...)` block. PUBLIC because the standard requirement is part of the header-level contract — consumers that include `<cortexflow/...hpp>` must compile under C++17 or later.

The existing global `CMAKE_CXX_STANDARD 17` / `CMAKE_CXX_STANDARD_REQUIRED ON` / `CMAKE_CXX_EXTENSIONS OFF` settings can stay (they apply to top-level builds, where they correctly govern *our* sources). The new `target_compile_features` line is the load-bearing addition for sub-project consumption.

### 3. Reconcile documentation references

After (1) lands, standardize on `cortexflow::cortexflow` as the canonical consumer-facing target name across the project docs:

- **`docs/adr/0023-release-packaging-strategy.md`** — line 57's "Build surface" bullet currently says `the cortexflow CMake target name`. Update to `the cortexflow::cortexflow CMake target (an ALIAS for cortexflow); both names link the same library, but the namespaced form is the canonical consumer interface`. Apply the same naming everywhere `cortexflow` appears as a target name in the ADR.
- **`CONTEXT.md`** — already uses `cortexflow::cortexflow`. No change needed; verify only.
- **`.scratch/release-packaging-v0.1.0/PRD.md`** — out of scope for this slice. The PRD references the un-namespaced `cortexflow` target name in two places. That is a known doc drift; it is not load-bearing (the PRD is parent context for these slices, not a consumer-facing doc) and will be reconciled as part of normal PRD-maintenance hygiene rather than by this slice. **Do not modify the PRD.**

### Knock-on effects on sibling slices

- **Slice 3 (FetchContent smoke test)** — when implemented, the smoke test's `target_link_libraries` should reference `cortexflow::cortexflow` (the canonical form) and, as an additional verification, also exercise the un-namespaced `cortexflow` form to confirm the ALIAS works. Both should compile and link successfully.
- **Slice 5 (README consumer docs)** — when implemented, the "Consuming CortexFlow" section's FetchContent stanza should use `target_link_libraries(my_app PRIVATE cortexflow::cortexflow)`. Slice 5 is now blocked by this slice (Slice 5's issue file should be updated to reflect the new blocker; this slice's acceptance criteria includes that edit).

## Acceptance criteria

- [ ] The top-level `CMakeLists.txt` declares `add_library(cortexflow::cortexflow ALIAS cortexflow)` immediately after the existing `add_library(cortexflow STATIC ...)` block
- [ ] The top-level `CMakeLists.txt` declares `target_compile_features(cortexflow PUBLIC cxx_std_17)` immediately after the `add_library(cortexflow STATIC ...)` block
- [ ] The existing global `CMAKE_CXX_STANDARD 17` setting is preserved (it still governs top-level builds correctly)
- [ ] `docs/adr/0023-release-packaging-strategy.md` is updated to reference `cortexflow::cortexflow` as the canonical target name everywhere a target name appears; the Build-surface bullet explicitly notes the ALIAS relationship
- [ ] `CONTEXT.md` is verified to already use `cortexflow::cortexflow` consistently (no change expected)
- [ ] `.scratch/release-packaging-v0.1.0/issues/05-readme-consumer-docs.md` is updated so its "Blocked by" section lists this slice (slice 7) in addition to slices 1 and 2
- [ ] The existing CI build/test matrix (host + posix × gcc + clang, with `CORTEXFLOW_BUILD_TESTS=ON`) passes end-to-end
- [ ] A targeted manual check: a tiny scratch CMakeLists with `add_subdirectory(/path/to/cortexflow)` plus `target_link_libraries(my_app PRIVATE cortexflow::cortexflow)` configures and builds; the same with `target_link_libraries(my_app PRIVATE cortexflow)` (un-namespaced) also configures and builds — confirming both forms work
- [ ] No source file under `src/cortexflow/` or `platform/*/` is modified — this is a CMake-and-docs-only slice
- [ ] The `examples/minimal_app` build, which currently links `cortexflow` directly, continues to compile (proving the ALIAS is additive, not replacing the underlying target)
- [ ] `.scratch/release-packaging-v0.1.0/PRD.md` is **not** modified (per the constraint above)

## Blocked by

None — can start immediately, in parallel with slices 1 and 2.

Ideally lands **before or alongside slice 3** so the smoke test can verify both the namespaced and un-namespaced forms work; and **before slice 5** so the README documents the canonical namespaced form.

## Notes

- The ALIAS is a zero-cost CMake construct — no runtime, no link-time, no symbol-table impact. It exists only at CMake's target-resolution layer.
- `target_compile_features(... PUBLIC cxx_std_17)` is the right modern-CMake way to express a header-level standard requirement. It does **not** force the consumer's standard above C++17 — it requires *at least* C++17, leaving the consumer free to use a newer standard if they wish. This matches CortexFlow's intent (we use C++17 features; we don't care if the consumer is on C++20 or C++23).
- A future refinement (out of scope for v0.1.0) would be to remove the global `CMAKE_CXX_STANDARD 17` setting entirely and rely exclusively on `target_compile_features` for the requirement. That's deferred because the global setting also governs `examples/` and `tests/` builds, which would need their own `target_compile_features` declarations to compensate. Net-net, the global stays for now; the target-level declaration is the addition.
- If you find that `CONTEXT.md` is inconsistent (some places `cortexflow`, some places `cortexflow::cortexflow`), reconcile to `cortexflow::cortexflow` and call it out in the PR description.
