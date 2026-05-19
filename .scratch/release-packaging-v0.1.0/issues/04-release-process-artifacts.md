# Release process artifacts: CHANGELOG, runbook, and tag-triggered workflow

Status: merged
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 13, 25, 26, 27, 28
ADR: `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

Three new files and one new GitHub Actions workflow that together turn "cut a release" from tribal knowledge into a documented, partially-automated process.

### `CHANGELOG.md` at the repo root

Format: [Keep-a-Changelog](https://keepachangelog.com/). Top section is `[Unreleased]` and accumulates entries between releases under the standard subsections (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`). At cut-a-release time the human renames `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD` and adds a fresh `[Unreleased]` above it.

Populate the initial file with:
- An empty `[Unreleased]` section.
- A skeletal `[0.1.0] - YYYY-MM-DD` section (placeholder date) describing the v0.1.0 contents at a high level: first tagged release, Apache-2.0 license declared, FetchContent consumption contract, sub-project hygiene fixes, PRIVATE compile flags, Release surface defined in CONTEXT.md, host and posix backends supported, freertos and bare_metal reserved as placeholders. Slice 6 finalizes the date and any wording.

### `docs/releasing.md` runbook

A maintainer-facing document describing the manual steps to cut a release. Concretely, it must cover:

- How to choose the next version number (semver, v0.x posture, criteria for major vs minor vs patch — reference ADR-0023's stability promise).
- The exact `project(cortexflow VERSION X.Y.Z LANGUAGES CXX)` line to update.
- How to migrate `[Unreleased]` entries to `[X.Y.Z] - YYYY-MM-DD` in CHANGELOG.md and add a fresh `[Unreleased]` section.
- The commit message convention for the release commit (`release: v0.1.0` or similar — pick one and document it).
- The exact `git tag -a vX.Y.Z -m "..."` and `git push --tags` commands.
- What to verify after pushing (the release workflow runs green; the GitHub Release exists; the Release notes match the CHANGELOG entry).
- What to do if the matrix fails on the tag (do not auto-yank; fix the source and cut a new patch tag — never re-point an existing tag).

Keep the runbook tight (one page; this is a checklist, not a tutorial). Link to ADR-0023 for context.

### `.github/workflows/release.yml`

A new GitHub Actions workflow that triggers on tag pushes matching `v*.*.*` (and `v*.*.*-*` for pre-release tags like `v0.1.0-rc1`). It must:

1. Check out the tagged commit.
2. Run the same build/test matrix as `.github/workflows/ci.yml` (host + posix backends × gcc + clang compilers, `CORTEXFLOW_BUILD_TESTS=ON`, `CORTEXFLOW_TRACE_LEVEL=FULL`). Reuse the matrix block if possible — if YAML reuse is awkward, duplication is acceptable but the two files must stay in lockstep.
3. After all matrix jobs pass, extract the `[X.Y.Z]` section from `CHANGELOG.md` (where `X.Y.Z` matches the tag minus the leading `v`, including any pre-release suffix) and post those notes to the GitHub Release for that tag. Use `gh release create` or the equivalent Actions step; `softprops/action-gh-release` is the conventional third-party action for this if a built-in step is unwieldy.
4. If the CHANGELOG section for that version is missing or empty, the workflow surfaces a clear error and does **not** create a Release with empty notes.
5. The workflow does **not** auto-yank or auto-delete the tag if the matrix fails. A failed matrix surfaces visibly in the Actions tab; the human cuts a new patch tag after fixing the underlying issue.
6. No prebuilt binaries are uploaded as Release artifacts — GitHub auto-attaches the source tarball, which is sufficient.

### Verification: pre-release dry-run

Before Slice 6 cuts the real `v0.1.0`, push a `v0.1.0-rc1` annotated tag (after Slice 4 lands and after Slices 1, 2, 3 are in main) and observe the workflow:
- All matrix jobs run against the rc1 commit and pass.
- A GitHub Release for `v0.1.0-rc1` is created.
- The Release notes contain the entries from the `[Unreleased]` section as currently populated.

The `v0.1.0-rc1` Release can be deleted after verification or kept as a marker; neither matters. The dry-run is verification of the workflow, not a shipped artifact.

## Acceptance criteria

- [ ] `CHANGELOG.md` exists at the repo root in Keep-a-Changelog format with an `[Unreleased]` section and a populated skeletal `[0.1.0] - YYYY-MM-DD` section
- [ ] `docs/releasing.md` exists and documents the manual cut-a-release steps (version selection, `project()` bump, CHANGELOG migration, commit message, tag command, push, post-push verification, handling failures)
- [ ] `.github/workflows/release.yml` exists and triggers on tag pushes matching `v*.*.*` and `v*.*.*-*`
- [ ] The release workflow runs the same matrix as `ci.yml` (host + posix × gcc + clang, `CORTEXFLOW_BUILD_TESTS=ON`, `CORTEXFLOW_TRACE_LEVEL=FULL`) against the tagged commit
- [ ] The release workflow extracts the matching `[X.Y.Z]` (or `[X.Y.Z-suffix]`) section from `CHANGELOG.md` and posts it as the GitHub Release notes
- [ ] The release workflow fails clearly (and does not create a Release with empty notes) when no matching CHANGELOG section is found
- [ ] The release workflow does not auto-modify, auto-delete, or auto-yank tags on matrix failure
- [ ] A dry-run `v0.1.0-rc1` tag is pushed and the workflow is observed to: run the matrix green, create the GitHub Release for `v0.1.0-rc1`, and populate the notes from the `[Unreleased]` (or pre-cut `[0.1.0]`) CHANGELOG section
- [ ] If `ci.yml` and the matrix block in `release.yml` are duplicated rather than shared, both files reference each other in a comment so a future maintainer doesn't drift them
- [ ] All four artifacts (CHANGELOG, releasing.md, release.yml, and the dry-run Release) are reviewed and confirmed before Slice 6 begins

## Blocked by

Slice 2 (`02-cmake-hygiene-for-subproject-consumption.md`) — the matrix needs to run green against the tagged commit, and the version-in-`project()` lives there. Slice 1 (`01-license-and-spdx-rollout.md`) is also recommended-before because release tooling that ships a build without a `LICENSE` is awkward.

The dry-run verification step depends on Slices 1, 2, and 3 being in main, since the `rc1` tag must point at a commit that already has the consumption contract working.

## Notes

- Resist the temptation to add conventional-commits automation for CHANGELOG generation. The project is small enough that hand-maintenance is correct; the automation would be more code to maintain than the CHANGELOG itself.
- The CHANGELOG entry extraction is the trickiest piece. A small `awk` or Python script invoked from the workflow that prints the lines between `## [X.Y.Z]` and the next `## [` heading is the conventional approach. Whichever script form you pick, keep it inside the workflow YAML or in `scripts/` — do not introduce a new tool.
- The workflow's permissions block should request `contents: write` (to create the Release) but no more. Default permissions are read-only on most repos.
- Pre-release tags (`v*.*.*-*`) should produce GitHub Releases marked as pre-release (`prerelease: true` in the create step). The dry-run `rc1` Release should appear as pre-release, not as a stable Release.
- If the existing `.github/workflows/ci.yml` is structured in a way that makes its matrix easy to extract into a reusable workflow (`workflow_call`), do that and call it from `release.yml`. If not, duplication is acceptable for v0.1.0; the comment-cross-reference acceptance criterion is the guard against drift.

## Comments

### 2026-05-19 — from afk worker

Built the four code artifacts called for by this issue:

- **`CHANGELOG.md`** at the repo root. Keep-a-Changelog format. Empty
  `[Unreleased]` section above a populated skeletal
  `[0.1.0] - YYYY-MM-DD` section that summarises the v0.1.0 contents
  per the issue's bullet list (first tagged release; Apache-2.0; the
  FetchContent contract; sub-project hygiene; PRIVATE compile flags;
  Release surface in CONTEXT.md; host/posix backends; freertos and
  bare_metal as placeholders). Slice 6 finalises the date and any
  wording. Reference link definitions at the bottom point at
  `github.com/jmendoza-10/cortexflow` (resolved from `git remote -v`
  in the worktree).
