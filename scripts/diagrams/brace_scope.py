"""Brace-scope tracker.

Walks C++ source text and returns a mapping {qualified_function_name: body_text}
covering every function definition in the source. Body text is the characters
between the function's outer braces (exclusive of the braces themselves).

This is the only structural-parsing component of the diagram pipeline. The
rest of the extractor relies on the stylized CortexFlow handler pattern; the
tracker exists to scope each pattern match to the function it lives in.

Handled in v1:
    - Nested braces (lambdas, conditionals) in function bodies.
    - String and char literals containing '{' or '}'.
    - Single-line and block comments containing '{' or '}'.
    - Functions nested inside one or more namespaces.
    - Member function definitions like `Idle::handle(...)` keyed by the
      `<namespace>::<class>::handle` qualifier.
    - Forward declarations like `class Foo;` (skipped).

Out of scope: template-prefix recognition for class bodies, namespace alias
declarations, C++17 nested-namespace shortcut (`namespace a::b {`), and
linkage specifications (`extern "C" { ... }`). These are not load-bearing
for the Flow extractor and would expand the parser into territory that the
ADR explicitly chose to avoid.
"""

from __future__ import annotations

import re
from typing import Dict


# Patterns scanned during the structural walk. They are all anchored at the
# current position; the walker only invokes them when it has finished trimming
# whitespace and decides what kind of construct it is looking at.
_NAMESPACE_RE = re.compile(r'namespace\s+([A-Za-z_]\w*)\s*\{')
_ANON_NS_RE = re.compile(r'namespace\s*\{')
_CLASS_RE = re.compile(
    r'(?:class|struct)\s+([A-Za-z_]\w*)\b(?:\s*:[^;{]*)?\s*\{'
)
_FORWARD_DECL_RE = re.compile(r'(?:class|struct)\s+[A-Za-z_]\w*\s*;')

_QUAL_NAME_RE = re.compile(r'((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*$')
_FUNC_TAIL_RE = re.compile(
    r'(?:noexcept(?:\s*\([^)]*\))?|const|override|final|\&{1,2}|\s)*$'
)

# Control-flow keywords that share the `IDENT(...) { ... }` shape with function
# definitions but are not functions. The walker rejects any match whose final
# name-part is one of these.
_NOT_FUNCTION_NAMES = frozenset({
    'if', 'while', 'for', 'switch', 'return', 'else', 'do', 'catch', 'try',
    'static_assert', 'sizeof', 'alignof', 'decltype', 'typeid', 'noexcept',
    'co_await', 'co_yield', 'co_return',
})


def neutralize(source: str) -> str:
    """Replace comment and string-literal contents with whitespace, preserving
    length, newlines, and the positions of every other character. After
    neutralization, braces inside `"..."`, `'...'`, `// ...`, or `/* ... */`
    are gone, so a plain brace count over the result is safe.

    The delimiters (`//`, `"`, `'`, `/*`, `*/`) themselves are preserved as
    a courtesy to anyone debugging the neutralized text; they cannot themselves
    be braces so they do not affect brace accounting.
    """
    out = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        # Block comment.
        if c == '/' and i + 1 < n and source[i + 1] == '*':
            out.append('/')
            out.append('*')
            i += 2
            while i + 1 < n and not (source[i] == '*' and source[i + 1] == '/'):
                out.append('\n' if source[i] == '\n' else ' ')
                i += 1
            if i + 1 < n:
                out.append('*')
                out.append('/')
                i += 2
            else:
                # Unterminated block comment: blank the rest.
                while i < n:
                    out.append('\n' if source[i] == '\n' else ' ')
                    i += 1
            continue
        # Line comment.
        if c == '/' and i + 1 < n and source[i + 1] == '/':
            out.append('/')
            out.append('/')
            i += 2
            while i < n and source[i] != '\n':
                out.append(' ')
                i += 1
            continue
        # String or char literal.
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n and source[i] != quote:
                if source[i] == '\\' and i + 1 < n:
                    out.append(' ')
                    out.append('\n' if source[i + 1] == '\n' else ' ')
                    i += 2
                elif source[i] == '\n':
                    out.append('\n')
                    i += 1
                else:
                    out.append(' ')
                    i += 1
            if i < n:
                out.append(quote)
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def _find_matching_brace(s: str, open_pos: int) -> int:
    depth = 1
    i = open_pos + 1
    n = len(s)
    while i < n:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _find_next_top(s: str, start: int, end: int) -> int:
    """Return the index of the next `;`, `{`, or `}` at paren-depth zero within
    [start, end). Returns -1 if the scope ends before any are seen.
    """
    i = start
    paren_depth = 0
    while i < end:
        c = s[i]
        if c == '(':
            paren_depth += 1
        elif c == ')':
            paren_depth -= 1
        elif paren_depth == 0 and c in ';{}':
            return i
        i += 1
    return -1


