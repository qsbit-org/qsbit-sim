"""Standalone trace replay validation and HTTP smoke test."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from replay_trace import load_trace  # noqa: E402


with tempfile.TemporaryDirectory() as directory:
    trace = Path(directory) / 'run.jsonl'
    trace.write_text('\n'.join(json.dumps({'schema': 1, 'tick': tick, 'kind': kind})
                               for tick, kind in [(0, 'SessionStarted'), (5, 'SimulationCompleted')]))
    bundle = load_trace(trace)
    assert [event['tick'] for event in bundle['examples'][0]['events']] == [0, 5]
    assert bundle['examples'][0]['configuration'] is None
    trace.with_suffix('.json').write_text(json.dumps({'configuration': {'cpu': {'period': 5, 'phase': 0}}}))
    assert load_trace(trace)['examples'][0]['configuration']['cpu']['period'] == 5
    process = subprocess.Popen([sys.executable, str(ROOT / 'tools/replay_trace.py'),
                                str(trace), '--no-browser'], stdout=subprocess.PIPE, text=True)
    try:
        url = process.stdout.readline().strip().split()[-1]
        for endpoint in ('', 'trace.json', 'trace-player.js', 'site.css'):
            with urllib.request.urlopen(url + endpoint, timeout=5) as response:
                assert response.status == 200
        with urllib.request.urlopen(url + 'trace.json', timeout=5) as response:
            assert json.load(response)['examples'][0]['events'] == bundle['examples'][0]['events']
    finally:
        process.terminate()
        process.wait(timeout=5)
    trace.write_text('{"schema":1,"tick":5,"kind":"A"}\n{"schema":1,"tick":4,"kind":"B"}\n')
    try:
        load_trace(trace)
        assert False, 'decreasing timestamps accepted'
    except ValueError as exc:
        assert 'line 2' in str(exc)
