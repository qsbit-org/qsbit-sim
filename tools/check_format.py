#!/usr/bin/env python3
"""Check project C++ formatting without traversing third-party or build trees."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--formatter', default='clang-format')
parser.add_argument('--fix', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = sorted(str(p) for directory in ['include', 'src', 'tests']
               for p in (root / directory).rglob('*') if p.suffix in {'.cpp', '.hpp'})
subprocess.run([args.formatter, *(['-i'] if args.fix else ['--dry-run', '--Werror']), *files], check=True)
