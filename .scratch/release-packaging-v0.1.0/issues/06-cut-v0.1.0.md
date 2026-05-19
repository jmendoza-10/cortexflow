# Cut v0.1.0: finalize CHANGELOG, tag, verify the release workflow

Status: merged
PRD: `.scratch/release-packaging-v0.1.0/PRD.md` — user stories 2, 13, 24, 26, 27
ADR: `docs/adr/0023-release-packaging-strategy.md`

## Parent

`.scratch/release-packaging-v0.1.0/PRD.md`

## What to build

This is the final slice — the actual `v0.1.0` cut. The framework now has license, SPDX headers, sub-project-clean CMake, a consumption smoke test, a release runbook, a tag-triggered workflow, and consumer-facing README docs. Slice 6 ships it.

Follow the runbook from `docs/releasing.md` (Slice 4) end-to-end. Concretely:

1. **Confirm prerequisites are in main.** All of Slices 1–5 are merged and the CI matrix is green on main HEAD. The dry-run `v0.1.0-rc1` workflow ran successfully and the resulting GitHub Release looks correct (notes match the CHANGELOG, all matrix jobs were green).

2. **Migrate the CHANGELOG.** In `CHANGELOG.md`, rename `[Unreleased]` to `[0.1.0] - YYYY-MM-DD` (the date is the date of the tag push). Add a fresh, empty `[Unreleased]` section above it. Review the `[0.1.0]` content for completeness — it must accurately describe everything user-visible that shipped in this release (license declaration, FetchContent contract, PRIVATE flag posture, `CORTEXFLOW_BUILD_EXAMPLES`, the `CORTEXFLOW_TARGET` selection mechanism, the Release surface vocabulary, the two new ADRs). If anything is missing or inaccurate, fix the wording before tagging.

3. **Confirm the version in `project()`.** The top-level `CMakeLists.txt` should already read `project(cortexflow VERSION 0.1.0 LANGUAGES CXX)` from Slice 2. Re-verify; no bump is required since this is the first tag at that version.

4. **Make the release commit.** Single commit with message `release: v0.1.0` (or whatever convention the runbook documents). The commit contains only the CHANGELOG migration — no other source changes.

5. **Tag.** `git tag -a v0.1.0 -m "v0.1.0"` (or a longer message — at minimum the tag is annotated, not lightweight, so it carries committer metadata and is signable). Do not point an existing tag at a new commit, and do not delete the `v0.1.0-rc1` tag — leave the pre-release marker in place for traceability.

6. **Push the tag.** `git push origin v0.1.0`. Do not push to a branch in the same command — keep the tag push isolated so the workflow trigger is unambiguous.

7. **Observe the workflow.** Watch the Actions tab. The `release.yml` workflow must:
   - Trigger on the `v0.1.0` tag push.
   - Run the full build/test matrix against the tagged commit (host + posix × gcc + clang, with tests, with `CORTEXFLOW_TRACE_LEVEL=FULL`).
   - All matrix jobs pass.
   - The workflow creates a GitHub Release for `v0.1.0` (not a pre-release this time — stable Release) with notes extracted from the `[0.1.0]` CHANGELOG section.

8. **Verify the published Release.** Confirm on the Releases page that:
   - `v0.1.0` is listed as the latest stable Release (not a pre-release).
   - The Release notes match the `[0.1.0]` section of `CHANGELOG.md`.
   - The auto-attached source tarball is present.
   - The tag points at the exact commit that contains the CHANGELOG migration.

9. **Smoke-test from a fresh checkout** (optional but recommended). In a scratch directory, write the FetchContent stanza from the README's "Consuming CortexFlow" section against `GIT_TAG v0.1.0`, configure it, and confirm it builds. This is the truest end-to-end verification: a fresh consumer following the documented contract gets a working library.

## Acceptance criteria

