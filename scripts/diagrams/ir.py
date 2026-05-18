"""IR dataclasses for generated Flow diagrams and Module graphs.

The IR is the renderer/extractor contract. A future tldraw or Graphviz
renderer should be a pure function over these dataclasses and need no other
information from the extractor.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import List, Optional


# Directive kinds, named to match the verbs in `cortexflow/flow.hpp`.
DIRECTIVE_TRANSITION = 'Transition'
DIRECTIVE_TRANSITION_NOW = 'TransitionNow'
DIRECTIVE_DONE = 'Done'

# Module-graph edge kinds, named to match the verbs in CONTEXT.md / PRD.
EDGE_SEND = 'send'
EDGE_WRITES = 'writes'
EDGE_KEY_CHANGED = 'KeyChanged'

# Module-graph node kinds.
NODE_MODULE = 'Module'
NODE_CACHE_KEY = 'CacheKey'


@dataclass
class FlowEdge:
    """One transition emitted by a State's `handle()` body.

    `to_state` is None when the directive is `Done` — the flow terminates
    rather than entering another State.

    `message` is the message type from the `payload_type_id() == type_id<X>()`
    check that immediately precedes the directive in source order. It is
    None for an unguarded directive (`return done()`, `return stay()`).
    """

    from_state: str
    to_state: Optional[str]
    directive: str
    message: Optional[str]


@dataclass
class FlowIR:
    """Per-Module Flow diagram IR.

    `module` is the fully-qualified module name (e.g. `minimal_app::Consumer`).
    `module_short` is the unqualified class name used to derive the output
    filename (`consumer.flow.mmd`).
    """

    module: str
    module_short: str
    states: List[str]
    initial_state: str
    edges: List[FlowEdge] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            'module': self.module,
            'module_short': self.module_short,
            'states': list(self.states),
            'initial_state': self.initial_state,
            'edges': [asdict(e) for e in self.edges],
        }

    @classmethod
    def from_dict(cls, d: dict) -> 'FlowIR':
        return cls(
            module=d['module'],
            module_short=d['module_short'],
            states=list(d['states']),
            initial_state=d['initial_state'],
            edges=[FlowEdge(**e) for e in d.get('edges', [])],
        )


@dataclass
class ModuleGraphNode:
    """One node in the Module graph: either a Module or a Cache key.

    `name` is the short class name used both as the rendered label and the
    Mermaid identifier (e.g. `Producer`, `Counter`).
    `kind` is `NODE_MODULE` or `NODE_CACHE_KEY` so the renderer can give
    Modules and Cache keys visually distinct shapes / styling.
    """

    name: str
    kind: str


@dataclass
class ModuleGraphEdge:
    """One edge in the Module graph.

    `source` and `target` are the short class names of the endpoints. They
    must match a `ModuleGraphNode.name` already in the graph.

    `kind` is `EDGE_SEND`, `EDGE_WRITES`, or `EDGE_KEY_CHANGED`. The renderer
    chooses arrow style and label from this.

    `label` is the rendered text on the edge:
        - For `send`: the qualified Message type (e.g. `Producer::Done`).
        - For `writes`: the literal string `writes`.
        - For `KeyChanged`: the literal string `KeyChanged`.
    """

    source: str
    target: str
    kind: str
    label: str


@dataclass
class ModuleGraphIR:
    """Per-application Module-graph IR.

    `app` is the application namespace (e.g. `minimal_app`).
    `app_short` is the short name used to derive the output filename
    (`minimal_app.modules.mmd`).
    """

    app: str
    app_short: str
    nodes: List[ModuleGraphNode] = field(default_factory=list)
    edges: List[ModuleGraphEdge] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            'app': self.app,
            'app_short': self.app_short,
            'nodes': [asdict(n) for n in self.nodes],
            'edges': [asdict(e) for e in self.edges],
        }

    @classmethod
    def from_dict(cls, d: dict) -> 'ModuleGraphIR':
        return cls(
            app=d['app'],
            app_short=d['app_short'],
            nodes=[ModuleGraphNode(**n) for n in d.get('nodes', [])],
            edges=[ModuleGraphEdge(**e) for e in d.get('edges', [])],
        )
