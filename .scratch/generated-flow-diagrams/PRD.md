# Generated Flow diagrams and Module graphs from C++ source

Status: ready-for-agent
ADR: [`docs/adr/0021-generated-diagrams-from-cpp-source.md`](../../docs/adr/0021-generated-diagrams-from-cpp-source.md)

## Problem Statement

CortexFlow's behavior is encoded entirely in C++ type-level constructs. A Module's `StateList<...>` declares its states. `transition_to<X>()` / `transition_to_now<X>()` / `done()` / `stay()` directives inside each State's `static handle()` body declare its edges. `send<Other>(Other::Msg{})` call sites declare cross-Module communication. `Owned<K, M>` entries inside `CacheKeyList` declare which Module writes which Cache key. There is no separate behavioral spec — the code *is* the spec.

A maintainer trying to understand "what happens when Message X arrives at Module Y" has to assemble the picture from at least three files: the Module's header (for the `Flow` declaration and `Inbox`), one or more `.cpp` files (for `handle()` bodies and cross-Module `send<>` calls), and the application's `app.hpp` (for the `Runtime<ModuleList, CacheKeyList, ...>` composition that scopes everything). The picture exists, but nowhere can it be looked at as a single shape.

Hand-drawn diagrams under `docs/` would drift the moment a transition is added. Build-only diagrams (gitignored) are invisible to PR review and cannot be linked from `CONTEXT.md`, ADRs, or `architecture.md`. Some pipeline that ties the diagrams mechanically to the code — and refuses to let them drift — is the missing piece.

## Solution

A Python script reads an application's `app.hpp`, walks the Modules and Cache keys it composes, extracts behavioral structure from the C++ source, emits a JSON intermediate representation (IR), and renders Mermaid files into a committed `docs/diagrams/` tree. Two artifact kinds, named to match the canonical glossary:

- **Flow diagram** — one per Module that owns a `Flow`. State nodes from `StateList<...>`, edges labeled with the Message type that triggers each transition, with `Transition` / `TransitionNow` / `Done` visually distinct, and the initial state marked.
- **Module graph** — one per `Runtime` composition. Modules and Cache keys are first-class nodes alongside each other. Edges: cross-Module `send<>` (labeled with the Message type), `Owned<K, M>` writer edges (writes), and `cache().subscribe<K, M>()` edges (`KeyChanged`).

The script's input is an `app.hpp` path — never a source directory — because an application is defined by its `Runtime<ModuleList, CacheKeyList, ...>` composition. A library Module not referenced from that file does not appear in that app's Module graph.

The generated `.mmd` files are committed to `docs/diagrams/{flows,modules}/`. A CI step regenerates them and fails the build if `git diff docs/diagrams/` is non-empty. A PR that adds a transition to a Flow but does not include the matching diagram update fails CI loudly. The diagrams cannot lie.

The extractor is plain Python with regex plus a small brace-scope tracker — no libclang dependency. This is sufficient because every CortexFlow `handle()` body follows a stylized pattern: `if (env.payload_type_id() == cortexflow::type_id<X>()) return cortexflow::transition_to<Y>();`. The brace tracker scopes each match to the function it lives in. The receiver-owned-messages convention (ADR-0020) keeps cross-Module `send<>` calls in `.cpp` files, making envelope-edge extraction equally regular.

Mermaid is the v1 renderer. A tldraw renderer is on the roadmap as a follow-up against the existing `tldraw-scratch/` harness; it is out of scope for this PRD.

## User Stories

