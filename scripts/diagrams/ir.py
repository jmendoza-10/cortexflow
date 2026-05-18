"""IR dataclasses for generated Flow diagrams.

The IR is the renderer/extractor contract. A future tldraw or Graphviz
renderer should be a pure function over this dataclass and need no other
information from the extractor.

Only `FlowIR` is defined in this slice; the Module-graph IR lands in a
later slice once its extractor is in place.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import List, Optional


# Directive kinds, named to match the verbs in `cortexflow/flow.hpp`.
DIRECTIVE_TRANSITION = 'Transition'
DIRECTIVE_TRANSITION_NOW = 'TransitionNow'
DIRECTIVE_DONE = 'Done'


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
