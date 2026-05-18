# Generated diagrams

The `.mmd` files under `flows/` and `modules/` are generated from C++
source by `scripts/gen-diagrams.py` and committed so they show up in PR
review (see [ADR-0021](../adr/0021-generated-diagrams-from-cpp-source.md)).
Do not edit them by hand.

## Regenerating locally

Run the same two commands the CI drift guard runs:

```sh
python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp
git diff --exit-code docs/diagrams/
```

If the second command exits non-zero, the diagrams are stale relative
to the source — `git add docs/diagrams/ && git commit` the regenerated
files alongside your source change.

Python 3 standard library only; no `pip install`, no libclang.
