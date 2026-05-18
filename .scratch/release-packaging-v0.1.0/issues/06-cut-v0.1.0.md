# Cut v0.1.0: finalize CHANGELOG, tag, verify the release workflow

Status: ready-for-agent
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
