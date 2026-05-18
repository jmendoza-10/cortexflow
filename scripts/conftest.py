"""Put `scripts/` on sys.path so tests can `import diagrams.*` without
having to install the package. Mirrors what `gen-diagrams.py` does for
its own entry point.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
