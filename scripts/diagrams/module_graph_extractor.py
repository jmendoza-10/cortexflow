"""Module-graph extractor.

Given the parsed `AppComposition` (Modules + Cache keys) and each Module's
`.cpp`, produces a `ModuleGraphIR` covering the three communication channels
this slice supports per ADR-0021 / the PRD:

    send<Other>(Other::Msg{})    -> Module --Msg--> Module
    Owned<K, M>                  -> Module --writes--> CacheKey
    cache().subscribe<K, M>()    -> CacheKey --KeyChanged--> Module

`Owned<K, M>` is read straight off `AppComposition.cache_keys` — no `.cpp`
walk needed for that channel. The cross-Module `send<>` calls and the
`subscribe<>` call sites are pulled from each Module's `.cpp` via regex
against the neutralized (comment- and string-stripped) source.

Boundary modules (e.g. `ButtonReader`) own no Flow and post no envelopes
themselves — the foreign thread or integration test that drives the boundary
calls `app.post(...)` on the module's behalf, and that call lives outside
the runtime where the extractor cannot see it. To keep the diagram from
showing a misleading orphan node, boundary modules may declare their
out-of-source posts via a `// boundary-post: <Receiver> <MessageType>`
comment in the module header; each such line produces a `send` edge as if
the boundary module had called `send<Receiver>(MessageType{})` directly.
The marker is documentation that names a contract the C++ source cannot
express — it is not enforced by the compiler.

Edges are deduplicated before return. The same logical edge can fall out
of two different patterns — e.g. four subscribe call sites in a Module
with four states all yield the same `Key -.-> Module` arrow — and the
rendered graph collapses to a single line per (source, target, kind, label).

Out of scope for v1: non-subscribing Cache reads (`cache().get<K>()` without
a subscription) and direct cross-Module method calls. ADR-0021 records the
rejection rationale; this extractor deliberately ignores both.
"""

from __future__ import annotations

import re
from typing import List, Optional, Set, Tuple

from .brace_scope import neutralize
from .composition import AppComposition
from .ir import (
    EDGE_KEY_CHANGED,
    EDGE_SEND,
    EDGE_WRITES,
    NODE_CACHE_KEY,
    NODE_MODULE,
    ModuleGraphEdge,
    ModuleGraphIR,
    ModuleGraphNode,
)


# `send<Receiver>(Expr...)`. The receiver template argument is a single
# qualified identifier; the call's `(...)` body is matched non-greedily up to
# the closing paren so the caller can pull the message-type expression out
# of `body` if needed. Self-sends (sender == receiver) are filtered out by
# the extractor.
_SEND_RE = re.compile(
    r'\bsend\s*<\s*([A-Za-z_][\w:]*)\s*>\s*\('
)

# `cache().subscribe<Key, Module>()`. We require the receiver `cache()`
# qualifier so the regex does not match unrelated `subscribe<>()` helpers
# that might appear in other contexts.
_SUBSCRIBE_RE = re.compile(
    r'\bcache\s*\(\s*\)\s*\.\s*subscribe\s*<\s*'
    r'([A-Za-z_][\w:]*)\s*,\s*([A-Za-z_][\w:]*)\s*>\s*\('
)

# `// boundary-post: <Receiver> <MessageType>`. Matched against the *raw*
# header text (not the neutralized one) because the marker lives in a
# comment that neutralize() would blank. Both tokens are qualified-identifier
# shaped; the receiver is matched against the composition's ModuleList and
# a marker pointing at an unknown module is dropped to match the existing
# `send<>()` cross-app behavior.
_BOUNDARY_POST_RE = re.compile(
    r'//\s*boundary-post\s*:\s*'
    r'([A-Za-z_][\w:]*)\s+([A-Za-z_][\w:]*)'
)


