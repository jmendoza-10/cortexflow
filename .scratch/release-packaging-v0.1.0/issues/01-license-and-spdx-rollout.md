# License + SPDX rollout: Apache-2.0 across the repository

Status: ready-for-agent
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 16, 17, 18, 19
ADR: `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

Apply Apache-2.0 across the CortexFlow repository so a consumer's legal team and license-scanning tooling can attribute the code correctly.

Three additions and one sweep:

1. **`LICENSE`** at the repo root: the verbatim canonical Apache License, Version 2.0 text (the version published by the Apache Software Foundation at apache.org/licenses/LICENSE-2.0.txt). No appendix substitutions, no commentary inside the file.

2. **`AUTHORS`** at the repo root: a one-per-line listing of contributors who hold copyright. First (and currently only) line is the project author. The collective copyright holder string referenced from per-file SPDX headers is "The CortexFlow Authors" — `AUTHORS` is what that collective resolves to.

3. **SPDX headers on every `.hpp` and `.cpp` file in the tree.** Two lines at the top of each file, before any other content:

   ```
   // Copyright 2026 The CortexFlow Authors
   // SPDX-License-Identifier: Apache-2.0
   ```

   The year is `2026`. Files that already carry a copyright-style banner have that banner replaced by these two lines (license attribution is conveyed exclusively via SPDX). No file carries the full Apache-2.0 boilerplate inline — that lives once in `LICENSE`.

   Scope: every `.hpp` and `.cpp` under `include/`, `src/`, `platform/`, `tests/`, and `examples/`. Generated files (none currently) and third-party files (none currently) are exempt.

4. **A CTest-registered regression guard** that fails the build if any `.hpp` or `.cpp` in the scoped directories is missing the two SPDX lines. Implement as a small shell-script test wired into `tests/CMakeLists.txt` (or its closest equivalent); it should be cheap (sub-second), have no external dependencies beyond `find` and `grep`, and produce a clear failure message naming the offending file(s).

## Acceptance criteria

- [ ] `LICENSE` exists at the repo root and contains the verbatim canonical Apache-2.0 license text
- [ ] `AUTHORS` exists at the repo root, with at least one line identifying the project author
- [ ] Every `.hpp` and `.cpp` under `include/`, `src/`, `platform/`, `tests/`, and `examples/` carries the two-line SPDX header as its first two lines, with `Copyright 2026 The CortexFlow Authors` and `SPDX-License-Identifier: Apache-2.0`
- [ ] No file in the tree carries the full Apache-2.0 boilerplate inline (it appears only in `LICENSE`)
- [ ] A new CTest case checks SPDX-header presence across the scoped directories and fails with a clear message when a file is missing the header
- [ ] The full build/test matrix (host + posix, gcc + clang) passes with the new test case enabled
- [ ] No header file's compilation behavior changes — the SPDX lines are pure `//`-comments and produce no preprocessor side effects
- [ ] `git grep -L "SPDX-License-Identifier"` over the scoped directories returns no files

## Blocked by

None — can start immediately.

## Notes

- The SPDX line is the legally-meaningful one for tooling. The `Copyright` line above it is for human readability.
- "The CortexFlow Authors" is the collective copyright holder string by deliberate choice (see ADR-0023 → *License*). Future contributors add their name to `AUTHORS` rather than editing per-file headers, so adding a contributor is a one-line change.
- If you find a `.cpp` or `.hpp` that doesn't fit the SPDX rule cleanly (e.g. a copy of a third-party header — there don't appear to be any in `third_party/` yet), flag it in the PR description rather than silently exempting it.