- [ ] All of Slices 1–5 are merged to main and the CI matrix is green on main HEAD before this slice begins
- [ ] The `v0.1.0-rc1` dry-run Release was verified and its GitHub Release contents match the CHANGELOG
- [ ] `CHANGELOG.md` is migrated: `[Unreleased]` renamed to `[0.1.0] - YYYY-MM-DD` (with the real cut date), a fresh empty `[Unreleased]` section is added above, and the `[0.1.0]` content is reviewed for completeness
- [ ] The top-level `project()` declaration reads `VERSION 0.1.0` (no change expected; verify only)
- [ ] A single release commit is made on main containing only the CHANGELOG migration, with a release-style commit message
- [ ] An annotated tag `v0.1.0` is created at the release commit and pushed to origin
- [ ] The `release.yml` workflow triggers on the tag, runs the full build/test matrix against the tagged commit, and all matrix jobs pass
- [ ] A stable (not pre-release) GitHub Release `v0.1.0` is created with notes matching the `[0.1.0]` CHANGELOG section and the auto-attached source tarball
- [ ] The `v0.1.0-rc1` pre-release tag and Release are left in place (not deleted)
- [ ] A smoke test from a fresh checkout — applying the README's FetchContent stanza with `GIT_TAG v0.1.0` — produces a working consumer build

## Blocked by

All of Slices 1–5. This is the integration point; everything must be in place before this slice can run.

## Notes

- If the tag push triggers a workflow that fails on the matrix, **do not** force-update the tag. Cut a `v0.1.1` patch tag after fixing the underlying issue. Tags are immutable in spirit and re-pointing them confuses consumers who have already pinned to the original tag (and FetchContent caches the SHA, not the tag, so re-pointing only affects fresh checkouts — making the divergence silent).
- The `v0.1.0-rc1` Release stays public as a historical artifact. Consumers should not pin to `-rc1` for production use, but it shouldn't be deleted — deletions in GitHub Releases are confusing and the rc1 marker is useful provenance for "when did we first run the release workflow end-to-end."
- If anything in the published `v0.1.0` Release looks subtly wrong (notes formatting, tarball naming, asset attachments), file a small follow-up issue rather than deleting and re-tagging. Document the discrepancy and address it in the next release's runbook iteration.
- After cutting, update CLAUDE.md or any other onboarding doc that lists "current release status" if one exists. (None currently does — but this is the moment when one might be created if useful.)
- This issue ends with a released `v0.1.0` artifact reachable via `FetchContent_Declare(... GIT_TAG v0.1.0)` from any external CMake project that has network access to the repo. That is the success criterion of the entire PRD.

## Comments

### 2026-05-19 — from afk worker

**What I did** (the local-prep half of the release cut):

- Migrated `CHANGELOG.md`: renamed `[Unreleased]` → `[0.1.0] - 2026-05-19`, added a fresh empty `[Unreleased]` above. The bottom-of-file comparison links (`[Unreleased]` and `[0.1.0]`) were already set up correctly in Slice 4, so no edits there.
- Expanded the `[0.1.0]` notes to call out three things the original draft did not mention explicitly even though the issue's content checklist required them: the `CORTEXFLOW_TARGET` cache-variable selection mechanism (its own Added bullet), the README "Consuming CortexFlow" section that Slice 5 added (its own Added bullet), and the two new ADRs (0022 and 0023) listed together as an Added bullet. The Changed section still references ADR-0022 inline as before.
- Re-verified `CMakeLists.txt:2` already reads `project(cortexflow VERSION 0.1.0 LANGUAGES CXX)`. No change needed; locally `cmake -B build` populates `CMAKE_PROJECT_VERSION:STATIC=0.1.0` in the cache.
- Locally dry-ran the workflow's `awk` extractor (the exact snippet in `.github/workflows/release.yml`) with `marker="## [0.1.0]"` and confirmed it produces clean notes — no stray content from the new empty `[Unreleased]` block bleeds in, and the section terminates correctly on the `[Unreleased]: ...` link reference at the bottom.
- Configured + built + ran the full local test suite at `CORTEXFLOW_BUILD_TESTS=ON` against the release commit: 22/22 tests passing. (Just a sanity check that the CHANGELOG-only commit doesn't break anything — the CHANGELOG isn't compiled, but it confirms `VERSION 0.1.0` still configures.)
- Made a single commit `release: v0.1.0` (`a0d3e24`) containing only the CHANGELOG migration. The commit body references this issue path per the worker convention.

**What I deferred — needs human action**, because the AFK worker is explicitly forbidden from pushing or opening PRs (see worker instructions). Picking up from here, after merging `agent/06-cut-v0.1.0` into `main`:

