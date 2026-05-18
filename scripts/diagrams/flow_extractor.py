"""Flow extractor.

Given a Module's header (and optional `.cpp`), produces a FlowIR. The
extractor relies on the stylized CortexFlow handler pattern documented in
ADR-0021:

    if (env.payload_type_id() == cortexflow::type_id<X>()) {
        return cortexflow::transition_to<Y>();
    }

A Flow body that hides a directive behind a macro, or computes the next
State dynamically, will be silently invisible to the extractor; that is the
v1 trade-off the ADR accepts and the CI guard (slice 03) closes against
silent drift.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import List, Optional, Tuple

from .brace_scope import functions, neutralize
from .ir import (
    DIRECTIVE_DONE,
    DIRECTIVE_TRANSITION,
    DIRECTIVE_TRANSITION_NOW,
    FlowEdge,
    FlowIR,
)


class FlowExtractionError(Exception):
    """Raised when a Flow declaration cannot be parsed for a module."""


# Match a `Flow<Owner, StateList<...>>` or `Flow<Owner, StateList<...>, Tag>`
# template instantiation. Tolerates a leading `cortexflow::` and arbitrary
# whitespace including newlines (the framework code wraps it).
_FLOW_DECL_RE = re.compile(
    r'(?:cortexflow::)?Flow\s*<\s*'
    r'(?P<owner>[A-Za-z_]\w*)\s*,\s*'
    r'(?:cortexflow::)?StateList\s*<\s*(?P<states>[^>]+?)\s*>'
    r'(?P<rest>[^>]*)>',
    re.DOTALL,
)

_DIRECTIVE_NAMES = ('transition_to_now', 'transition_to', 'done', 'stay')
_DIRECTIVE_TO_KIND = {
    'transition_to': DIRECTIVE_TRANSITION,
    'transition_to_now': DIRECTIVE_TRANSITION_NOW,
    'done': DIRECTIVE_DONE,
}


def extract(module_name: str, app_namespace: str,
            header_path: Path,
            source_path: Optional[Path]) -> Optional[FlowIR]:
    """Extract the FlowIR for `module_name`. Returns None if the module
    has no `Flow<...>` member declaration (e.g. `Producer` in `minimal_app`,
    which has an Inbox but no Flow).
    """
    header_text = header_path.read_text()
    source_text = source_path.read_text() if source_path is not None else ''

    decl = _find_flow_decl(header_text, module_name)
    if decl is None:
        return None
    states, initial = decl

    handle_bodies = _gather_handle_bodies(
        states, header_text, source_text,
    )

    edges: List[FlowEdge] = []
    for state in states:
        body = handle_bodies.get(state)
        if body is None:
            # No handle() body found for this state — emit no edges. The
            # State still appears in the IR's state list.
            continue
        edges.extend(_extract_edges_from_body(state, body))

    full_name = f'{app_namespace}::{module_name}' if app_namespace else module_name
    return FlowIR(
        module=full_name,
        module_short=module_name,
        states=states,
        initial_state=initial,
        edges=edges,
    )


def _find_flow_decl(header_text: str,
                    module_name: str) -> Optional[Tuple[List[str], str]]:
    """Locate the `Flow<Owner, StateList<...>, [Initial]>` declaration for
    `module_name` in the header and return (states, initial_state_short_name).
    """
    s = neutralize(header_text)
    for m in _FLOW_DECL_RE.finditer(s):
        if m.group('owner').split('::')[-1] != module_name:
            continue
        states_csv = m.group('states')
        states = [
            t.strip().split('::')[-1]
            for t in states_csv.split(',')
            if t.strip()
        ]
        if not states:
            continue
        rest = m.group('rest').strip()
        initial = states[0]
        if rest:
            # rest is anything between `>` of StateList and the closing `>`
            # of Flow. After stripping a leading `,` it should be the initial
            # state tag.
            tail = rest.lstrip(',').strip()
            if tail:
                initial = tail.split('::')[-1]
        return states, initial
    return None


def _gather_handle_bodies(states: List[str], header_text: str,
                          source_text: str) -> dict:
    """Return {state_short_name: body_text} by scanning both the header and
    the `.cpp` for `State::handle` definitions. The header is scanned first
    so that inline definitions win over (or are overridden by) `.cpp`
    definitions if both exist — the `.cpp` body is preferred because that
    is where CortexFlow conventionally places handler bodies.
    """
    state_set = set(states)
    bodies: dict = {}
    # Header first.
    for qname, body in functions(header_text).items():
        parts = qname.split('::')
        if len(parts) >= 2 and parts[-1] == 'handle' and parts[-2] in state_set:
            bodies[parts[-2]] = body
    # Then source — overrides header.
    if source_text:
        for qname, body in functions(source_text).items():
            parts = qname.split('::')
            if len(parts) >= 2 and parts[-1] == 'handle' and parts[-2] in state_set:
                bodies[parts[-2]] = body
    return bodies


def _extract_edges_from_body(state: str, body: str) -> List[FlowEdge]:
    """Walk a State's `handle()` body and yield the transitions it declares.

    Each `type_id<X>()` reference seen tags the next directive with message
    `X`; the tag is consumed by the next directive. `stay()` consumes a
    pending tag but emits no edge.
    """
    events: List[Tuple[int, str, object]] = []
    for pos, msg in _find_type_id_calls(body):
        events.append((pos, 'msg', msg))
    for pos, kind, target in _find_directive_calls(body):
        events.append((pos, 'dir', (kind, target)))
    events.sort(key=lambda e: e[0])

    edges: List[FlowEdge] = []
    current_msg: Optional[str] = None
    for pos, etype, value in events:
        if etype == 'msg':
            current_msg = _normalize_type(value)
        else:
            kind, target = value
            if kind not in _DIRECTIVE_TO_KIND:
                # `stay` — clear the pending message and continue.
                current_msg = None
                continue
            edges.append(FlowEdge(
                from_state=state,
                to_state=(None if kind == 'done'
                          else _normalize_type(target) if target else None),
                directive=_DIRECTIVE_TO_KIND[kind],
                message=current_msg,
            ))
            current_msg = None
    return edges


def _find_type_id_calls(body: str):
    """Yield (start_pos, type_text) for each `type_id<X>()` call in `body`.
    Handles nested `<>` in `X`.
    """
    for m in re.finditer(r'\btype_id\b', body):
        end = m.end()
        i = end
        n = len(body)
        while i < n and body[i].isspace():
            i += 1
        if i >= n or body[i] != '<':
            continue
        depth = 0
        j = i
        while j < n:
            if body[j] == '<':
                depth += 1
            elif body[j] == '>':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if j >= n:
            continue
        type_text = body[i + 1:j].strip()
        k = j + 1
        while k < n and body[k].isspace():
            k += 1
        if k < n and body[k] == '(':
            yield (m.start(), type_text)


def _find_directive_calls(body: str):
    """Yield (start_pos, kind_word, target_or_None) for each directive call.

    `kind_word` is the verbatim `transition_to`/`transition_to_now`/`done`/
    `stay` token. `target_or_None` is the template argument (e.g. `Idle`) for
    transitions, None for `done()`/`stay()`.
    """
    pattern = re.compile(
        r'\b(transition_to_now|transition_to|done|stay)\b'
    )
    n = len(body)
    for m in pattern.finditer(body):
        kind_word = m.group(1)
        i = m.end()
        target: Optional[str] = None
        if kind_word in ('transition_to', 'transition_to_now'):
            while i < n and body[i].isspace():
                i += 1
            if i >= n or body[i] != '<':
                continue
            depth = 0
            j = i
            while j < n:
                if body[j] == '<':
                    depth += 1
                elif body[j] == '>':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            if j >= n:
                continue
            target = body[i + 1:j].strip().split('::')[-1]
            i = j + 1
        while i < n and body[i].isspace():
            i += 1
        if i >= n or body[i] != '(':
            continue
        # We do not need to validate the `()` content; just confirm a call.
        yield (m.start(), kind_word, target)


def _normalize_type(name: str) -> str:
    """Strip a leading `cortexflow::` from a type expression while preserving
    nested template args (e.g. `cortexflow::KeyChanged<Counter>` -> `KeyChanged<Counter>`).
    """
    name = name.strip()
    while name.startswith('cortexflow::'):
        name = name[len('cortexflow::'):]
    return name