1. As a maintainer opening `docs/diagrams/flows/consumer.flow.mmd`, I want to see Consumer's full state machine at a glance, so I do not have to read `consumer.hpp` and `consumer.cpp` to learn it.
2. As a maintainer reading a Flow diagram, I want every edge labeled with the Message type that triggers the transition (e.g. `KeyChanged<Counter>`, `Consumer::ProcessingTick`), so I can see *why* each transition happens, not just that it happens.
3. As a maintainer reading a Flow diagram, I want `Transition` and `TransitionNow` rendered as visually distinct arrows, because `TransitionNow` re-enters the new State synchronously with the same Envelope and is a behaviorally different operation per `flow.hpp`.
4. As a maintainer reading a Flow diagram, I want `done()` rendered as a terminal sink node, so the State that ends the Flow is unambiguous.
5. As a maintainer reading a Flow diagram, I want the initial State (the first in `StateList<...>`, or an explicit `InitialStateTag`) marked, so I know where dispatch begins after `flow.start()`.
6. As a maintainer reading the Module graph for `minimal_app`, I want every Module in `Runtime<ModuleList<Producer, Consumer>, ...>` shown as a node, so I can see the application's composition without parsing `app.hpp`.
7. As a maintainer reading the Module graph, I want cross-Module `send<>` calls rendered as labeled edges from sender to receiver (e.g. `Consumer --Producer::Done--> Producer`), so I can see envelope traffic between Modules.
8. As a maintainer reading the Module graph, I want every Cache key (e.g. `Counter`) rendered as a first-class node alongside Modules, so the third communication channel is visible rather than hidden inside a hub.
9. As a maintainer reading the Module graph, I want `Owned<K, M>` declarations rendered as `Module --writes--> CacheKey` edges, so the authoritative writer of each key is visible.
10. As a maintainer reading the Module graph, I want each `cache().subscribe<K, M>()` call rendered as a `CacheKey --KeyChanged--> Module` edge, so subscribers are visible alongside writers.
11. As a maintainer running the generator, I want to invoke it with the path to my application's `app.hpp` (`python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp`), so the script's scope matches the Runtime composition rather than guessing from a directory.
12. As a maintainer running the generator, I want it to refuse to run when handed a directory or a file without `Runtime<...>` in it, so I do not silently get a wrong-shaped graph.
13. As a maintainer maintaining several applications in one repo, I want each application to produce its own Module graph scoped to its own Runtime, so a library Module not in *this* app's composition does not pollute its graph.
14. As a reviewer reading a PR that changes `consumer.cpp`, I want the matching diff to appear in `docs/diagrams/flows/consumer.flow.mmd` in the same PR, so I can see the behavioral impact alongside the code change.
15. As a reviewer reading a PR on GitHub, I want `.mmd` files to render inline in the PR preview, so I can see the diagram without checking out the branch or running any tool.
16. As a release-engineer enforcing diagram integrity, I want a CI step that regenerates the diagrams and fails the build if `git diff docs/diagrams/` is non-empty, so a PR that changes a Flow without regenerating the diagram cannot merge.
17. As a maintainer working offline or on a machine without `clang`, I want the generator to run with only a stock Python 3 install (no libclang, no `compile_commands.json` provisioning), so the diagram tooling is not gated on a heavyweight toolchain.
18. As a maintainer reading the diagrams, I want `Flow diagram` and `Module graph` to be the canonical names for these artifacts, with `flowchart` retired across the docs, so the vocabulary does not shadow the framework primitive **Flow**.
19. As a contributor adding a new Module to an existing application, I want to add it to `ModuleList<...>` in `app.hpp`, regenerate, and see it appear in both its own Flow diagram (if it owns a `Flow`) and the application's Module graph, so the integration is mechanical and visible.
20. As a contributor adding a new transition to a State, I want it to appear in the regenerated Flow diagram with the triggering Message type as the edge label, so the diagram tracks the code change in one step.
21. As a contributor adding a new Cache key declared with `Owned<K, M>`, I want it to appear as a new node with a `writes` edge in the Module graph, so cache topology stays current.
22. As an onboarding engineer reading `docs/architecture.md` or the example's `README.md`, I want to be able to link to a current Flow diagram or Module graph from prose, so explanations have a visual anchor.
23. As a maintainer reading `docs/diagrams/`, I want the layout to be `docs/diagrams/flows/<module>.flow.mmd` and `docs/diagrams/modules/<app>.modules.mmd`, so file location is predictable from the diagram I want to look at.
24. As a maintainer reading ADR-0021, I want the entire pipeline decision — extractor choice, IR-first design, commit policy, CI guard, naming retirement — captured with rejected alternatives, so I do not have to re-litigate the decisions later.
25. As a contributor running the script's tests, I want unit tests for the brace-scope tracker, the Flow extractor, and the Mermaid renderer, plus an end-to-end test against `examples/minimal_app/app.hpp`, so behavioral regressions in the tooling are caught locally before they hit CI.
26. As a contributor writing fixtures for the Flow extractor tests, I want each fixture to be a small synthetic `.hpp`/`.cpp` pair that exercises one extraction case (single transition, `TransitionNow`, `done()`, initial-state override, multi-State), so each test isolates one rule of the extractor.
27. As a contributor reading the Mermaid renderer tests, I want them to be snapshot tests against a fixed IR input, so rendering regressions are obvious from the diff.
28. As a contributor reading the end-to-end test, I want it to invoke the CLI driver against `examples/minimal_app/app.hpp` and assert byte-equality against the committed `.mmd` files, so the same check the CI guard performs is reproducible locally.
29. As a future contributor extending the tooling, I want the IR JSON shape to be stable and renderer-agnostic, so adding a Graphviz, tldraw, or static-HTML renderer costs only a renderer.
30. As a future contributor adding the tldraw renderer (deferred to a follow-up issue), I want to read the IR and emit `insert<Diagram>(editor)` functions matching the existing `tldraw-scratch/src/architectureDiagram.ts` style, so the harness already in the repo is reused.