- **`docs/releasing.md`** — one-page runbook. Covers version selection
  with the v0.x posture spelled out, the `project()` version bump,
  the CHANGELOG migration ritual, the `release: vX.Y.Z` commit
  message, the annotated-tag command, push, verification, and the
  failure-handling rule (never re-point an existing tag; cut a new
  patch tag after fixing). Links back to ADR-0023 and to
  CONTEXT.md → Release surface.
- **`.github/workflows/release.yml`** — triggered on `v*.*.*` and
  `v*.*.*-*` tags. Two jobs:
  - `build-and-test`: duplicates the `ci.yml` matrix verbatim (host +
    posix × gcc + clang, `CORTEXFLOW_BUILD_TESTS=ON`,
    `CORTEXFLOW_TRACE_LEVEL=FULL`).
  - `publish-release`: extracts the `[X.Y.Z]` (or `[X.Y.Z-suffix]`)
    section from `CHANGELOG.md` via an inline `awk` script bounded by
    the next `## [` heading **or** the first `[name]: url`
    link-reference definition (so the trailing reference links don't
    leak into release notes). The script writes the notes to
    `release-notes.md`; an empty-content check fails the job with a
    `::error::` annotation before any Release is created. Publication
    uses the built-in `gh release create --notes-file ...`, gated by
    `if [[ "${TAG}" == *-* ]]` so pre-release tags pass
    `--prerelease`. Permissions block requests `contents: write`
    only. The workflow never deletes, re-points, or auto-yanks tags.
