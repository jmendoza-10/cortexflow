# ADR-0021: Generated Flow diagrams and Module graphs from C++ source

**Status:** Accepted
**Date:** 2026-05-16

## Context

CortexFlow's behavior is encoded entirely in C++ type-level constructs: a module's `StateList<...>` declares its states, `transition_to<X>()` / `transition_to_now<X>()` / `done()` directives inside `handle()` bodies declare its edges, `send<Other>(Msg{})` call sites declare cross-module communication, and `Owned<K, M>` entries in `CacheKeyList` declare which module writes which cache key. There is no separate behavioral spec — the code is the spec.

That has a cost. Understanding "what happens when message X arrives at module Y" requires cross-referencing the module's header (for the `Flow` declaration and `Inbox`), one or more `.cpp` files (for `handle()` bodies and cross-module `send<>` calls), and the application's `app.hpp` (for the `Runtime<ModuleList, Keys, ...>` composition that defines which modules and cache keys are in scope). A reader cannot assemble the picture linearly from one place. Two visual views would help:

- A **per-module state machine** showing each `Flow`'s states, the message types that trigger transitions, and the directive kind of each transition (`Transition` vs `TransitionNow` vs `Done` — semantically distinct per `flow.hpp`).
- A **cross-module communication graph** for a given application, showing envelope traffic and cache flows between modules.

The architectural question is how these diagrams should relate to the C++ that is the source of truth. Hand-drawn diagrams under `docs/` drift immediately and lie about what is actually implemented. Diagrams generated only as build artifacts (gitignored) are invisible in PR review and cannot be linked from `CONTEXT.md` or other ADRs. Some pipeline that ties the diagrams mechanically to the code is required.

A second question is *what to call these artifacts*. "Flowchart" is the natural English word but collides with **Flow** (the framework primitive, per `CONTEXT.md` glossary). The same ambiguity already exists for **State** and is already flagged in `CONTEXT.md`'s Flagged ambiguities section. A new shadow term would compound the problem rather than reduce it.

## Decision

Flow diagrams and Module graphs are **generated from C++ source** by an out-of-tree Python script, **committed under `docs/diagrams/`**, and **guarded by CI** so they cannot drift from the code they describe.

### Naming

- **Flow diagram**: the per-module artifact — nodes are states, edges are transitions. One per module that owns a `Flow`.
- **Module graph**: the per-application artifact — one diagram per `Runtime<...>` composition.

"Flowchart" is not used. Both terms are tooling vocabulary and do not enter the `CONTEXT.md` glossary, which remains reserved for framework primitives.

### Pipeline

```
C++ source → Python extractor → IR (JSON) → renderer(s) → docs/diagrams/{flows,modules}/
```

The extractor walks the source files referenced by a given `app.hpp` and emits a single IR JSON describing every module, state, transition, cache key, and cross-module edge in that app. Renderers are pure functions over the IR.

The v1 renderer target is **Mermaid**. Mermaid renders inline in GitHub PR previews, is text-diffable, and has built-in graph layout — none of which require running a server. A second renderer that emits insertion functions against the existing `tldraw-scratch/` harness is on the roadmap as an interactive companion, not a replacement.

### Extractor

The extractor is **Python with regex plus a small brace-scope tracker**, not libclang. Justification: every CortexFlow `handle()` body follows the stylized pattern

```cpp
if (env.payload_type_id() == cortexflow::type_id<X>()) {
    return cortexflow::transition_to<Y>();
}
return cortexflow::stay();
```

This is parseable by a regex that knows the four directive call shapes (`transition_to<>()`, `transition_to_now<>()`, `done()`, `stay()`) and a brace-counter that tracks which `Name::handle(...)` function body each match is inside. The brace tracker is the only piece of structural parsing needed; it does not need a full C++ AST. The receiver-owned message convention (ADR-0020) keeps cross-module `send<X>(Msg{})` calls in `.cpp` files, which makes envelope-edge extraction for the Module graph similarly stylized.

### Script input

The script takes the path to an application's `app.hpp` (or whichever file declares `Runtime<ModuleList<...>, CacheKeyList<...>, ...>`), not a source directory. The `ModuleList` and `CacheKeyList` already declare exactly which modules and cache keys belong to this application; the script honors that declaration as the authoritative scope. Library modules not referenced from the chosen `app.hpp` do not appear in its Module graph.

### Detail level

Flow diagrams render at "Mid" detail:

- Nodes labeled with state names from `StateList<...>`.
- Initial state (`StateListT::head` or explicit `InitialStateTag`) marked.
- Edges labeled with the message type extracted from `payload_type_id() == type_id<X>()` checks.
- `Transition` and `TransitionNow` rendered as visually distinct arrow styles, because `TransitionNow` re-enters the new state synchronously with the same envelope and is a meaningfully different behavior per `flow.hpp`.
- `done()` rendered as a terminal sink.

Node decorations from a state's `Locals` type (e.g., showing that `Idle::Locals` holds a `Subscription`) are deliberately out of scope for v1; they pull the extractor into constructor bodies, which are a different parse surface.

### Module graph edges

Cache keys are **first-class nodes** in the Module graph alongside modules, not a single hub. Edges:

- `Module --[Message]--> Module` from cross-module `send<X>(Msg{})` call sites in `.cpp`.
- `Module --[writes]--> CacheKey` from `Owned<K, M>` declarations in `CacheKeyList`.
- `CacheKey --[KeyChanged]--> Module` from `cache().subscribe<K, M>()` call sites.

