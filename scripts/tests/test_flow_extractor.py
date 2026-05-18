"""Unit tests for the Flow extractor.

Each test points the extractor at a small synthetic `.hpp`/`.cpp` pair under
`fixtures/flows/` and asserts the resulting `FlowIR`.

The fixtures are intentionally not compilable (they reference cortexflow
headers without the include path) — the extractor is a static-text tool, not
a compiler, and ADR-0021 explicitly rules out a clang dependency.
"""

from pathlib import Path

from diagrams.flow_extractor import extract
from diagrams.ir import (
    DIRECTIVE_DONE,
    DIRECTIVE_TRANSITION,
    DIRECTIVE_TRANSITION_NOW,
    FlowEdge,
)


FIXTURES = Path(__file__).parent / 'fixtures' / 'flows'


def _extract(module: str, basename: str):
    hpp = FIXTURES / f'{basename}.hpp'
    cpp = FIXTURES / f'{basename}.cpp'
    return extract(
        module_name=module,
        app_namespace='fixture',
        header_path=hpp,
        source_path=cpp if cpp.exists() else None,
    )


def test_two_state_flow_extracts_states_and_transitions():
    """Mirrors `minimal_app::Consumer`: two States, one transition in each
    direction, triggered by distinct message types."""
    ir = _extract('TwoState', 'two_state')
    assert ir is not None
    assert ir.module == 'fixture::TwoState'
    assert ir.module_short == 'TwoState'
    assert ir.states == ['Idle', 'Processing']
    assert ir.initial_state == 'Idle'
    assert ir.edges == [
        FlowEdge(
            from_state='Idle',
            to_state='Processing',
            directive=DIRECTIVE_TRANSITION,
            message='KeyChanged<Counter>',
        ),
        FlowEdge(
            from_state='Processing',
            to_state='Idle',
            directive=DIRECTIVE_TRANSITION,
            message='Processing::Tick',
        ),
    ]


def test_transition_now_directive_is_tagged():
    ir = _extract('NowModule', 'transition_now')
    assert ir is not None
    assert ir.states == ['Loading', 'Active']
    assert len(ir.edges) == 1
    edge = ir.edges[0]
    assert edge.from_state == 'Loading'
    assert edge.to_state == 'Active'
    assert edge.directive == DIRECTIVE_TRANSITION_NOW
    assert edge.message == 'Loading::Ready'


def test_done_directive_emits_terminal_edge():
    ir = _extract('DoneModule', 'done_flow')
    assert ir is not None
    assert ir.states == ['Working']
    assert len(ir.edges) == 1
    edge = ir.edges[0]
    assert edge.from_state == 'Working'
    assert edge.to_state is None
    assert edge.directive == DIRECTIVE_DONE
    assert edge.message == 'Working::Shutdown'


def test_initial_state_tag_overrides_state_list_head():
    ir = _extract('TaggedModule', 'initial_tag')
    assert ir is not None
    assert ir.states == ['First', 'Second']
    # First is the StateList head, but the third template argument is
    # `Second`, which should win.
    assert ir.initial_state == 'Second'


def test_multi_branch_handle_emits_one_edge_per_branch():
    ir = _extract('Router', 'multi_branch')
    assert ir is not None
    assert ir.states == ['Dispatch', 'AState', 'BState']
    dispatch_edges = [e for e in ir.edges if e.from_state == 'Dispatch']
    assert len(dispatch_edges) == 3
    expected = {
        ('Dispatch::ToA', DIRECTIVE_TRANSITION, 'AState'),
        ('Dispatch::ToB', DIRECTIVE_TRANSITION, 'BState'),
        ('Dispatch::Halt', DIRECTIVE_DONE, None),
    }
    actual = {(e.message, e.directive, e.to_state) for e in dispatch_edges}
    assert actual == expected


def test_module_without_flow_returns_none():
    """A Module that has no `Flow<...>` member should yield no FlowIR; the
    CLI uses this to skip Flow-less modules without writing empty files."""
    # Producer in minimal_app has no Flow.
    repo_root = Path(__file__).resolve().parents[2]
    hpp = repo_root / 'examples' / 'minimal_app' / 'modules' / 'producer.hpp'
    cpp = repo_root / 'examples' / 'minimal_app' / 'modules' / 'producer.cpp'
    ir = extract(
        module_name='Producer',
        app_namespace='minimal_app',
        header_path=hpp,
        source_path=cpp,
    )
    assert ir is None