## Implementation Decisions

**Naming.** `Flow diagram` (per-Module) and `Module graph` (per-application). The term `flowchart` is retired across the repository and not added to the `CONTEXT.md` glossary; both artifact names are tooling vocabulary, not framework primitives.

**Pipeline.** `C++ source → Python extractor → IR (JSON) → Mermaid renderer → docs/diagrams/`. The IR is the seam at which renderers become pluggable; a follow-up tldraw renderer will consume the same IR.

**Script input.** A path to an application's `app.hpp` (the file declaring `Runtime<ModuleList, CacheKeyList, Config>`). The script refuses any other input shape — no directory walks, no glob input. The composition declared in `ModuleList<...>` is the authoritative scope; only those Modules and the Cache keys in `CacheKeyList<Owned<K, M>, ...>` enter that application's diagrams.

**Extractor approach.** Plain Python (no libclang, no `compile_commands.json`). The extractor relies on the stylized CortexFlow handler pattern:

```cpp
if (env.payload_type_id() == cortexflow::type_id<X>()) {
    return cortexflow::transition_to<Y>();
}
return cortexflow::stay();
```

A small brace-scope tracker scopes each `transition_to<>()` / `transition_to_now<>()` / `done()` / `stay()` match to the `Name::handle(FlowCtx&, Envelope&)` function it lives in. Receiver-owned-messages discipline (ADR-0020) keeps cross-Module `send<>` calls in `.cpp` files, making envelope edges equally regular.

**Modules to build.**

| # | Module | Responsibility |
|---|---|---|
| 1 | Brace-scope tracker | Reads a `.hpp`/`.cpp` and returns a mapping of fully-qualified function name → function body text. The only structural-parsing component. |
| 2 | Composition parser | Given an `app.hpp` path, extracts `ModuleList<...>`, `CacheKeyList<Owned<K, M>, ...>`, and the `#include` paths to each Module's header. Returns a small typed app-composition record. |
| 3 | Flow extractor | Given a Module's header and `.cpp`, extracts `StateList<...>`, the initial State, and per-State transitions (triggering Message type + directive kind). Emits a per-Module Flow IR. |
| 4 | Module-graph extractor | Given the app composition + each Module's `.cpp`, finds cross-Module `send<>` calls and `cache().subscribe<K, M>()` calls. Combines with the `Owned<K, M>` writers from the composition parser. Emits an app-level Module-graph IR. |
| 5 | IR schema | Python dataclasses for `FlowIR` and `ModuleGraphIR` plus JSON serialization. The renderer/extractor contract. |
| 6 | Mermaid renderer | Pure function: IR → Mermaid text. Two entry points: `render_flow(flow_ir)` and `render_module_graph(graph_ir)`. |
| 7 | CLI driver | `gen-diagrams.py <app.hpp> [--out docs/diagrams/]`. Orchestrates (2) → (3) and (4) → (5) → (6) → writes `.mmd` files. Shallow glue. |

**Flow diagram detail level (Mid).** Node label = State name from `StateList<...>`. Edge label = the Message type from the matching `payload_type_id() == type_id<X>()` check. Edge style distinguishes `Transition`, `TransitionNow`, and `Done`. Initial State (`StateList::head` or explicit `InitialStateTag`) is marked. State `Locals` decorations and constructor side-effect rendering are deferred (not in v1).

**Module graph edges.**

| Edge | Source | Target | Extracted from |
|---|---|---|---|
| `send<X>(Msg)` | Module | Module | Cross-Module `send<>` calls in `.cpp` |
| `writes` | Module | CacheKey | `Owned<K, M>` declarations in `CacheKeyList` |
| `KeyChanged` | CacheKey | Module | `cache().subscribe<K, M>()` calls |

Non-subscribing Cache readers (Modules that call `cache().get<K>()` without subscribing) and direct cross-Module method calls are deferred; both are hard to distinguish reliably by regex and not load-bearing for v1.

