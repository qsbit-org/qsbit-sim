"""Run the checked-in JSON examples through the CLI and inspect their outputs."""

import argparse
import json
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--simulator', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()

for name in ('bell', 'feedback', 'pulse', 'overlap'):
    config_path = args.source / 'examples' / 'runs' / f'{name}.json'
    config = json.loads(config_path.read_text())
    result = subprocess.run([str(args.simulator), '--config', str(config_path)],
                            cwd=args.source / 'examples', capture_output=True, text=True,
                            timeout=30)
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    summary_path = (config_path.parent / config['summary']).resolve()
    trace_path = (config_path.parent / config['trace']).resolve()
    summary = json.loads(summary_path.read_text())
    trace = [json.loads(line) for line in trace_path.read_text().splitlines()]
    assert summary['success'] and trace[-1]['kind'] == 'SimulationCompleted', name
    assert summary['backend'] == config['backend'], name
    if name == 'bell':
        assert summary['memory']['4096'] == summary['memory']['4100'], summary
    elif name in ('feedback', 'pulse'):
        assert summary['memory']['4096'] == 1, summary
    else:
        assert len([event for event in trace if event['kind'] == 'OperationStart']) == 2, name

print('PASS four JSON-configured numerical examples')
