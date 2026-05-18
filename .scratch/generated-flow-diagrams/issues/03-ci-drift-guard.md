# CI drift guard for `docs/diagrams/`

Status: ready-for-merge
PRD: `.scratch/generated-flow-diagrams/PRD.md` — user stories 15, 16
ADR: `docs/adr/0021-generated-diagrams-from-cpp-source.md`

## Parent

`.scratch/generated-flow-diagrams/PRD.md`

## What to build

A CI step that regenerates the diagrams from source and fails the build if the result differs from the committed files under `docs/diagrams/`. After this slice, a PR that modifies a Flow or a cross-Module `send<>` but does not regenerate the matching diagrams cannot merge — the contradiction surfaces as a red check on the PR rather than as a silent lie in the repository.

The mechanism is deliberately boring. The CI job runs the generator against `examples/minimal_app/app.hpp` and then asserts that `git diff --exit-code docs/diagrams/` is clean. A non-zero exit fails the build with a clear instruction telling the contributor to run `python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp` locally and commit the result.

This slice can run in parallel with issue 02. The drift guard uses `git diff --exit-code docs/diagrams/`, which checks whatever files happen to live under that path — it does not care whether the directory contains only flow files (after issue 01 lands) or both flows and a module graph (after issue 02 lands). The same CI step works for either state.

The slice does not introduce a new GitHub Actions workflow or CI provider. It extends whichever workflow the repository already uses for C++ builds and tests with one additional step that runs after checkout and before (or in parallel with) the C++ build. The Python toolchain needed is whatever is already available on the CI runner; if no Python is installed, the workflow file installs the version `gen-diagrams.py` requires (Python 3 standard library only, per issue 01).

A `make` or `npm`-style convenience target is out of scope; the script is invoked directly. The CI step is reproducible locally by running the same two commands by hand.

## Acceptance criteria

- [ ] The repository's existing CI workflow includes a step that runs `python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp` followed by `git diff --exit-code docs/diagrams/`
- [ ] On a clean checkout where the committed `docs/diagrams/` matches what the generator produces, the step exits zero and the build proceeds
- [ ] On a checkout where any file under `docs/diagrams/` is stale relative to the source, the step exits non-zero and fails the build
- [ ] The failure output makes it obvious to a contributor that the fix is to run the generator locally and commit the result (e.g. an `echo` line before the `git diff` printing the regenerate-and-commit instruction)
- [ ] The step works whether `docs/diagrams/` contains only flow files (issue 02 not yet merged) or both flow files and a module graph (issue 02 merged)
- [ ] The Python toolchain required by the step is whatever `gen-diagrams.py` needs — Python 3 standard library only, no libclang, no extra `pip install`
- [ ] A local reproduction of the CI check is documented somewhere a contributor will find it (a one-line note in `docs/diagrams/README.md`, the PRD, or wherever feels natural for the repo's docs culture)
- [ ] The full CI suite (existing C++ build + test matrix from issue 17, plus the new drift guard) is green on the merge commit

## Blocked by

`.scratch/generated-flow-diagrams/issues/01-flow-diagrams-end-to-end.md` (can run in parallel with `02-module-graph-end-to-end.md`)

## Comments

What I built:

- Added a `diagrams-drift-guard` job to `.github/workflows/ci.yml` that runs
  in parallel with `build-and-test`. It runs `python3
  scripts/gen-diagrams.py examples/minimal_app/app.hpp` then `git diff
  --exit-code docs/diagrams/`. On non-zero diff it prints a GitHub-annotated
  error plus the exact two commands a contributor should run locally, then
  exits 1.
- Added `docs/diagrams/README.md` documenting the local reproduction (the
  same two commands) and pointing at ADR-0021.

Verified locally:

- Clean state on this worktree: generator runs, `git diff --exit-code
  docs/diagrams/` returns 0. Drift guard passes.
- Stale state (simulated by staging a `STALE` line on
  `consumer.flow.mmd`, then re-running the generator so the working tree
  matches source but the index does not): the same shell block from the CI
  step exits 1 and prints the regenerate-and-commit instructions. Matches
  the CI semantics where `actions/checkout@v4` populates the index from
  HEAD and any drift between HEAD's diagrams and the freshly generated
  ones shows up as a non-empty `git diff`.

Choices worth flagging:

- Made the drift guard a separate job rather than a step inside
  `build-and-test`. The existing matrix runs 4 combinations (gcc/clang x
  host/posix); putting the check inside would run it 4x. A separate
  ubuntu-latest job runs it once, in parallel with the C++ matrix, which
  matches "before (or in parallel with) the C++ build" from the issue.
- Did not add `actions/setup-python`. `ubuntu-latest` ships with Python 3
  pre-installed and the generator is stdlib-only (per issue 01), so the
  toolchain note in the issue ("whatever is already available on the CI
  runner") is satisfied without an extra step.
- Did not modify `gen-diagrams.py` or anything under `scripts/diagrams/`
  — that is issue 01/02 territory and out of scope here.

Skipped / deferred: nothing. All acceptance-criteria checkboxes are
covered by this diff. The last checkbox ("full CI suite green on the
merge commit") will be confirmed by the human merging — I cannot
observe the merge-commit CI run from inside this worktree.

— 2026-05-18, from afk worker