1. **Pre-merge sanity check.** Confirm CI is green on `main` HEAD before merging this branch. The issue's first acceptance criterion is "all of Slices 1–5 are merged to main and the CI matrix is green on main HEAD." I can see Slices 1–5's commits in `git log` (license, CMake hygiene, FetchContent smoke test, release process, README consumer docs) but cannot observe CI status on origin/main from inside the worktree.
2. **Tag on the merge commit, not the branch SHA.** After merging this branch into main, run `git tag -a v0.1.0 -m "CortexFlow v0.1.0"` (or `-m "v0.1.0"` — runbook only requires annotated, not lightweight) on the resulting merge commit on main. **Do not tag `a0d3e24` directly** — that SHA is on the agent branch; the tag must point at whichever commit on main contains the migrated `CHANGELOG.md` in its tree (typically the merge commit, or the migration commit itself if the merge was fast-forward).
3. **Push the tag in isolation.** `git push origin v0.1.0`. Runbook step 6: do not combine with a branch push.
4. **Observe `release.yml`.** Build/test matrix (host + posix × gcc + clang, with tests, with `CORTEXFLOW_TRACE_LEVEL=FULL`) runs against the tagged commit, then the `publish-release` job extracts notes from the `[0.1.0]` CHANGELOG section and creates a stable (non-pre-release) GitHub Release.
5. **Verify the published Release** matches the criteria in the issue (latest stable, notes match, source tarball present, tag points at the migration commit).
6. **Smoke-test from a fresh checkout.** Recommended (optional acceptance criterion): copy the README's FetchContent stanza into a scratch consumer with `GIT_TAG v0.1.0`, configure, build.

**Things the reviewer should pay attention to:**

- **Date in the CHANGELOG header.** I dated `[0.1.0] - 2026-05-19` to match today (the date of the preparatory commit). The runbook says the date should be "today's date in ISO 8601" at finalization time and the issue says "the date is the date of the tag push." If the human pushes the tag on a different day, **update the date in `CHANGELOG.md` to match the actual tag-push date before pushing the tag** — otherwise the GitHub Release notes (extracted by the workflow) will carry the wrong date, and re-pointing the tag to fix it is forbidden by the runbook's "do not re-tag" rule. The simplest path: tag on the same day as merging, OR amend the date in a follow-up commit on main before tagging.
- **No `v0.1.0-rc1` tag exists yet.** `git tag --list` (run inside this worktree) is empty. The issue's prerequisite ("The dry-run `v0.1.0-rc1` workflow ran successfully") and acceptance criterion ("the `v0.1.0-rc1` pre-release tag and Release are left in place") both presuppose an rc1 dry-run that the runbook documents as recommended. I left no rc1 tag because (a) creating it would require pushing — forbidden — and (b) it is out of scope for this slice's deliverable (the CHANGELOG migration), but **the human should consider whether to run an rc1 dry-run first**. The runbook supports it (`docs/releasing.md:84-86`); doing so verifies the workflow end-to-end before committing the immutable `v0.1.0` tag. If skipping rc1, accept the risk that the first real workflow run is on the stable tag.
- **CHANGELOG additions vs. the original draft.** Slice 4's draft already covered most of the content checklist. The three additions I made (CORTEXFLOW_TARGET bullet, README bullet, explicit ADR list) were judgment calls based on the issue's content checklist. None of them mention behavior that wasn't already shipped; they just surface it for consumers reading the Release notes verbatim. Reviewer should confirm the wording is acceptable — the workflow extracts these notes literally.
- **Acceptance criteria I can't tick from inside an AFK worker.** Criteria covering "all of Slices 1–5 are green on main HEAD," the rc1 dry-run verification, the tag push, the workflow run, the published Release verification, and the fresh-checkout smoke test are all human-side. The local-prep criteria (CHANGELOG migrated, `VERSION 0.1.0` verified, single release commit made) are done.
- **No source changes.** The release commit is CHANGELOG-only as the runbook prescribes. No `CMakeLists.txt` bump was needed; the version was already `0.1.0` from Slice 2.
- **No edits to CLAUDE.md or onboarding docs.** The issue's Notes section flagged this as optional ("After cutting, update CLAUDE.md or any other onboarding doc that lists 'current release status' if one exists. (None currently does — but this is the moment when one might be created if useful.)"). I did not create such a doc — that's a judgment call better made by the human after the actual cut, not from inside an AFK worker pre-cut.
