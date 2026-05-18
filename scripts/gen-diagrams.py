#!/usr/bin/env python3
"""Generate Flow diagrams for an application.

Usage:
    python3 scripts/gen-diagrams.py path/to/app.hpp [--out docs/diagrams/]

Reads the application's app.hpp, finds every Flow-owning Module in its
ModuleList<...>, and writes a Mermaid `.mmd` file per Module under
`<out>/flows/`. The Module graph artifact is a separate slice.

Depends only on the Python 3 standard library — no libclang, no clang
toolchain, no `compile_commands.json`.
"""

from __future__ import annotations

import sys
from pathlib import Path


# Ensure `import diagrams.<...>` resolves whether the script is invoked as
# `python3 scripts/gen-diagrams.py` or `python3 -m scripts.gen_diagrams`.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from diagrams.cli import main  # noqa: E402


if __name__ == '__main__':
    raise SystemExit(main())