Non-subscribing cache readers (modules that poll `cache().get<K>()` without subscribing) are deferred until their absence is shown to harm comprehension. Direct cross-module method calls are also deferred; they are rare in the framework and not reliably distinguishable from ordinary calls by regex.

### Output and CI

Generated files live under:

```
docs/diagrams/
├── flows/
│   └── <module>.flow.mmd
└── modules/
    └── <app>.modules.mmd
```

The CI pipeline regenerates the diagrams and fails the build if `git diff docs/diagrams/` is non-empty. A PR that modifies a `Flow` or a cross-module `send<>` and does not include the matching diagram update fails CI.

## Consequences

**Enables:**

- Diagrams are PR-reviewable and cannot silently lie about the code. A change to `consumer.cpp` that adds a transition is visible as a hunk in `docs/diagrams/flows/consumer.flow.mmd` in the same PR.
- A reader of `CONTEXT.md` or `docs/architecture.md` can be linked directly to a current diagram instead of being asked to run a tool.
- The IR is renderer-agnostic. Adding a Graphviz/dot output, a static-site HTML view, or the tldraw renderer costs only the renderer; the extractor and IR are unchanged.
- Each application gets a Module graph scoped to its own `Runtime` composition. Multi-app repos do not produce one over-broad picture that is correct for none of them.
- `Owned<K, M>` becomes more than documentation. It is the authoritative source for the `writes` edge in the Module graph, which strengthens the case for future static enforcement (ADR-018).

**Costs:**

- The regex extractor is brittle to deviations from the canonical `handle()` body pattern. A macro that hides a `transition_to<X>()` call site, or a transition computed dynamically (`auto dir = decide(env); return dir;`), will silently disappear from the diagram. A small CI fixture that round-trips a known-good Flow against a committed IR snapshot catches the most common drift class.
- The CI guard adds one job to maintain.
- "Mid" detail requires parsing inside `handle()` function bodies, not just declarations. The brace tracker is the only structural complexity, but it is real code, not a one-line regex.

**Forbids:**

- Hand-edited diagrams under `docs/diagrams/` that diverge from code. The CI guard makes any such edit fail loudly on the next push.
- Diagrams that describe modules outside the chosen `app.hpp`'s `Runtime` composition. The script refuses to draw library modules that the application has not declared. A module that compiles but is not in any `Runtime` does not appear in any Module graph.

## Alternatives considered

- **libclang-based extraction.** AST-correct, sees through macros, handles arbitrary template instantiations. Rejected: the `handle()` body pattern in CortexFlow is stylized enough that regex suffices, and libclang adds a clang-tooling dependency (`compile_commands.json` provisioning, clang version skew, `pip install clang` on every developer machine) that costs more than the precision is worth at v1. Reconsider if a future deviation from the canonical pattern breaks the regex extractor in a way that cannot be repaired by stricter source conventions.

- **Compile-time self-emission.** Build a companion C++ program that template-walks `Runtime<>` and emits JSON via `type_name<T>()`. Trivially accurate for *structure* (every module, every state in every `StateList`, every `Owned<K, M>`). Rejected: it cannot see inside `handle()` function bodies, which is where the message-type edge labels and directive kinds live. The result is a Flow diagram with nodes but no verbs — strictly worse than the regex extractor at the level of detail this ADR commits to.

- **Source annotations (`// @transition_on<X> -> Y`).** State authors hand-annotate each transition. Trivially parseable. Rejected: a self-reported annotation drifts from code at the first refactor that misses a comment update, which contradicts the "what is actually implemented" goal that motivates this work.

- **Build artifacts only, gitignored.** The script emits `.mmd` files into `build/` or similar. No commits, no CI guard. Rejected: severs the link between diagram and PR review. A maintainer changing a Flow cannot see the diagram impact without running the script locally; a reviewer cannot see it at all. The CI guard depends on a committed file to diff against; this alternative removes that anchor.

- **Hand-drawn diagrams in `docs/`.** The status quo before this ADR (none currently exist). Rejected: drifts immediately and offers no mechanical guarantee that the diagram and the code agree.

- **Cache as a single hub node in the Module graph.** All writers point to one "Cache" node; all subscribers point out from it. Matches the language in `CONTEXT.md` literally. Rejected: cache keys are first-class types in CortexFlow ("the type *is* the identity — no enums, no string keys"). Treating them as graph nodes mirrors how the framework treats them and prevents the Cache from becoming a visual star pattern that obscures every diagram.

- **Tldraw as the v1 renderer.** Tldraw is interactive and the repo already has the `tldraw-scratch/` harness. Rejected as v1: tldraw has no built-in graph layout (existing diagrams in `tldraw-scratch/` use hand-coded `x, y` coordinates), is opaque in PR diffs, and requires running the Vite dev server to view. Mermaid is the right v1 target. Tldraw remains on the roadmap as an interactive companion once the IR has stabilized and a layout strategy for the larger Module graph is settled.

- **Single-renderer pipeline (skip the IR).** Have the Python script emit Mermaid directly with no intermediate JSON. Rejected: any future renderer (tldraw, Graphviz, custom HTML) would have to re-parse C++. The IR is cheap to define and cheap to maintain, and it is the seam at which renderers become pluggable.
