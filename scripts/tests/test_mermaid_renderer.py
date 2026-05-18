"""Snapshot tests for the Mermaid renderer.

The renderer is a pure function over `FlowIR`, so a byte-equality check
against a committed expected output catches every visual regression. The
fixture exercises all three directive arrow styles, the initial-state marker,
the terminal-sink for `done()`, and an angle-bracketed message type to
verify HTML entity escaping.
"""

from pathlib import Path

from diagrams.ir import (
    DIRECTIVE_DONE,
    DIRECTIVE_TRANSITION,
    DIRECTIVE_TRANSITION_NOW,
    FlowEdge,
    FlowIR,
)
from diagrams.mermaid import render_flow


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
