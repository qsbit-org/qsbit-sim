"""Standalone trace replay validation and HTTP smoke test."""

import json
from http.server import BaseHTTPRequestHandler
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
from unittest.mock import patch
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from replay_trace import ReplayHTTPServer, load_trace  # noqa: E402


with patch('socket.getfqdn', side_effect=OSError('name resolution unavailable')):
    with ReplayHTTPServer(('127.0.0.1', 0), BaseHTTPRequestHandler) as server:
        assert server.server_name == '127.0.0.1'
        assert server.server_port > 0


with tempfile.TemporaryDirectory() as directory:
    trace = Path(directory) / 'run.jsonl'
    trace.write_text('\n'.join(json.dumps({'schema': 1, 'tick': tick, 'kind': kind})
                               for tick, kind in [(0, 'SessionStarted'), (5, 'SimulationCompleted')]))
    bundle = load_trace(trace)
    assert [event['tick'] for event in bundle['examples'][0]['events']] == [0, 5]
    assert bundle['examples'][0]['configuration'] is None
    trace.with_suffix('.json').write_text(json.dumps({'configuration': {'cpu': {'period': 5, 'phase': 0}}}))
    assert load_trace(trace)['examples'][0]['configuration']['cpu']['period'] == 5
    summary = trace.with_suffix('.summary.json')
    summary.write_text(json.dumps({'cores': [{'id': 3, 'configuration': {'cpu': {'period': 7}}}]}))
    assert load_trace(trace)['examples'][0]['cores'][0]['id'] == 3
    assert load_trace(trace, trace.with_suffix('.json'))['examples'][0]['configuration']['cpu']['period'] == 5
    process = subprocess.Popen([sys.executable, str(ROOT / 'tools/replay_trace.py'),
                                str(trace), '--no-browser'], stdout=subprocess.PIPE, text=True)
    startup = queue.Queue()
    reader = threading.Thread(target=lambda: startup.put(process.stdout.readline()), daemon=True)
    try:
        reader.start()
        try:
            line = startup.get(timeout=10)
        except queue.Empty:
            raise AssertionError('trace replay did not start within 10 seconds') from None
        assert line.startswith('Trace replay: '), f'trace replay startup failed: {line!r}'
        url = line.strip().split()[-1]
        for endpoint in ('', 'trace-model.js', 'trace-player.js', 'site.css'):
            with urllib.request.urlopen(url + endpoint, timeout=5) as response:
                assert response.status == 200
        with urllib.request.urlopen(url + 'trace.json', timeout=5) as response:
            assert response.status == 200
            assert json.load(response)['examples'][0]['events'] == bundle['examples'][0]['events']
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        reader.join(timeout=1)
        process.stdout.close()
    trace.write_text('{"schema":1,"tick":5,"kind":"A"}\n{"schema":1,"tick":4,"kind":"B"}\n')
    try:
        load_trace(trace)
        assert False, 'decreasing timestamps accepted'
    except ValueError as exc:
        assert 'line 2' in str(exc)