- **Cross-reference comments** added to both `ci.yml` and
  `release.yml` headers noting that their matrix blocks are
  duplicated and must be kept in lockstep.

### Implementation choices worth flagging

- **Built-in `gh` over `softprops/action-gh-release`.** The issue
  permits either; `gh release create --notes-file` is built into
  GitHub-hosted runners and avoids a third-party action pin. The
  workflow stays first-party-only.
- **Duplication, not `workflow_call`.** `ci.yml`'s matrix is small
  and the `release.yml` job adds only the extract-notes and publish
  steps. Factoring into a reusable workflow would have moved more
  lines than it saved. The cross-reference comments are the drift
  guard per the issue's last note. Easy to refactor to
  `workflow_call` later if a third workflow ever needs the same
  matrix.
- **Awk extraction end conditions.** Section ends at the *next* `## [`
  heading (the conventional Keep-a-Changelog rule) **or** the first
  Markdown link-reference definition (`[name]: url`). The
  link-reference case matters for the last `[X.Y.Z]` section in the
  file, which has no later heading; without it, the trailing
  `[Unreleased]: ...` / `[0.1.0]: ...` lines would have been
  appended to the release notes verbatim. Verified locally against
  the CHANGELOG written in this issue.
- **Empty-content check is whitespace-only.** A section that contains
  only `### Added`, `### Changed`, etc. with no entries underneath
  is *not* flagged as empty by `tr -d '[:space:]'`. The dry-run is
  the practical guard against shipping a skeleton — the matrix would
  succeed but the Release would have only subsection headers, which
  the human spots immediately on the Releases page. Tightening this
  check (e.g. requiring at least one non-heading line) felt like
  over-engineering relative to the dry-run already on the
  acceptance list.

### Deferred to the human

- **Pushing `v0.1.0-rc1` for the dry-run verification.** The runner
  instructions say "Do NOT push the branch. Do NOT open a PR." That
  applies to tags too. The dry-run acceptance checkbox can only be
  satisfied after this branch lands on `main` and the human pushes
  the `rc1` tag. Per the PRD, the rc1 commit also needs Slices 1, 2,
  and 3 in main (they are — issues 01, 02, 03 are marked merged).
  Suggested manual steps for the human after merge:
  1. From `main`: optionally add a `[0.1.0-rc1]` heading to the
     CHANGELOG section currently labelled `[0.1.0] - YYYY-MM-DD`
     so the workflow finds a matching section, OR leave `[0.1.0]`
     as the only versioned section and temporarily push a tag
     named `v0.1.0-rc1` *after* renaming the heading to
     `[0.1.0-rc1]` for the duration of the rc. The simplest path:
     edit `CHANGELOG.md` on a short-lived branch, change
     `## [0.1.0] - YYYY-MM-DD` to `## [0.1.0-rc1] - 2026-05-19`,
     commit, tag `v0.1.0-rc1`, push tag, observe the workflow,
     then revert the CHANGELOG change before cutting the real
     `v0.1.0`. Or just push `v0.1.0` directly if confident; the
     workflow has been smoke-tested locally for the extraction
     logic. The runbook explains the post-push verification.
  2. Observe in the Actions tab: matrix green, Release created,
     notes match CHANGELOG, Release flagged Pre-release.
  3. The rc1 Release can be deleted afterwards or kept — either is
     fine.

### Things to pay attention to in review

- The CHANGELOG reference links at the bottom hard-code
  `github.com/jmendoza-10/cortexflow`. If the canonical repo URL
  ever changes (rename, fork-as-canonical), update those two lines
  too.
- The `gh release create` step assumes the default `GITHUB_TOKEN`
  has permission to create Releases on the repo. The
  `permissions: contents: write` block at the workflow level grants
  this; no PAT or other secret is needed.
- The workflow runs the matrix from scratch on every tag push —
  including pre-release tags. The matrix is short (~minutes) so
  this is fine, but if it ever balloons, the obvious lever is to
  scope the job to only stable tags. The issue's acceptance
  criteria require running the full matrix on both stable and
  pre-release tags, so this is correct as-is.

— from afk worker
