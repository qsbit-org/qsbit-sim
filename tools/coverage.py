#!/usr/bin/env python3
"""Summarize gcov line coverage for project sources after an instrumented CTest run."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--gcov', default='gcov')
args = parser.parse_args()
build = args.build.resolve()
root = Path(__file__).resolve().parents[1]
output = build / 'coverage'
output.mkdir(exist_ok=True)
objects = sorted((build / 'CMakeFiles').rglob('*.gcda'))
assert objects, 'no instrumented execution data; configure QSBIT_COVERAGE and run tests first'
for obj in objects:
    subprocess.run([args.gcov, '--json-format', '--branch-probabilities', str(obj)],
                   cwd=output, check=True, stdout=subprocess.DEVNULL)
files = {}
for report in output.glob('*.gcov.json.gz'):
    with gzip.open(report, 'rt') as stream:
        data = json.load(stream)
    for item in data['files']:
        path = Path(item['file'])
        if path.is_relative_to(root / 'src') or path.is_relative_to(root / 'include'):
            lines = files.setdefault(str(path.relative_to(root)), {})
            for line in item['lines']:
                number = line['line_number']
                lines[number] = max(lines.get(number, 0), line['count'])
summary = {name: {'covered': sum(n > 0 for n in lines.values()), 'executable': len(lines)}
           for name, lines in sorted(files.items())}
(output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
for name in ['src/isa.cpp', 'src/producer.cpp', 'src/tcu.cpp', 'src/feedback.cpp', 'src/device.cpp']:
    row = summary[name]
    print(f'{name}: {row["covered"]}/{row["executable"]} executable lines')
