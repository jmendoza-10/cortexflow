"""Composition parser.

Reads an application's `app.hpp` and returns the small typed record the rest
of the pipeline needs: the application namespace, the list of Modules in
`ModuleList<...>`, and the path to each Module's header (derived from the
`#include "..."` lines in `app.hpp`).

This slice only consumes the ModuleList fields. `CacheKeyList<Owned<...>>`
parsing lands with the Module-graph extractor in a later slice.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

from .brace_scope import neutralize


@dataclass
class ModuleEntry:
    """One module declared in `ModuleList<...>`.

    `name` is the short class name (e.g. `Consumer`).
    `header_path` is the resolved filesystem path to the module's `.hpp`,
    derived from the matching `#include` in `app.hpp`.
    `source_path` is the same path with the `.hpp` swapped for `.cpp` if a
    `.cpp` exists; otherwise None (header-only module).
    """

    name: str
    header_path: Path
    source_path: Optional[Path]


@dataclass
class AppComposition:
    """Result of `parse(app_hpp)`."""

    app_namespace: str
    app_short: str
    modules: List[ModuleEntry] = field(default_factory=list)


_RUNTIME_RE = re.compile(r'(?:cortexflow::)?Runtime\s*<')
_MODULELIST_RE = re.compile(
    r'(?:cortexflow::)?ModuleList\s*<\s*([^>]+?)\s*>'
)
_NAMESPACE_RE = re.compile(r'namespace\s+([A-Za-z_]\w*)\s*\{')
_INCLUDE_RE = re.compile(r'#\s*include\s*"([^"]+)"')


class CompositionError(Exception):
    """Raised when an input cannot be interpreted as an application's app.hpp."""


def parse(app_hpp_path: Path) -> AppComposition:
    """Parse an application header. Raises CompositionError on a malformed
    input — the CLI translates that into a clear error message.
    """
    if not app_hpp_path.exists():
        raise CompositionError(
            f'no such file: {app_hpp_path}'
        )
    if app_hpp_path.is_dir():
        raise CompositionError(
            f'{app_hpp_path} is a directory; pass the application\'s app.hpp '
            f'instead (the file declaring Runtime<ModuleList<...>, ...>)'
        )
    text = app_hpp_path.read_text()
    neutral = neutralize(text)

    if not _RUNTIME_RE.search(neutral):
        raise CompositionError(
            f'{app_hpp_path} does not declare a Runtime<...> composition; '
            f'this script only accepts an application\'s app.hpp'
        )

    ml = _MODULELIST_RE.search(neutral)
    if not ml:
        raise CompositionError(
            f'{app_hpp_path} declares Runtime<...> but no ModuleList<...> '
            f'could be located; cannot determine the Modules to draw'
        )
    module_names = [
        m.strip().split('::')[-1]
        for m in ml.group(1).split(',')
        if m.strip()
    ]
    if not module_names:
        raise CompositionError(
            f'{app_hpp_path}\'s ModuleList<...> is empty'
        )

    ns = _NAMESPACE_RE.search(neutral)
    if not ns:
        raise CompositionError(
            f'{app_hpp_path} has no enclosing namespace; expected the '
            f'application to be declared inside `namespace <app_name> {{ ... }}`'
        )
    app_ns = ns.group(1)
    app_short = app_ns

    includes = _INCLUDE_RE.findall(text)
    base_dir = app_hpp_path.parent
    modules: List[ModuleEntry] = []
    for name in module_names:
        header = _resolve_header(name, includes, base_dir)
        if header is None:
            raise CompositionError(
                f'could not locate header for module `{name}` declared in '
                f'{app_hpp_path}\'s ModuleList<...>'
            )
        src = header.with_suffix('.cpp')
        modules.append(ModuleEntry(
            name=name,
            header_path=header,
            source_path=src if src.exists() else None,
        ))

    return AppComposition(
        app_namespace=app_ns,
        app_short=app_short,
        modules=modules,
    )


def _resolve_header(module_name: str, includes: List[str],
                    base_dir: Path) -> Optional[Path]:
    """Find the `#include` whose basename matches `module_name.hpp`
    (case-insensitive) and resolve it relative to `base_dir`.
    """
    target = module_name.lower() + '.hpp'
    for inc in includes:
        if Path(inc).name.lower() == target:
            candidate = (base_dir / inc).resolve()
            if candidate.exists():
                return candidate
    return None
