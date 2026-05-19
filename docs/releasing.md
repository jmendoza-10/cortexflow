# Releasing CortexFlow

This is the maintainer-facing runbook for cutting a tagged release. The
contract this runbook implements is defined in
[ADR-0023](adr/0023-release-packaging-strategy.md); read that ADR for
the rationale behind these steps.

## 1. Choose the version number

Tags are `vMAJOR.MINOR.PATCH` (semver). While the project is in
**v0.x posture** (which lasts until the architectural-spine ADRs
001–019 are written — see ADR-0023):

- **Patch bump** (`v0.1.0 → v0.1.1`): bugfix-only. No changes to the
  release surface (Core API headers, per-target `platform.hpp`, the
  `cortexflow` CMake target). Consumers can take patch upgrades
  automatically.
- **Minor bump** (`v0.1.0 → v0.2.0`): may break consumers. Used for
  any change to the release surface, however small. Consumers should
  read the CHANGELOG before bumping.
- **Major bump** (`v0.x → v1.0.0`): graduation. Only happens once all
  reserved-spine ADRs are written and Accepted.

After v1.0.0, standard semver applies: patch is bugfix, minor is
backwards-compatible additions, major is breaking.

## 2. Bump the version in `CMakeLists.txt`

The single source of truth for the version is the top-level `project()`
call:

```cmake
project(cortexflow VERSION X.Y.Z LANGUAGES CXX)
```

Edit `X.Y.Z` to the chosen version. Consumers read this as
`${cortexflow_VERSION}` after `FetchContent_MakeAvailable`.

## 3. Finalize `CHANGELOG.md`

`CHANGELOG.md` is in [Keep a Changelog](https://keepachangelog.com/)
format. Between releases, entries accumulate under `[Unreleased]`. At
release time:

1. Rename `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD` (today's date in
   ISO 8601).
2. Add a fresh empty `[Unreleased]` section above it, with the
   standard subsections (`Added`, `Changed`, `Deprecated`, `Removed`,
   `Fixed`, `Security`).
3. Update the comparison links at the bottom of the file: the
   `[Unreleased]` link should now compare against `vX.Y.Z`, and add a
   new `[X.Y.Z]` link pointing at the release tag.
4. Skim the section. If a subsection is empty, delete it. If an entry
   is unclear, edit for clarity — consumers read this verbatim from
   the GitHub Release page.

## 4. Commit

Commit the `CMakeLists.txt` and `CHANGELOG.md` changes together:

```
git add CMakeLists.txt CHANGELOG.md
git commit -m "release: vX.Y.Z"
```

The commit message convention is `release: vX.Y.Z`. One commit per
release. Keep the body empty unless there is something a future
archaeologist needs to know that isn't already in the CHANGELOG.

## 5. Tag

Create an annotated tag against the release commit:

```
git tag -a vX.Y.Z -m "CortexFlow vX.Y.Z"
```

Annotated tags (`-a`) carry the tagger identity, date, and message;
lightweight tags do not. Always annotated.

For a pre-release dry-run (e.g. before cutting the first real
`v0.1.0`), use a suffix:

```
git tag -a vX.Y.Z-rc1 -m "CortexFlow vX.Y.Z release candidate 1"
```

The release workflow recognises both `vX.Y.Z` and `vX.Y.Z-suffix`
tags. Pre-release tags produce GitHub Releases marked as pre-release.

## 6. Push

Push the release commit and the tag in one go:

```
git push origin main
git push --tags
```

(Or `git push --follow-tags` if your local config has the tag tracking
the branch.)

## 7. Verify

After pushing, watch the `Release` workflow in the GitHub Actions tab:

- All matrix jobs (host + posix × gcc + clang) run green against the
  tagged commit.
- A GitHub Release for `vX.Y.Z` appears under the repo's
  *Releases* page.
- The Release notes match the `[X.Y.Z]` section of `CHANGELOG.md`
  verbatim.
- For a pre-release tag, the Release is flagged as *Pre-release* in
  the UI.

If any of those checks fail, see the next section.

## 8. If something goes wrong

**Do not** re-point or delete the existing tag. Tags are immutable in
the eyes of consumers who may have already pinned them.

- **Matrix failure on the tag.** The workflow surfaces the failure
  visibly in the Actions tab and does **not** create a GitHub Release.
  Fix the underlying issue on `main`, then cut a new patch tag
  (`vX.Y.(Z+1)` for a stable release, `vX.Y.Z-rc(N+1)` for a fresh
  release candidate). Never re-tag the same version number.
- **CHANGELOG section missing or empty.** The workflow fails with a
  clear error message and does not create a Release with empty notes.
  Add or correct the section in `CHANGELOG.md` on `main`, then cut a
  new tag as above.
- **Wrong content shipped.** Same rule: cut a new patch tag with the
  fix. Optionally edit the GitHub Release description by hand to add
  a "superseded by vX.Y.(Z+1)" note, but never delete the tag.

The release workflow does not auto-yank, auto-delete, or auto-modify
tags under any failure condition. The human is always in the loop.

## Reference

- [ADR-0023: Release-packaging strategy for v0.1.0+](adr/0023-release-packaging-strategy.md) —
  the formal contract this runbook implements.
- [CONTEXT.md → Release surface](../CONTEXT.md#release-surface) —
  the definition of what changes are breaking vs. non-breaking.
- [Keep a Changelog](https://keepachangelog.com/) — the format
  `CHANGELOG.md` follows.
