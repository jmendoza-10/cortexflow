"""CLI driver for `gen-diagrams.py`.

Orchestrates: composition parser → Flow extractor → Mermaid renderer → write
`<out>/flows/<module>.flow.mmd` for every Flow-owning Module in the
application's `ModuleList<...>`. The Module graph is a separate slice; this
CLI deliberately emits only Flow diagrams.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List

from .composition import CompositionError, parse
from .flow_extractor import extract
from .mermaid import render_flow


DEFAULT_OUT = Path('docs/diagrams')


def main(argv: List[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog='gen-diagrams.py',
        description=(
            'Generate Flow diagrams from an application\'s app.hpp. '
            'Writes one Mermaid file per Flow-owning Module in the '
            'application\'s ModuleList<...>.'
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
            f'Flow diagrams are written under <out>/flows/.'
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
    flows_dir.mkdir(parents=True, exist_ok=True)

    written: List[Path] = []
    for module in composition.modules:
        flow_ir = extract(
            module_name=module.name,
            app_namespace=composition.app_namespace,
            header_path=module.header_path,
            source_path=module.source_path,
        )
        if flow_ir is None:
            # Module has no Flow — skip silently. The Module still appears in
            # the Module graph (a later slice); only Flow-owning Modules get
            # a Flow diagram.
            continue
        out_path = flows_dir / f'{module.name.lower()}.flow.mmd'
        mermaid_text = render_flow(flow_ir)
        out_path.write_text(mermaid_text)
        written.append(out_path)

    if written:
        print(
            'gen-diagrams.py: wrote Flow diagrams for '
            f'{len(written)} module(s):'
        )
        for path in written:
            print(f'  {path}')
    else:
        print(
            'gen-diagrams.py: no Flow-owning Modules found in '
            f'{app_hpp}\'s ModuleList<...>; no files written'
        )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
