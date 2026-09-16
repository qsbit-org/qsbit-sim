#!/usr/bin/env python3
"""Provision pinned build dependencies into a user-selected local directory."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

SYSTEMC_REVISION = '11ad094d282fd5330b27ab57f90f9d231a763da1'
ARCH_REVISION = '37e6e0022814d880375e4a310b4a9a10fb9b268a'


def run(*command, **kwargs):
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def checkout(url, directory, revision):
    if not (directory / '.git').exists():
        run('git', 'clone', '--filter=blob:none', '--no-checkout', url, directory)
    run('git', '-C', directory, 'fetch', '--depth', '1', 'origin', revision)
    run('git', '-C', directory, 'checkout', '--detach', revision)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=Path('tmp/deps'))
    parser.add_argument('--architecture-tests', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    target = args.directory.resolve()
    target.mkdir(parents=True, exist_ok=True)
    if sys.prefix == sys.base_prefix:
        raise SystemExit('Run this installer inside a virtual environment.')
    if importlib.util.find_spec('pip') is None:
        run(sys.executable, '-m', 'ensurepip', '--upgrade')
    run(sys.executable, '-m', 'pip', 'install', 'cmake==3.31.10', 'ninja==1.13.2',
        'clang-format==18.1.8', '-r', root / 'requirements-backends.txt',
        '-r', root / 'requirements-verification.txt')
    checkout('https://github.com/accellera-official/systemc.git', target / 'systemc', SYSTEMC_REVISION)
    cmake = Path(sys.executable).parent / 'cmake'
    run(cmake, '-S', target / 'systemc', '-B', target / 'systemc-build', '-DCMAKE_CXX_STANDARD=20',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=ON', '-DENABLE_EXAMPLES=OFF',
        '-DENABLE_REGRESSION=OFF', f'-DCMAKE_INSTALL_PREFIX={target}/systemc-install')
    run(cmake, '--build', target / 'systemc-build', '-j', min(os.cpu_count() or 2, 8))
    run(cmake, '--install', target / 'systemc-build')
    if args.architecture_tests:
        checkout('https://github.com/riscv/riscv-arch-test.git', target / 'riscv-arch-test', ARCH_REVISION)
    print(f'Dependencies ready. Set CMAKE_PREFIX_PATH={target}/systemc-install')


if __name__ == '__main__':
    main()
