# Module graph end-to-end for an application

Status: ready-for-merge
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

## Comments

Built the Module-graph half of the pipeline on top of slice 01's
scaffolding. The CLI now emits both diagram kinds in a single
invocation; `python3 scripts/gen-diagrams.py examples/minimal_app/app.hpp`
writes `docs/diagrams/flows/consumer.flow.mmd` (unchanged from slice 01)
and `docs/diagrams/modules/minimal_app.modules.mmd` (new), and the
extended E2E test asserts byte-equality against both committed
artifacts. `python3 -m pytest scripts/` runs 28 green tests.

What the reviewer should look at:

- **Module-graph extractor lives at
  `scripts/diagrams/module_graph_extractor.py`.** It walks each
  Module's `.cpp` with two regexes — one for `send<Receiver>(...)` and
  one for `cache().subscribe<K, M>()` — over the neutralized source
  (comments and string literals already stripped by the existing
  `brace_scope.neutralize`). Self-sends (sender == receiver) are
  dropped because the PRD's `send<>` edge is "cross-Module" only; the
  `Producer::on_start` / `Producer::on(Done&)` self-sends in
  `producer.cpp` therefore do not appear in the graph. Sends to a
  Module not in this app's composition are also dropped (defensive,
  not exercised by `minimal_app`).

- **Send-label parsing reads the argument expression, not the template
  parameter.** `send<Producer>(Producer::Done{})` becomes
  `Consumer --Producer::Done--> Producer` — the label is the qualified
  Message type as written in source, not the receiver. This matches
  the issue's example and the PRD's user story 7. The parser walks
  from the `(` to the first `{`, `(`, `,`, or top-level `)`, with
  `<...>` template arguments allowed inside the type expression. For
  `minimal_app`'s style the parsed expression is the bare type name
  (`Producer::Done`); a hypothetical `send<X>(Y::Msg{some_arg})`
  would still parse correctly because the `{` stops the read.

- **`writes` edges come straight from the composition parser.** The
  acceptance criterion calls this out explicitly: no separate `.cpp`
  traversal is required for `writes` edges. `cache().set<K>()` calls
  in `producer.cpp` are deliberately ignored — the
  authoritative-writer relationship is the `Owned<K, M>` declaration
  in `CacheKeyList`, not the call site.

- **Modules and Cache keys use different node shapes.** Modules
  render as labelled rectangles (`Name["Name"]`), Cache keys as
  labelled rounded nodes (`Name(("Name"))`). Each kind also has its
  own `classDef` for fill/stroke colour so the visual distinction
  survives in monochrome rendering too. The PRD's user story 8 is
  "Cache keys are first-class nodes alongside Modules, never hidden
  inside a hub" — different shapes are the most direct way to convey
  that they are co-equal but not the same kind of thing.

- **Edge styles for the three edge kinds.** `send` uses a plain solid
  arrow (`-->`), `writes` uses a heavy arrow (`==>`) because writer
  ownership is a stronger structural fact than a transient send, and
  `KeyChanged` uses a dashed arrow (`-.->`) to convey async fanout.
  These mirror the three Flow-diagram directive arrow styles from
  slice 01, so the two diagram kinds share a visual idiom.

- **Composition parser now uses balanced-bracket walking instead of
  `[^>]+?`.** `ModuleList<Producer, Consumer>` worked under the old
  pattern, but `CacheKeyList<Owned<Counter, Producer>>` has nested
  `>` and would have been truncated. The new `_find_template_body`
  helper counts `<` / `>` depth so nested template args are read in
  full, and `_split_top_level` splits the body at depth-zero commas.
  This is also why a `Runtime<ModuleList<...>, CacheKeyList<...>>`
  in `app.hpp` continues to read correctly.

- **Renderer snapshot for the new path.** A second fixture
  (`scripts/tests/snapshots/fixture_app.modules.mmd`) exercises both
  node kinds, all three edge kinds, and an angle-bracketed `send`
  label (`Consumer::Audit<Counter>`) to verify the same HTML-entity
  escape path used by `render_flow` covers Module-graph edges too.

- **E2E test extension is symmetric with slice 01's flow check.**
  After the existing flow comparison the test now diffs the contents
  of `<out>/modules/` against `docs/diagrams/modules/`. Adding a new
  app under `examples/` will require its own
  `docs/diagrams/modules/<app>.modules.mmd`; the test as written
  iterates over both committed sets and would catch a missing or
  extra file.

Deferred (still not in scope, called out for slice 03 or later):

- **CI guard.** Slice 03's responsibility per ADR-0021 — the E2E test
  reproduces the guard locally; CI wiring is still pending.
- **Non-subscribing Cache reads (`cache().get<K>()`).** PRD-deferred
  and out of scope here; the extractor deliberately does not match
  this pattern.
- **Direct cross-Module method calls.** PRD-deferred (third
  communication channel in `CONTEXT.md`, but rare and hard to
  distinguish from ordinary function calls).
- **Library Modules / multi-app composition.** Only `minimal_app` is
  in the repo today; the CLI is scoped to one `app.hpp` per
  invocation as the PRD requires.

— 2026-05-18, from afk worker