**Output layout.**

```
docs/diagrams/
├── flows/
│   └── <module-name>.flow.mmd
└── modules/
    └── <app-name>.modules.mmd
```

The module-name and app-name are derived from the C++ class name and the enclosing namespace respectively (e.g. `consumer.flow.mmd` for `minimal_app::Consumer`, `minimal_app.modules.mmd` for the `minimal_app` application).

**CI guard.** A pipeline step runs the generator and asserts a clean working tree:

```
python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp
git diff --exit-code docs/diagrams/
```

A non-zero exit fails the build with a clear message asking the contributor to regenerate.

**Script lives at.** `scripts/gen-diagrams.py` (plus a Python package under `scripts/diagrams/` for the seven internal modules). Repository convention places automation scripts under `scripts/`; this is consistent with similar repos.

**Architecture decision.** Recorded in `docs/adr/0021-generated-diagrams-from-cpp-source.md` (Status: Accepted). The eight design decisions and their rejected alternatives are captured there. `docs/adr/README.md` lists ADR-0021 under "Written" and retitled the planned ADR-008 row from `One flowchart per module (v1)` to `One Flow per module (v1)`. `README.md` and `docs/architecture.md` were swept to retire `flowchart` in favor of `Flow`. `docs/prd.md` is left as a historical artifact.

## Testing Decisions

**A good test for this work asserts the IR produced from a fixture, or the Mermaid produced from a fixture IR — never how the parser is implemented internally.** Fixtures are small, focused, and exercise one extraction or rendering rule each.

**Prior art.** None. This is the first Python code in the repo; the test pattern is being set fresh. The framework's own C++ tests use `doctest`; the Python tooling will use `pytest` as the de-facto Python testing standard. The fixture-driven discipline mirrors how the framework's integration tests work: small composed scenarios that assert external behavior, not internal mechanics.

**Modules with tests in v1:**

- **Brace-scope tracker.** Unit tests against small synthetic snippets (`.hpp`/`.cpp` strings) covering: function with nested braces (lambda inside), function whose body contains a string literal with `{` / `}`, function in a namespace, function with a comment containing braces, two functions where one nests another in scope. Each test asserts the returned `{qualified_name: body_text}` mapping for a known input.

- **Flow extractor.** Unit tests against fixture `.hpp`/`.cpp` pairs covering: a Flow with two States and one transition each direction (mirrors `minimal_app::Consumer`); a Flow that uses `transition_to_now<X>()` (asserts directive-kind extraction); a Flow that ends with `done()` (asserts terminal-edge extraction); a Flow with an explicit `InitialStateTag` overriding `StateList::head`; a Flow whose `handle()` body contains multiple `if` branches (asserts multiple transitions extracted per State). Each test asserts the resulting `FlowIR` against a hand-written expected value.

- **Mermaid renderer.** Snapshot tests on fixed `FlowIR` and `ModuleGraphIR` inputs. The expected Mermaid output for each fixture is committed alongside the test and the test asserts byte-equality. A renderer change that intentionally updates the output requires regenerating the snapshot, which is itself a reviewable diff.

- **End-to-end (CLI driver).** One integration test that invokes `gen-diagrams.py examples/minimal_app/app.hpp --out <tmpdir>` and asserts byte-equality between the produced files and the committed files under `docs/diagrams/`. This is the same check the CI guard performs; running it locally reproduces CI's verdict without needing to push.

**Modules without dedicated tests in v1.**

- **Composition parser** and **Module-graph extractor** are covered by the end-to-end test against `examples/minimal_app/app.hpp`. Their inputs (an `app.hpp` and a set of `.cpp` files) are not amenable to small synthetic fixtures the way the brace tracker and Flow extractor are — the smallest meaningful fixture is roughly the size of `minimal_app/` itself. Adding focused unit tests later is straightforward if the E2E proves too coarse a net.

- **IR schema** is data, not behavior. JSON serialization is exercised by the renderer tests (which round-trip IR through serialization) and the E2E test.

- **CLI driver** is shallow glue. The E2E test covers it end-to-end.

**Build configurations the diagrams must remain consistent with.**

- The CI guard runs on every PR. A change to `consumer.cpp` that drifts the diagram fails CI without needing the C++ build to fail.
- The generator itself does not depend on the C++ build state. It is a static-analysis tool over source text.

**No tests for "the rendered Mermaid is valid Mermaid syntax."** The Mermaid renderer is a small pure function; checking its output against a committed snapshot already catches malformed output. Adding a real Mermaid parser as a test dependency would be a heavier toolchain than the generator itself.

