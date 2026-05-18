"""CLI driver for `gen-diagrams.py`.

Orchestrates two pipelines in a single invocation:

    composition parser → Flow extractor       → render_flow         →
        <out>/flows/<module>.flow.mmd  (one per Flow-owning Module)

    composition parser → Module-graph extractor → render_module_graph →
        <out>/modules/<app>.modules.mmd  (one per application)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List

from .composition import CompositionError, parse
from .flow_extractor import extract as extract_flow
from .mermaid import render_flow, render_module_graph
from .module_graph_extractor import extract as extract_module_graph


DEFAULT_OUT = Path('docs/diagrams')


def main(argv: List[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog='gen-diagrams.py',
        description=(
            'Generate Flow diagrams and a Module graph from an application\'s '
            'app.hpp. Writes one Flow diagram per Flow-owning Module in the '
            'application\'s ModuleList<...>, plus one Module graph per '
            'application.'
        ),
    )
    parser.add_argument(
        'app_hpp',
        help=(
            'Path to the application\'s app.hpp '
            '(the file declaring Runtime<ModuleList<...>, ...>).'
        ),
    )
    parser.add_argument(
        '--out',
        default=str(DEFAULT_OUT),
        help=(
            f'Output directory for generated diagrams (default: {DEFAULT_OUT}). '
            f'Flow diagrams are written under <out>/flows/; the Module graph '
            f'is written under <out>/modules/.'
        ),
    )
    args = parser.parse_args(argv)

    app_hpp = Path(args.app_hpp)
    try:
        composition = parse(app_hpp)
    except CompositionError as e:
        print(f'gen-diagrams.py: error: {e}', file=sys.stderr)
        return 2

    out_root = Path(args.out)
    flows_dir = out_root / 'flows'
    modules_dir = out_root / 'modules'
    flows_dir.mkdir(parents=True, exist_ok=True)
    modules_dir.mkdir(parents=True, exist_ok=True)

    written: List[Path] = []
    for module in composition.modules:
        flow_ir = extract_flow(
            module_name=module.name,
            app_namespace=composition.app_namespace,
            header_path=module.header_path,
            source_path=module.source_path,
        )
        if flow_ir is None:
            # Module has no Flow — skip silently. The Module still appears in
            # the Module graph below; only Flow-owning Modules get a Flow
            # diagram.
            continue
        out_path = flows_dir / f'{module.name.lower()}.flow.mmd'
        out_path.write_text(render_flow(flow_ir))
        written.append(out_path)

    graph_ir = extract_module_graph(composition)
    modules_path = modules_dir / f'{composition.app_short.lower()}.modules.mmd'
    modules_path.write_text(render_module_graph(graph_ir))
    written.append(modules_path)

    print(f'gen-diagrams.py: wrote {len(written)} file(s):')
    for path in written:
        print(f'  {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
