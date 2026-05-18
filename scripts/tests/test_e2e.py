"""End-to-end test for the diagrams pipeline.

Invokes the CLI driver against `examples/minimal_app/app.hpp`, writes into a
tempdir, and asserts byte-equality between the generated files and the
files committed under `docs/diagrams/flows/`. This is the same check the
CI guard in slice 03 will perform, so the test reproduces CI's verdict
locally.
"""

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_HPP = REPO_ROOT / 'examples' / 'minimal_app' / 'app.hpp'
COMMITTED_FLOWS = REPO_ROOT / 'docs' / 'diagrams' / 'flows'
GEN_DIAGRAMS = REPO_ROOT / 'scripts' / 'gen-diagrams.py'


def test_cli_matches_committed_diagrams(tmp_path):
    out_dir = tmp_path / 'diagrams'
    result = subprocess.run(
        [sys.executable, str(GEN_DIAGRAMS),
         str(APP_HPP), '--out', str(out_dir)],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert result.returncode == 0, (
        f'gen-diagrams.py failed: stdout={result.stdout!r} '
        f'stderr={result.stderr!r}'
    )

    generated = sorted((out_dir / 'flows').glob('*.flow.mmd'))
    committed = sorted(COMMITTED_FLOWS.glob('*.flow.mmd'))
    assert [p.name for p in generated] == [p.name for p in committed], (
        'Set of generated Flow diagrams diverged from the committed set. '
        f'generated={[p.name for p in generated]} '
        f'committed={[p.name for p in committed]}'
    )

    for gen_path, com_path in zip(generated, committed):
        assert gen_path.read_bytes() == com_path.read_bytes(), (
            f'{gen_path.name} drifted from {com_path}. '
            f'Regenerate with `python3 scripts/gen-diagrams.py {APP_HPP}`.'
        )


def test_cli_refuses_directory(tmp_path):
    result = subprocess.run(
        [sys.executable, str(GEN_DIAGRAMS), str(tmp_path)],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert result.returncode != 0
    assert 'directory' in result.stderr.lower()


def test_cli_refuses_missing_file(tmp_path):
    missing = tmp_path / 'does_not_exist.hpp'
    result = subprocess.run(
        [sys.executable, str(GEN_DIAGRAMS), str(missing)],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert result.returncode != 0
    assert 'no such file' in result.stderr.lower()


def test_cli_refuses_file_without_runtime(tmp_path):
    bogus = tmp_path / 'bogus.hpp'
    bogus.write_text('#pragma once\nnamespace x { struct Y {}; }\n')
    result = subprocess.run(
        [sys.executable, str(GEN_DIAGRAMS), str(bogus)],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert result.returncode != 0
    assert 'runtime' in result.stderr.lower()
