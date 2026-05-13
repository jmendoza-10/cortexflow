# Rename: `framework` → `cortexflow` (namespace, library, headers, macros)

Status: ready-for-agent
PRD: N/A — refactoring for naming alignment with the project

## What to build

The library and namespace are currently named `framework` — a placeholder from the initial bring-up. Rename every "framework" identifier in the codebase to "cortexflow" so the project's identity is visible in every file an engineer (or agent) opens. **No behavior changes** — identifiers only.

Should run **before** any further implementation issues (#04 onward), otherwise each new feature lands in `framework::` and has to be refactored again. The open issue files that describe future work must also be updated, or the next agent will re-introduce `framework` based on their text.

## Acceptance criteria

- [ ] **C++ namespace**: `namespace framework` → `namespace cortexflow` in every `.hpp` and `.cpp` (declarations, definitions, fully-qualified uses like `::framework::Foo`).
- [ ] **Header directory**: `include/framework/` → `include/cortexflow/`. Use `git mv` to preserve history. Update every `#include <framework/...>` to `#include <cortexflow/...>` (and `"framework/..."` form if present).
- [ ] **Source directory**: `src/framework/` → `src/cortexflow/`. Use `git mv`. Update CMake source listings.
- [ ] **CMake library target**: `add_library(framework ...)` → `add_library(cortexflow ...)`. Update every `target_link_libraries(... framework)`. (Sharing the name with `project(cortexflow ...)` is fine; CMake handles it.)
- [ ] **CMake cache variables / options**:
  - `FRAMEWORK_BUILD_TESTS` → `CORTEXFLOW_BUILD_TESTS`
  - `FRAMEWORK_TRACE_LEVEL` → `CORTEXFLOW_TRACE_LEVEL`
  - `FRAMEWORK_TRACE_LEVEL_VALUE` → `CORTEXFLOW_TRACE_LEVEL_VALUE`
- [ ] **C++ macros**:
  - `FRAMEWORK_TRACE`, `FRAMEWORK_TRACE_ERROR`, `FRAMEWORK_TRACE_WARN`, `FRAMEWORK_TRACE_INFO`, `FRAMEWORK_TRACE_DISPATCH`, `FRAMEWORK_TRACE_FULL` → corresponding `CORTEXFLOW_TRACE*` names
  - `FRAMEWORK_ASSERT` → `CORTEXFLOW_ASSERT` (from issue #02)
  - Any other `FRAMEWORK_*` macro discovered during the sweep
- [ ] **Docs** (current-state text only — don't rewrite ADRs' historical decision wording): `README.md`, `docs/prd.md`, `docs/architecture.md`, `docs/adr/README.md`, `CLAUDE.md`.
- [ ] **Open issue files in `.scratch/cortexflow-v1/issues/`**: every issue with `Status: ready-for-agent` whose body mentions `framework::`, `FRAMEWORK_*`, `include/framework/...`, or `src/framework/...` must be updated to the new names. This prevents future agent runs from re-introducing `framework`. Verify with `grep -l "framework" .scratch/cortexflow-v1/issues/*.md` and inspect each match. Leave already-completed issues' bodies alone (history).
- [ ] **Issue filename `02-framework-assert-fault-path.md`** → `02-cortexflow-assert-fault-path.md`. Use `git mv`. (#02 is already merged; this is a cosmetic rename to keep mental model consistent.)
- [ ] **CI workflow `.github/workflows/ci.yml`**: update any references to old target/option names.
- [ ] **Build & tests still pass** on host:
  ```
  cmake -S . -B build -G Ninja -DCORTEXFLOW_BUILD_TESTS=ON
  cmake --build build
  ctest --test-dir build --output-on-failure
  ```
- [ ] **Final sweep is clean**: `grep -rln "framework" --include='*.hpp' --include='*.cpp' --include='CMakeLists.txt' .` returns no matches. Top-level `grep -rln "framework" .` may legitimately show ADR text and historical issue files — that's fine.

## Notes for the agent

- **Use `git mv`** (not plain `mv`) for directory moves and file renames so git can track them as renames and preserve blame.
- **Do not edit historical content**: completed issue files (`Status: ready-for-human`), commit messages, ADRs' decision rationale. Only update text describing *current* state.
- **One commit per logical chunk** is easier to review: e.g. headers+namespace, then sources, then CMake/macros, then docs, then open-issue-body sweep. Single squash-commit also acceptable if the agent prefers.
- The rename target shares the CMake `project()` name — that's fine, intentional.

## Blocked by

None — can start immediately. **Recommend running before issue #04 and beyond** so new features inherit the renamed namespace from the start.