## Out of Scope

- **The tldraw renderer.** ADR-0021 staged tldraw as a follow-up after Mermaid is solid. A separate PRD/issue will pick up that work against the existing `tldraw-scratch/` harness once the IR has survived first contact with real diagrams.
- **A libclang-based extractor.** Considered in ADR-0021 and rejected for v1 because the stylized CortexFlow handler pattern makes regex sufficient. Reconsider only if a deviation breaks the regex extractor in a way that cannot be repaired by stricter source conventions.
- **Compile-time self-emission of structural metadata.** Considered and rejected: cannot see inside `handle()` function bodies, so the resulting Flow diagram would have nodes but no verbs.
- **Source annotations** (e.g. `// @transition_on<X> -> Y`). Rejected: self-reported, drifts from code, violates "what is actually implemented."
- **Non-subscribing Cache readers** (Modules that poll `cache().get<K>()` without subscribing). Hard to distinguish reliably from any other Cache access by regex, and not load-bearing for v1. Reconsider if the resulting Module graphs miss material dependencies that hurt comprehension.
- **Direct cross-Module method calls.** The third communication channel listed in `CONTEXT.md`, but rare in framework usage and not reliably distinguishable from ordinary function calls. Reconsider if a deliberate cross-Module direct-call pattern emerges.
- **State `Locals` decorations on Flow diagram nodes.** Showing the State's `Locals` type (e.g. `Idle\n[Subscription]`) and the side effects in its constructor (`cache().subscribe<Counter,...>()`, `timers().arm<Consumer>(...)`) would require parsing constructor bodies, which is a different parse surface than `handle()` bodies. Worth doing later; not in v1.
- **Hand-edited diagrams under `docs/diagrams/`.** The CI guard explicitly disallows them. A maintainer who wants a one-off illustrative diagram should put it elsewhere (e.g. inline in an ADR's Mermaid block).
- **Diagrams for Modules that are not in any `Runtime` composition.** The script refuses to draw library Modules that no `app.hpp` references. A Module that compiles but is unused in every application gets no diagram.
- **Build-time integration (CMake target, header-emitted artifacts, etc.).** The generator is an out-of-tree Python script. CI invokes it; developers invoke it manually when they want fresh diagrams. The C++ build does not depend on it and the generator does not depend on the C++ build.
- **Multi-renderer parallel output in the CLI.** The CLI emits Mermaid only in v1. The IR is renderer-agnostic, but additional renderers (Graphviz, tldraw, static HTML) are separate work.
- **Generating diagrams for the framework primitives themselves.** This work targets *application*-level Flow diagrams and Module graphs. Drawing the framework's internal subsystems (Runtime, Cache, timer service) as a meta-diagram is a different exercise and not in scope.

## Further Notes

- The architectural rationale is in [`docs/adr/0021-generated-diagrams-from-cpp-source.md`](../../docs/adr/0021-generated-diagrams-from-cpp-source.md). Rejection rationale for each alternative considered (libclang, compile-time emission, source annotations, build artifacts only, hand-drawn diagrams, hub-style Cache rendering, tldraw-first, single-renderer pipeline) is recorded there.
- The terminology sweep that retired `flowchart` is partial-by-design: `README.md` and `docs/architecture.md` were updated; `docs/prd.md` and the `.scratch/cortexflow-v1/` legacy issue files were left alone as historical artifacts. Anyone writing new prose should use **Flow** per `CONTEXT.md`.
- The existing `tldraw-scratch/` directory under the workspace root is the harness the future tldraw renderer will target. Its current hand-coded diagrams (`architectureDiagram.ts`, `fluentBitDiagram.ts`, etc.) demonstrate the `insert<Diagram>(editor)` pattern the IR-driven renderer will follow.
- ADR-0021 explicitly forbids hand-edited diagrams under `docs/diagrams/`. A maintainer who wants a one-off illustrative diagram in an ADR should embed Mermaid directly in the ADR's markdown, not commit a `.mmd` to `docs/diagrams/`.
- The session that produced this PRD also surfaced and *did not* take action on:
  - The four-digit-vs-three-digit ADR ID inconsistency in `docs/adr/README.md`'s planned-ADR table. Cosmetic; out of scope.
  - Possible future static enforcement of `Owned<K, M>` writer ownership (referenced by ADR-018). If/when added, it would further strengthen the Module graph's `writes` edges, which currently rely on the same declaration treated as documentation.
