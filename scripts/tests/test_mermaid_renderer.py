"""Snapshot tests for the Mermaid renderer.

The renderer is a pure function over the IR dataclasses, so byte-equality
checks against committed expected output catch every visual regression. The
fixtures exercise:

    - render_flow: all three directive arrow styles, the initial-state
      marker, the terminal-sink for `done()`, and an angle-bracketed message
      type to verify HTML entity escaping.
    - render_module_graph: both node kinds (Module / CacheKey), all three
      edge kinds (send / writes / KeyChanged), and an angle-bracketed send
      label to verify entity escaping is shared with the Flow renderer.
"""

from pathlib import Path

from diagrams.ir import (
    DIRECTIVE_DONE,
    DIRECTIVE_TRANSITION,
    DIRECTIVE_TRANSITION_NOW,
    EDGE_KEY_CHANGED,
    EDGE_SEND,
    EDGE_WRITES,
    FlowEdge,
    FlowIR,
    ModuleGraphEdge,
    ModuleGraphIR,
    ModuleGraphNode,
    NODE_CACHE_KEY,
    NODE_MODULE,
)
from diagrams.mermaid import render_flow, render_module_graph


SNAPSHOTS = Path(__file__).parent / 'snapshots'


def _fixture_flow_ir() -> FlowIR:
    return FlowIR(
        module='fixture::Router',
        module_short='Router',
        states=['Dispatch', 'AState', 'BState'],
        initial_state='Dispatch',
        edges=[
            FlowEdge(
                from_state='Dispatch',
                to_state='AState',
                directive=DIRECTIVE_TRANSITION,
                message='Router::ToA',
            ),
            FlowEdge(
                from_state='Dispatch',
                to_state='BState',
                directive=DIRECTIVE_TRANSITION_NOW,
                message='Router::ToB',
            ),
            FlowEdge(
                from_state='Dispatch',
                to_state=None,
                directive=DIRECTIVE_DONE,
                message='Router::Halt',
            ),
            FlowEdge(
                from_state='AState',
                to_state='Dispatch',
                directive=DIRECTIVE_TRANSITION,
                message='AState::Reset<Counter>',
            ),
        ],
    )


def test_renderer_snapshot_matches_committed_output():
    ir = _fixture_flow_ir()
    rendered = render_flow(ir)
    snapshot_path = SNAPSHOTS / 'three_state.flow.mmd'
    expected = snapshot_path.read_text()
    assert rendered == expected, (
        f'Renderer output drifted from snapshot.\n'
        f'  rendered: {rendered!r}\n'
        f'  expected: {expected!r}\n'
        f'If this change is intentional, regenerate the snapshot at '
        f'{snapshot_path} and review the diff.'
    )


def test_renderer_round_trips_through_ir_json():
    """JSON round-trip is the seam future renderers will use to consume IR
    without re-parsing source. Snapshot-equivalence after a round-trip means
    nothing renderer-relevant is lost in serialization."""
    ir = _fixture_flow_ir()
    restored = FlowIR.from_dict(ir.to_dict())
    assert render_flow(restored) == render_flow(ir)


def _fixture_module_graph_ir() -> ModuleGraphIR:
    """Three-Module fixture covering every node/edge kind. An angle-bracketed
    `send` label exercises the same HTML escape path as `render_flow`."""
    return ModuleGraphIR(
        app='fixture_app',
        app_short='fixture_app',
        nodes=[
            ModuleGraphNode(name='Producer', kind=NODE_MODULE),
            ModuleGraphNode(name='Consumer', kind=NODE_MODULE),
            ModuleGraphNode(name='Auditor', kind=NODE_MODULE),
            ModuleGraphNode(name='Counter', kind=NODE_CACHE_KEY),
        ],
        edges=[
            ModuleGraphEdge(
                source='Producer', target='Counter',
                kind=EDGE_WRITES, label='writes',
            ),
            ModuleGraphEdge(
                source='Counter', target='Consumer',
                kind=EDGE_KEY_CHANGED, label='KeyChanged',
            ),
            ModuleGraphEdge(
                source='Consumer', target='Producer',
                kind=EDGE_SEND, label='Producer::Done',
            ),
            ModuleGraphEdge(
                source='Auditor', target='Consumer',
                kind=EDGE_SEND, label='Consumer::Audit<Counter>',
            ),
        ],
    )


def test_module_graph_renderer_snapshot_matches_committed_output():
    ir = _fixture_module_graph_ir()
    rendered = render_module_graph(ir)
    snapshot_path = SNAPSHOTS / 'fixture_app.modules.mmd'
    expected = snapshot_path.read_text()
    assert rendered == expected, (
        f'Module-graph renderer output drifted from snapshot.\n'
        f'  rendered: {rendered!r}\n'
        f'  expected: {expected!r}\n'
        f'If this change is intentional, regenerate the snapshot at '
        f'{snapshot_path} and review the diff.'
    )


def test_module_graph_renderer_round_trips_through_ir_json():
    ir = _fixture_module_graph_ir()
    restored = ModuleGraphIR.from_dict(ir.to_dict())
    assert render_module_graph(restored) == render_module_graph(ir)
