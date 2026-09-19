#!/usr/bin/env python3
"""Exercise dependency discovery without relying on a user's CMake environment."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cmake', required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--toolchain', type=Path, required=True)
parser.add_argument('--configuration', required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='qsbit-dependencies-') as directory:
    root = Path(directory)
    poison = root / 'unrelated-install'
    poison.mkdir()
    (poison / 'SystemCLanguageConfig.cmake').write_text(
        'message(FATAL_ERROR "Unrelated SystemC package was loaded")\n')
    environment = dict(os.environ, CMAKE_PREFIX_PATH=str(poison))
    command = [args.cmake, '-S', str(args.source), '-G', 'Ninja',
               '-DBUILD_TESTING=OFF', '-DQSBIT_PYTHON_BACKENDS=OFF',
               f'-DCMAKE_BUILD_TYPE={args.configuration}']
    missing = subprocess.run(
        [*command, '-B', str(root / 'missing'),
         f'-DCMAKE_TOOLCHAIN_FILE={root / "missing-toolchain.cmake"}'],
        env=environment, text=True, capture_output=True, timeout=30)
    assert missing.returncode != 0, missing.stdout + missing.stderr
    assert 'Conan dependencies are not prepared' in missing.stderr, missing.stderr
    assert not (root / 'missing' / '_deps').exists()

    configured = subprocess.run(
        [*command, '-B', str(root / 'configured'),
         f'-DCMAKE_TOOLCHAIN_FILE={args.toolchain}',
         f'-DSystemCLanguage_DIR={poison}'],
        env=environment, text=True, capture_output=True, timeout=60)
    assert configured.returncode == 0, configured.stdout + configured.stderr
    cache = (root / 'configured' / 'CMakeCache.txt').read_text()
    assert f'SystemCLanguage_DIR:PATH={args.toolchain.parent}' in cache, cache
    assert not (root / 'configured' / '_deps').exists()
    examples = subprocess.run(
        [args.cmake, '--build', str(root / 'configured'), '--target', 'example_images'],
        text=True, capture_output=True, timeout=30)
    assert examples.returncode == 0, examples.stdout + examples.stderr
    for name in ('bell', 'feedback', 'pulse', 'overlap'):
        assert (root / 'configured' / 'examples' / f'{name}.elf').read_bytes()[:4] == b'\x7fELF'
    assert (root / 'configured' / 'examples' / 'runs' / 'scripted.json').is_file()

    minimal = subprocess.run(
        [*command, '-B', str(root / 'minimal'),
         f'-DCMAKE_TOOLCHAIN_FILE={args.toolchain}', '-DQSBIT_BUILD_EXAMPLES=OFF'],
        env=environment, text=True, capture_output=True, timeout=60)
    assert minimal.returncode == 0, minimal.stdout + minimal.stderr
    assert not (root / 'minimal' / 'examples').exists()
    assert 'RISCV_AS:' not in (root / 'minimal' / 'CMakeCache.txt').read_text()
    mismatched = subprocess.run(
        [*command, '-B', str(root / 'mismatched'),
         f'-DCMAKE_TOOLCHAIN_FILE={args.toolchain}',
         '-DCMAKE_BUILD_TYPE=Release' if args.configuration == 'Debug'
         else '-DCMAKE_BUILD_TYPE=Debug'],
        env=environment, text=True, capture_output=True, timeout=60)
    assert mismatched.returncode != 0, mismatched.stdout + mismatched.stderr
    assert 'Build type differs' in mismatched.stderr, mismatched.stderr
print('Missing setup is actionable; Conan discovery ignores unrelated installations.')