def _extract_function_name(sig: str) -> str | None:
    """If `sig` (text between the last statement boundary and a `{`) looks like
    a function definition signature, return the qualified function name.
    Otherwise return None.
    """
    s = sig.rstrip()
    if not s:
        return None
    # Locate the first balanced `(...)` group at top level — that is the
    # function's argument list. Anything after the closing `)` (qualifiers,
    # constructor initializer list) belongs to the signature too but is
    # not part of the args proper.
    depth = 0
    open_paren = -1
    close_paren = -1
    for idx, ch in enumerate(s):
        if ch == '(':
            if depth == 0 and open_paren == -1:
                open_paren = idx
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0 and open_paren >= 0 and close_paren == -1:
                close_paren = idx
                break
    if open_paren < 0 or close_paren < 0:
        return None

    tail = s[close_paren + 1:].strip()
    if tail.startswith(':'):
        # Constructor with initializer list — accept; the brace tracker does
        # not need to parse the initializer.
        pass
    else:
        if not _FUNC_TAIL_RE.fullmatch(tail or ''):
            return None

    before = s[:open_paren].rstrip()
    m = _QUAL_NAME_RE.search(before)
    if not m:
        return None
    name = m.group(1)
    last_part = name.split('::')[-1]
    if last_part in _NOT_FUNCTION_NAMES:
        return None
    return name


def _join_qname(prefix: str, name: str) -> str:
    return f'{prefix}::{name}' if prefix else name


def _scan(s: str, start: int, end: int, prefix: str,
          bodies: Dict[str, str]) -> None:
    i = start
    while i < end:
        c = s[i]
        if c.isspace():
            i += 1
            continue
        if c == '}':
            # End of the enclosing scope reached from a parent call.
            return
        if c == '#':
            # Preprocessor directive — skip to end of line. Comments and
            # string literals are already neutralized, so a `\n` here is a
            # genuine line break in the original source.
            while i < end and s[i] != '\n':
                i += 1
            continue
        # `neutralize` blanks comment *contents* but preserves the `//` and
        # `/* */` delimiters. Skip past them so a file that opens with a
        # license header doesn't get treated as one giant function signature.
        if c == '/' and i + 1 < end and s[i + 1] == '/':
            while i < end and s[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < end and s[i + 1] == '*':
            i += 2
            while i + 1 < end and not (s[i] == '*' and s[i + 1] == '/'):
                i += 1
            if i + 1 < end:
                i += 2
            continue

        m = _FORWARD_DECL_RE.match(s, i, end)
        if m:
            i = m.end()
            continue

        m = _NAMESPACE_RE.match(s, i, end)
        if m:
            name = m.group(1)
            open_pos = m.end() - 1
            close_pos = _find_matching_brace(s, open_pos)
            if close_pos < 0 or close_pos >= end:
                return  # Malformed — bail.
            _scan(s, open_pos + 1, close_pos, _join_qname(prefix, name), bodies)
            i = close_pos + 1
            continue

        m = _ANON_NS_RE.match(s, i, end)
        if m:
            open_pos = m.end() - 1
            close_pos = _find_matching_brace(s, open_pos)
            if close_pos < 0 or close_pos >= end:
                return
            _scan(s, open_pos + 1, close_pos, prefix, bodies)
            i = close_pos + 1
            continue

        m = _CLASS_RE.match(s, i, end)
        if m:
            name = m.group(1)
            open_pos = m.end() - 1
            close_pos = _find_matching_brace(s, open_pos)
            if close_pos < 0 or close_pos >= end:
                return
            _scan(s, open_pos + 1, close_pos, _join_qname(prefix, name), bodies)
            i = close_pos + 1
            while i < end and s[i].isspace():
                i += 1
            if i < end and s[i] == ';':
                i += 1
            continue

        marker = _find_next_top(s, i, end)
        if marker < 0:
            return
        if s[marker] == ';':
            i = marker + 1
            continue
        if s[marker] == '}':
            return

        sig = s[i:marker]
        qname = _extract_function_name(sig)
        open_brace = marker
        close_brace = _find_matching_brace(s, open_brace)
        if close_brace < 0 or close_brace >= end:
            return
        if qname is not None:
            full_name = _join_qname(prefix, qname)
            bodies[full_name] = s[open_brace + 1:close_brace]
        i = close_brace + 1
        while i < end and s[i].isspace():
            i += 1
        if i < end and s[i] == ';':
            i += 1


def functions(source: str) -> Dict[str, str]:
    """Return {qualified_function_name: body_text} for every function definition.

    The returned `body_text` is the neutralized body — comments and string
    literals are stripped — which is what every downstream pass wants when
    scanning for `transition_to<X>()`, `type_id<X>()`, and so on. Callers that
    want the original text can re-read it from the source by line numbers,
    but the Flow extractor does not need that.
    """
    s = neutralize(source)
    bodies: Dict[str, str] = {}
    _scan(s, 0, len(s), '', bodies)
    return bodies