def extract(composition: AppComposition) -> ModuleGraphIR:
    """Build a Module-graph IR for the application described by `composition`.

    Modules and Cache keys both become first-class nodes (a Cache key is never
    rendered as a hub edge). Edges are appended in a stable order — writes,
    then KeyChanged, then send — so the rendered output diff is reviewable.
    """
    nodes: List[ModuleGraphNode] = []
    nodes.extend(
        ModuleGraphNode(name=m.name, kind=NODE_MODULE)
        for m in composition.modules
    )
    nodes.extend(
        ModuleGraphNode(name=k.key_name, kind=NODE_CACHE_KEY)
        for k in composition.cache_keys
    )

    module_names: Set[str] = {m.name for m in composition.modules}

    edges: List[ModuleGraphEdge] = []

    # writes: from the composition's Owned<K, M> declarations.
    for key in composition.cache_keys:
        edges.append(ModuleGraphEdge(
            source=key.writer_module,
            target=key.key_name,
            kind=EDGE_WRITES,
            label='writes',
        ))

    # KeyChanged and send: walk each Module's .cpp once.
    for module in composition.modules:
        if module.source_path is None:
            continue
        source_text = neutralize(module.source_path.read_text())

        for match in _SUBSCRIBE_RE.finditer(source_text):
            key_name = match.group(1).split('::')[-1]
            subscriber = match.group(2).split('::')[-1]
            edges.append(ModuleGraphEdge(
                source=key_name,
                target=subscriber,
                kind=EDGE_KEY_CHANGED,
                label='KeyChanged',
            ))

        for match in _SEND_RE.finditer(source_text):
            receiver = match.group(1).split('::')[-1]
            if receiver == module.name:
                # Self-send: not a cross-Module envelope edge per the PRD.
                continue
            if receiver not in module_names:
                # send<>() target that is not part of this app's composition
                # — skip rather than draw a dangling edge.
                continue
            message = _extract_send_message(source_text, match.end())
            if message is None:
                continue
            edges.append(ModuleGraphEdge(
                source=module.name,
                target=receiver,
                kind=EDGE_SEND,
                label=message,
            ))

    # boundary-post markers: read each Module's .hpp raw (comments are the
    # signal, not noise — so neutralize() would erase what we're looking for).
    for module in composition.modules:
        header_text = module.header_path.read_text()
        for match in _BOUNDARY_POST_RE.finditer(header_text):
            receiver = match.group(1).split('::')[-1]
            message = match.group(2)
            if receiver == module.name:
                continue
            if receiver not in module_names:
                continue
            edges.append(ModuleGraphEdge(
                source=module.name,
                target=receiver,
                kind=EDGE_SEND,
                label=message,
            ))

    edges = _dedup_edges(edges)

    return ModuleGraphIR(
        app=composition.app_namespace,
        app_short=composition.app_short,
        nodes=nodes,
        edges=edges,
    )


def _dedup_edges(edges: List[ModuleGraphEdge]) -> List[ModuleGraphEdge]:
    """Collapse byte-identical edges, preserving first-seen order. Two
    distinct call sites can yield the same `(source, target, kind, label)`
    tuple — e.g. four state Locals constructors that each call
    `cache().subscribe<DebouncedButtonState, ClickClassifier>()` — and the
    graph should show one arrow per logical relationship, not one per match.
    """
    seen: Set[Tuple[str, str, str, str]] = set()
    out: List[ModuleGraphEdge] = []
    for edge in edges:
        key = (edge.source, edge.target, edge.kind, edge.label)
        if key in seen:
            continue
        seen.add(key)
        out.append(edge)
    return out


def _extract_send_message(text: str, paren_open_end: int) -> Optional[str]:
    """Return the qualified Message type passed to a `send<>()` call.

    `paren_open_end` is the index just past the `(` of the call (the regex
    captures up to and including the `(`). The argument is expected to be a
    temporary of the form `Type{}` / `Type{args}` / `Type(args)` per ADR-0020;
    we read the type expression up to the first `{`, `(`, `,`, or `)`.
    """
    n = len(text)
    i = paren_open_end
    while i < n and text[i].isspace():
        i += 1
    start = i
    depth = 0
    while i < n:
        c = text[i]
        if c == '<':
            depth += 1
        elif c == '>':
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and c in '{(,)':
            break
        i += 1
    expr = text[start:i].strip()
    if not expr:
        return None
    return expr
