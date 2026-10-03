"""Run the checked-in JSON examples through the CLI and inspect their outputs."""

import argparse
import json
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--simulator', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--scenarios', nargs='+', required=True)
args = parser.parse_args()

directories = {'bell': 'bell-state', 'feedback': 'measurement-feedback',
               'mock': 'measurement-feedback', 'pulse': 'single-qubit-pulse',
               'overlap': 'overlapping-pulses'}
for name in args.scenarios:
    config_path = args.build / 'examples' / directories[name] / ('mock.json' if name == 'mock' else 'run.json')
    config = json.loads(config_path.read_text())
    summary_path = config_path.parent / config['summary']
    trace_path = config_path.parent / config['trace']
    result = subprocess.run([str(args.simulator), '--config', str(config_path)],
                            cwd=args.build, capture_output=True, text=True,
                            timeout=30)
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    summary = json.loads(summary_path.read_text())
    trace = [json.loads(line) for line in trace_path.read_text().splitlines()]
    assert summary['success'] and trace[-1]['kind'] == 'SimulationCompleted', name
    assert summary['backend'] == config['backend'], name
    assert summary['configuration']['start'] == 200, name
    starts = [e['tick'] for e in trace if e['kind'] == 'OperationStart']
    expected = {'bell': [360, 400, 400, 440, 440], 'feedback': [360, 440, 720],
                'mock': [360, 440, 720], 'pulse': [360, 440], 'overlap': [360, 360]}
    assert starts == expected[name], (name, starts)
    if name == 'mock':
        assert summary['memory']['4096'] == 1, summary
    elif name == 'bell':
        assert summary['memory']['4096'] == summary['memory']['4100'], summary
    elif name in ('feedback', 'pulse'):
        assert summary['memory']['4096'] == 1, summary

print(f'PASS {len(args.scenarios)} JSON-configured examples')
