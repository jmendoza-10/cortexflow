# Module graph end-to-end for an application

Status: ready-for-agent
PRD: `.scratch/generated-flow-diagrams/PRD.md` — user stories 6–10, 13, 21, 22, 23 (modules), 25 (E2E), 27, 28, 29 (extended), 30
ADR: `docs/adr/0021-generated-diagrams-from-cpp-source.md`

## Parent

`.scratch/generated-flow-diagrams/PRD.md`

## What to build

Extend the pipeline from issue 01 to produce a single **Module graph** per application — a visualization of every Module and every Cache key declared by the application's `Runtime<ModuleList, CacheKeyList, ...>` composition, with edges showing how they communicate. After this slice, running `gen-diagrams.py examples/minimal_app/app.hpp` writes both `docs/diagrams/flows/<module>.flow.mmd` (from issue 01) *and* `docs/diagrams/modules/<app>.modules.mmd`.

The Module graph treats Cache keys as first-class nodes alongside Modules — never as a hub. For `minimal_app`, that means `Producer`, `Consumer`, and `Counter` all appear as distinct nodes, with `Producer --writes--> Counter` (from the `Owned<Counter, Producer>` declaration in `CacheKeyList`) and `Counter --KeyChanged--> Consumer` (from `consumer.cpp`'s `cache().subscribe<Counter, Consumer>()` call). Cross-Module Envelope traffic appears as `Sender --Message::Type--> Receiver` edges extracted from `send<>` call sites in each Module's `.cpp` files.

The slice extends the components introduced in issue 01: the composition parser learns to extract `CacheKeyList<Owned<K, M>, ...>` in addition to `ModuleList<...>`; the IR schema gains a `ModuleGraphIR` dataclass alongside `FlowIR`; the Mermaid renderer gains a `render_module_graph(graph_ir)` entry point; the CLI driver emits both diagram kinds in a single run; and the end-to-end test asserts the committed module-graph file in addition to the per-Module flow files.

A new component is added: the Module-graph extractor. It walks each Module's `.cpp` to find `send<OtherModule>(OtherModule::Msg{})` calls (per ADR-0020, these only live in `.cpp` files) and `cache().subscribe<K, M>()` calls. The `writes` edges come directly from the composition parser's `Owned<K, M>` extraction — no separate `.cpp` traversal needed for those.

Non-subscribing Cache readers (Modules that call `cache().get<K>()` without subscribing) and direct cross-Module method calls are out of scope per the PRD's deferred-list. Only `send<>`, `subscribe<>`, and `Owned<>` produce edges in this slice.

The committed `docs/diagrams/modules/minimal_app.modules.mmd` is the snapshot the end-to-end test asserts against and the artifact slice 03's CI guard will later validate.

## Acceptance criteria

- [ ] The composition parser extracts `CacheKeyList<Owned<K, M>, ...>` from `app.hpp` and returns each `(CacheKey, WriterModule)` pair in addition to the `ModuleList<...>` already extracted in issue 01
- [ ] The IR schema includes a `ModuleGraphIR` dataclass with nodes for every Module and every Cache key declared by the application, plus edges of three kinds: `send` (Module → Module, labeled with the Message type), `writes` (Module → CacheKey), and `KeyChanged` (CacheKey → Module)
- [ ] The Module-graph extractor finds every `send<OtherModule>(OtherModule::Msg{})` call site in each Module's `.cpp` and emits the corresponding `send` edge with the qualified Message type as the label
- [ ] The Module-graph extractor finds every `cache().subscribe<K, M>()` call site in each Module's `.cpp` and emits the corresponding `KeyChanged` edge (`CacheKey --KeyChanged--> Module`)
- [ ] The Module-graph extractor emits one `writes` edge per `Owned<K, M>` declaration found by the composition parser (no separate `.cpp` traversal is required for `writes` edges)
- [ ] The Mermaid renderer's `render_module_graph(graph_ir)` produces Mermaid text in which Modules and Cache keys are visually distinguishable (e.g. different node shapes or styling) and the three edge kinds carry distinct labels
- [ ] Running `python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp` writes both `docs/diagrams/flows/<module>.flow.mmd` (per issue 01) and `docs/diagrams/modules/minimal_app.modules.mmd` in a single invocation
- [ ] `docs/diagrams/modules/minimal_app.modules.mmd` is committed to the repository, contains nodes for `Producer`, `Consumer`, and `Counter`, and contains edges for `Producer --writes--> Counter`, `Counter --KeyChanged--> Consumer`, and `Consumer --Producer::Done--> Producer` (the cross-Module send in `consumer.cpp`)
- [ ] No edges appear for non-subscribing Cache reads or direct cross-Module method calls (both are out of scope per the PRD)
- [ ] The end-to-end test from issue 01 is extended to assert byte-equality of the committed `docs/diagrams/modules/<app>.modules.mmd` in addition to the flow files
- [ ] All tests (issue 01's plus the extended E2E) pass when run via `python3 -m pytest`
- [ ] No additional system dependencies are introduced beyond what issue 01 required

## Blocked by

`.scratch/generated-flow-diagrams/issues/01-flow-diagrams-end-to-end.md`
