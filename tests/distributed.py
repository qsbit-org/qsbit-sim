"""Exercise multicore execution, measurement routing, reset and shared quantum state."""

import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--simulator", type=Path, required=True)
parser.add_argument("--assembler", required=True)
parser.add_argument("--linker", required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--backend", default="mock")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
common = Path(__file__).resolve().parents[1] / "examples/common"


def compile_program(name, source):
    path = args.output / f"{name}.S"
    path.write_text('.include "quantum.inc"\n.global _start\n.text\n_start:\n' + source + '\nsim_exit\n')
    obj, elf = path.with_suffix('.o'), path.with_suffix('.elf')
    subprocess.run([args.assembler, '-march=rv32i', '-mabi=ilp32', '-mno-relax',
                    '-I', str(common), '-o', str(obj), str(path)], check=True)
    subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(common / 'link.ld'),
                    '-o', str(elf), str(obj)], check=True)
    return str(elf.resolve())


def run(name, config, fault=None, invalid=False):
    config = dict(config, schema=1, trace=f'{name}.jsonl', summary=f'{name}.json')
    path = args.output / f'{name}-run.json'
    path.write_text(json.dumps(config))
    result = subprocess.run([str(args.simulator), '--config', str(path)],
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == (2 if invalid else 1 if fault else 0), result.stdout + result.stderr
    if invalid:
        assert fault in result.stderr, result.stderr
        return None, None
    summary = json.loads((args.output / f'{name}.json').read_text())
    assert summary['success'] == (fault is None), summary
    if fault:
        assert summary['fault'] == fault, summary
    trace = [json.loads(line) for line in (args.output / f'{name}.jsonl').read_text().splitlines()]
    return summary, trace


first = compile_program('first', 'wait.i 10\nsync 2\nwait.i 8\ncw.i.i 0, 1\nwait.i 20\nfmr a1, 0\nwait.i 100')
second = compile_program('second', 'wait.i 40\nsync 1\nwait.i 6\ncw.i.i 0, 1\nwait.i 20\nfmr a1, 1')
profile = {'cpu': {'period': 5, 'phase': 0}, 'tcu': {'period': 4, 'phase': 0},
           'start': 1000, 'watchdog': 6000, 'qubits': 2, 'ports': 1, 'mappings': []}


def mapping(target):
    return [{'port': 0, 'codeword': 1, 'actions': [
        {'kind': 'acquire', 'operation': 'measure', 'targets': [target], 'duration': 8,
         'discriminator_delay': 4}]}]


config = {'backend': 'mock', 'outcomes': [True, False], 'profile': profile,
          'cores': [{'id': 1, 'program': first, 'profile': {'mappings': mapping(0)}},
                    {'id': 2, 'program': second, 'cpu_model': 'vliw', 'profile': {'mappings': mapping(1)}}],
          'sync_connections': [{'first': 1, 'second': 2, 'first_to_second': 8, 'second_to_first': 6}]}
if args.backend != 'mock':
    first = compile_program('prepare', 'wait.i 5\ncw.i.i 0, 2\nwait.i 5\nsync 2\nwait.i 8\nwait.i 100')
    second = compile_program('measure', 'wait.i 40\nsync 1\nwait.i 6\ncw.i.i 0, 1\nwait.i 20\nfmr a1, 0')
    config['backend'] = args.backend
    config.pop('outcomes')
    config['cores'][0]['program'] = first
    config['cores'][0]['profile']['mappings'] = [{'port': 0, 'codeword': 2, 'actions': [
        {'kind': 'gate', 'operation': 'x', 'targets': [0], 'duration': 4}]}]
    config['cores'][1]['program'] = second
    config['cores'][1]['profile']['mappings'] = mapping(0)

for reset in (False, True):
    runs = []
    for reverse in (False, True):
        name = f'{args.backend}-{reset}-{reverse}'
        settings = dict(config, reverse_registration=reverse, resets=[1076] if reset else [])
        summary, trace = run(name, settings)
        expected = 2260 if reset else 1184
        final = [e for e in trace if e['epoch'] == (2 if reset else 1)]
        starts = [e['tick'] for e in final if e['kind'] == 'OperationStart' and e['operation'] == 'measure']
        assert starts == [expected] * (2 if args.backend == 'mock' else 1), starts
        assert all(c['drained'] for c in summary['cores'])
        if args.backend == 'mock':
            assert [c['registers'][11] for c in summary['cores']] == [1, 0]
        else:
            assert summary['cores'][1]['registers'][11] == 1
        assert summary['stop_tick'] > expected + 300
        selected = {(e['tick'], e.get('core'), e['kind'], e['id'], e['value']) for e in final
                    if e['kind'] in {'OperationStart', 'ResultReady', 'SyncCompleted', 'TimerPaused', 'TimerResumed'}}
        runs.append((summary, selected))
    assert runs[0] == runs[1]

if args.backend == 'aer':
    thermal = deepcopy(config)
    thermal['cores'][1]['program'] = compile_program(
        'thermal-peer', 'wait.i 40\nsync 1\nwait.i 6\ncw.i.i 0, 2\nwait.i 200')
    thermal['cores'][1]['profile']['mappings'] = [{'port': 0, 'codeword': 2, 'actions': [
        {'kind': 'gate', 'operation': 'z', 'targets': [1], 'duration': 4}]}]
    thermal['backend_options'] = {'method': 'density_matrix', 'noise': {
        'model': 'thermal_relaxation', 'qubits': [
            {'qubit': 0, 't1_ns': 1000, 't2_ns': 2000, 'excited_state_population': 0}]}}
    summary, trace = run('paused-relaxation', thermal)
    assert [(e['kind'], e['tick']) for e in trace if e['kind'] in {'TimerPaused', 'TimerResumed'}] == [
        ('TimerPaused', 1072), ('TimerResumed', 1184)]
    assert summary['stop_tick'] == 1984
    density = summary['density_matrix']
    population = sum(density[i][i][0] for i in (1, 3))
    assert abs(population - math.exp(-(1984 - 1020) / 1000)) < 1e-10, population

if args.backend == 'mock':
    extended = deepcopy(config)
    extended['cores'].append({'id': 17, 'program': compile_program('third', 'wait.i 400'),
                              'profile': {'cpu': {'period': 7, 'phase': 1}, 'mappings': []}})
    summary, _ = run('third-core', extended)
    assert summary['stop_tick'] == 2600 and len(summary['cores']) == 3
    assert all(c['drained'] for c in summary['cores'])
    unmatched = deepcopy(config)
    unmatched['cores'][1]['program'] = compile_program('no-sync', 'wait.i 1')
    summary, trace = run('missing-peer', unmatched, fault='Watchdog')
    assert any(e['kind'] == 'TimerPaused' for e in trace)
    invalid = deepcopy(config)
    invalid['sync_connections'][0]['second'] = 3
    run('unknown-core', invalid, fault='InvalidProfile', invalid=True)
    invalid = deepcopy(config)
    invalid['cores'][1]['profile']['tcu'] = {'period': 8, 'phase': 0}
    run('clock-mismatch', invalid, fault='InvalidProfile', invalid=True)
    conflict = deepcopy(config)
    conflict['cores'][1]['profile']['mappings'] = mapping(0)
    run('shared-qubit-conflict', conflict, fault='ResourceConflict')
