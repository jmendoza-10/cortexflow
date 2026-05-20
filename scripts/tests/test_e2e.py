"""End-to-end test for the diagrams pipeline.

Invokes the CLI driver against every example app, writes into a tempdir,
and asserts byte-equality between the generated files and the files
committed under `docs/diagrams/{flows,modules}/`. This is the same check
the CI drift guard performs, so the test reproduces CI's verdict locally.
"""

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_HPPS = [
    REPO_ROOT / 'examples' / 'minimal_app' / 'app.hpp',
    REPO_ROOT / 'examples' / 'button_pipeline' / 'app.hpp',
]
COMMITTED_FLOWS = REPO_ROOT / 'docs' / 'diagrams' / 'flows'
COMMITTED_MODULES = REPO_ROOT / 'docs' / 'diagrams' / 'modules'
GEN_DIAGRAMS = REPO_ROOT / 'scripts' / 'gen-diagrams.py'


def test_cli_matches_committed_diagrams(tmp_path):
    out_dir = tmp_path / 'diagrams'
    for app_hpp in APP_HPPS:
        result = subprocess.run(
            [sys.executable, str(GEN_DIAGRAMS),
             str(app_hpp), '--out', str(out_dir)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        assert result.returncode == 0, (
            f'gen-diagrams.py failed for {app_hpp}: '
            f'stdout={result.stdout!r} stderr={result.stderr!r}'
        )

    generated_flows = sorted((out_dir / 'flows').glob('*.flow.mmd'))
    committed_flows = sorted(COMMITTED_FLOWS.glob('*.flow.mmd'))
    assert [p.name for p in generated_flows] == [p.name for p in committed_flows], (
        'Set of generated Flow diagrams diverged from the committed set. '
        f'generated={[p.name for p in generated_flows]} '
        f'committed={[p.name for p in committed_flows]}'
    )

    for gen_path, com_path in zip(generated_flows, committed_flows):
        assert gen_path.read_bytes() == com_path.read_bytes(), (
            f'{gen_path.name} drifted from {com_path}. '
            f'Regenerate by running gen-diagrams.py against each app.hpp '
            f'under examples/.'
        )

    generated_modules = sorted((out_dir / 'modules').glob('*.modules.mmd'))
    committed_modules = sorted(COMMITTED_MODULES.glob('*.modules.mmd'))
    assert [p.name for p in generated_modules] == [p.name for p in committed_modules], (
        'Set of generated Module graphs diverged from the committed set. '
        f'generated={[p.name for p in generated_modules]} '
        f'committed={[p.name for p in committed_modules]}'
    )

    for gen_path, com_path in zip(generated_modules, committed_modules):
        assert gen_path.read_bytes() == com_path.read_bytes(), (
            f'{gen_path.name} drifted from {com_path}. '
            f'Regenerate by running gen-diagrams.py against each app.hpp '
            f'under examples/.'
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
