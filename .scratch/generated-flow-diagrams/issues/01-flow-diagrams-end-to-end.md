# Flow diagrams end-to-end for an application

Status: ready-for-merge
PRD: `.scratch/generated-flow-diagrams/PRD.md` — user stories 1–5, 11, 12, 14, 17, 19, 20, 22, 23 (flows), 25, 26, 27, 28, 29
ADR: `docs/adr/0021-generated-diagrams-from-cpp-source.md`

## Parent

`.scratch/generated-flow-diagrams/PRD.md`

## What to build

A working end-to-end pipeline that turns an application's `app.hpp` into committed Mermaid **Flow diagrams** — one per Flow-owning Module in the application's `ModuleList<...>`. After this slice, a maintainer can run

```
python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp
```

and see `docs/diagrams/flows/<module>.flow.mmd` written for every Flow-owning Module, with each diagram rendering inline on GitHub PR preview. The Module graph and the CI drift guard are not in scope here — both are separate slices that build on this one.

The slice lands the foundational scaffolding the later slices reuse: the `scripts/diagrams/` Python package, the brace-scope tracker (the only structural-parsing component), the parts of the composition parser needed to read `ModuleList<...>`, the Flow extractor, the FlowIR portion of the IR schema, the `render_flow` entry point of the Mermaid renderer, the CLI driver wired to emit flow diagrams only, and pytest configuration with the three unit-test suites + one end-to-end test mandated by the PRD.

The Flow diagrams render at the "Mid" detail level defined in ADR-0021: State nodes labeled with the State name from `StateList<...>`, edges labeled with the Message type from the matching `payload_type_id() == type_id<X>()` check, `Transition` and `TransitionNow` rendered as visually distinct arrow styles, `done()` rendered as a terminal sink, and the initial State (the first in `StateList<...>` or an explicit `InitialStateTag`) visually marked.

The extractor uses plain Python — no libclang, no `compile_commands.json`. The PRD-specified stylized handler pattern is the contract; if a Flow body deviates (macros hiding directives, dynamically computed transitions), the extractor may miss edges silently. That is accepted for this slice; the CI guard in slice 03 closes the drift loop, and a follow-up can tighten the extractor's diagnostics if real drift surfaces.

`docs/diagrams/flows/<module>.flow.mmd` files are committed to the repository for every Flow-owning Module in `examples/minimal_app/`. These committed files are also the snapshot the end-to-end test asserts against — the same byte-equality check the CI guard will perform once slice 03 lands.

The script refuses any input that is not a file containing a `Runtime<ModuleList<...>, ...>` declaration. A directory argument, a non-existent path, or a file with no Runtime composition all fail with a clear error message rather than producing a wrong-shaped graph.

## Acceptance criteria

- [ ] `scripts/gen-diagrams.py` is the CLI entry point and accepts an application's `app.hpp` path as its required positional argument
- [ ] The script refuses to run on a directory, a non-existent file, or a file with no `Runtime<...>` declaration, exiting non-zero with a clear error message
- [ ] `scripts/diagrams/` is a Python package containing the brace-scope tracker, the composition parser (with `ModuleList<...>` support), the Flow extractor, the IR schema (FlowIR), and the Mermaid renderer (`render_flow`)
- [ ] The brace-scope tracker correctly returns function bodies keyed by fully-qualified function name, handling nested braces, string/char literals containing `{` or `}`, single-line and block comments containing braces, and functions nested inside namespaces
- [ ] The Flow extractor produces a FlowIR that contains the State list from `StateList<...>`, the initial State (defaulting to `StateList::head`, overridable by explicit `InitialStateTag` template argument), and one edge per `transition_to<X>()`, `transition_to_now<X>()`, or `done()` directive in each State's `static handle()` body
- [ ] Each edge in the FlowIR carries the Message type from the matching `payload_type_id() == cortexflow::type_id<X>()` check (or the equivalent `type_id<X>()` call without the `cortexflow::` qualifier) that immediately precedes the directive
- [ ] Each edge in the FlowIR is tagged with its directive kind (`Transition`, `TransitionNow`, or `Done`)
- [ ] The Mermaid renderer's `render_flow(flow_ir)` produces Mermaid text that visually distinguishes `Transition`, `TransitionNow`, and `Done`, marks the initial State, and labels each edge with the Message type
- [ ] Running `python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp` writes `docs/diagrams/flows/<module>.flow.mmd` for every Module in `minimal_app`'s `ModuleList<...>` that owns a `Flow`, and writes no other files
- [ ] `docs/diagrams/flows/consumer.flow.mmd` (and any other Flow-owning Module's file) is committed to the repository and renders correctly when previewed as Mermaid (initial State marked, `Idle → Processing` on `KeyChanged<Counter>`, `Processing → Idle` on `Consumer::ProcessingTick`, `Transition` arrow style)
- [ ] `pytest` is configured at the repo root (or under `scripts/`) so `python3 -m pytest scripts/` runs the test suite
- [ ] Unit tests for the brace-scope tracker cover: nested braces, string literals with braces, comments with braces, namespace-nested functions, and at least one negative case (malformed input handled gracefully)
- [ ] Unit tests for the Flow extractor cover: a two-State Flow with one transition each direction (mirrors `minimal_app::Consumer`), a Flow using `transition_to_now<X>()`, a Flow ending with `done()`, a Flow with an explicit `InitialStateTag`, and a Flow whose `handle()` body has multiple `if` branches
- [ ] Snapshot tests for the Mermaid renderer assert byte-equality against committed fixture outputs for at least one fixture FlowIR
- [ ] One end-to-end test invokes the CLI against `examples/minimal_app/app.hpp` (writing to a tempdir) and asserts byte-equality between the produced files and the committed files under `docs/diagrams/flows/`
- [ ] All tests pass when run via `python3 -m pytest`
- [ ] The script depends only on the Python 3 standard library — no libclang, no `compile_commands.json`, no `clang` system dependency
- [ ] The terms `Flow diagram` (per-Module artifact) and `Flow` (the framework primitive) are used in script output, error messages, and any new docs; `flowchart` does not appear anywhere in the slice's diff

## Blocked by

None — can start immediately.
