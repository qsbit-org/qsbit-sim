"""Fresh-process pipeline, reset, crossing, and CLI contract regressions."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser()
for key in ['simulator', 'assembler', 'linker', 'objdump', 'source', 'output']:
    p.add_argument('--' + key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
count = 0


def compile_case(name, body):
    root = a.output / name
    root.mkdir(exist_ok=True)
    (root / 'program.S').write_text('.include "quantum.inc"\n.global _start\n.text\n_start:\n' + body + '\n')
    subprocess.run([str(a.assembler), '-march=rv32i', '-mabi=ilp32', '-mno-relax', '-I', str(a.source / 'examples'),
                    '-o', str(root / 'program.o'), str(root / 'program.S')], check=True, capture_output=True)
    subprocess.run([str(a.linker), '-m', 'elf32lriscv', '--no-relax', '-T', str(a.source / 'examples/link.ld'),
                    '-o', str(root / 'program.elf'), str(root / 'program.o')], check=True, capture_output=True)
    disassembly = subprocess.check_output([str(a.objdump), '-d', str(root / 'program.elf')], text=True)
    (root / 'disassembly.txt').write_text(disassembly)
    return root


def run(root, name='normal', profile=None, args=(), fault=None):
    global count
    config = root / (name + '.profile.json')
    config.write_text(json.dumps(profile or {}))
    summary, trace = root / (name + '.json'), root / (name + '.jsonl')
    cmd = [str(a.simulator), '--program', str(root / 'program.elf'), '--profile', str(config),
           '--trace', str(trace), '--summary', str(summary), '--inspect', '4096', *args]
    result = subprocess.run(cmd, text=True, capture_output=True, timeout=20)
    assert result.returncode == (1 if fault else 0), (name, result.stdout, result.stderr)
    state = json.loads(summary.read_text())
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    assert state['success'] == (fault is None), (name, state)
    assert events[-1]['kind'] == ('Fault' if fault else 'SimulationCompleted')
    if fault:
        assert state['fault'] == fault, state
    count += 1
    return state, events


def kinds(events, kind):
    return [e for e in events if e['kind'] == kind]


hazards = compile_case('hazards', '''li t0, 4096
li t1, -1
sw t1, 0(t0)
lbu t2, 0(t0)
add t3, t2, t2
lh t4, 0(t0)
add t5, t4, t3
beq t1, t4, taken
qappend x0, x0, x0
.word 0xffffffff
taken:
sw t5, 0(t0)
qend''')
for latency in [1, 2, 7]:
    s, e = run(hazards, f'latency{latency}', {'memory_latency': latency})
    assert s['memory']['4096'] == 509 and not kinds(e, 'ProducerAccepted')
    assert kinds(e, 'PipelineFlushed') and kinds(e, 'CpuStalled')
    retired = kinds(e, 'InstructionRetired')
    assert len({r['id'] for r in retired}) == len(retired)
    reverse, re = run(hazards, f'reverse{latency}', {'memory_latency': latency}, ['--reverse-registration'])
    assert e == re and s == reverse
for tick in [1, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 75, 100]:
    s, e = run(hazards, f'reset{tick}', args=['--reset', str(tick)])
    assert s['memory']['4096'] == 509
    assert len(kinds(e, 'SessionReset')) == 1
    after = [x for x in e if x['tick'] >= tick]
    assert all(x['epoch'] == 2 for x in after)

older_fault = compile_case('older_fault', 'li t0, 1\nlw t1, 0(t0)\nqappend x0, x0, x0\nqend')
s, e = run(older_fault, fault='LoadMisaligned')
assert not kinds(e, 'ProducerAccepted')

unsupported = compile_case('unsupported', '.insn r 0x0b, 6, 0, x0, x0, x0\nqend')
run(unsupported, fault='UnsupportedSynchronization')
illegal = compile_case('illegal', '.word 0x02000033\nqend')
run(illegal, fault='IllegalInstruction')

same_group = compile_case('same_group', '''li t0, 8
qadvance t0
li t1, 1
qappend x0, x0, t1
qadvance x0
qappend x0, t1, t1
qend''')
s, e = run(same_group)
starts = kinds(e, 'OperationStart')
assert len(starts) == 2 and {x['tick'] for x in starts} == {1160}
assert {tuple(x['targets']) for x in starts} == {(0,), (1,)}
for tick in [1160, 1170, 1180]:
    s, e = run(same_group, f'reset{tick}', args=['--reset', str(tick)])
    reset = kinds(e, 'SessionReset')[0]
    starts = [x for x in kinds(e, 'OperationStart') if x['epoch'] == reset['epoch']]
    origin = ((tick + 1000 + 19) // 20) * 20
    assert len(starts) == 2 and {x['tick'] for x in starts} == {origin + 160}
    assert not [x for x in e if x['tick'] == tick and x['kind'] == 'OperationStart']

flushed = compile_case('flush', 'li t0, 8\nqadvance t0\nli t1, 1\nqappend x0, x0, t1\nqflush\nqadvance x0\nli t0, 2\nqadvance t0\nqappend x0, t1, t1\nqend')
s, e = run(flushed)
assert [x['tick'] for x in kinds(e, 'OperationStart')] == [1160, 1200]
invalid_flush = compile_case('append_after_flush', 'li t0, 8\nqadvance t0\nli t1, 1\nqappend x0, x0, t1\nqflush\nqappend x0, t1, t1\nqend')
run(invalid_flush, fault='Protocol')

for cpu, tcu in [(7, 20), (5, 13), (11, 17)]:
    profile = {'cpu': {'period': cpu, 'phase': 2}, 'tcu': {'period': tcu, 'phase': 3}, 'start': tcu * 100 + 3}
    s, e = run(same_group, f'clocks{cpu}-{tcu}', profile)
    r, re = run(same_group, f'reverse-clocks{cpu}-{tcu}', profile, ['--reverse-registration'])
    assert s == r and e == re
    assert {x['tick'] for x in kinds(e, 'OperationStart')} == {profile['start'] + 8 * tcu}

fast = compile_case('fast', '''li t0, 8
qadvance t0
li t1, 4
qappend s0, x0, t1
li t0, 100
qadvance t0
li t1, 1
qappend_if s0, t1, t1, 1
qend''')
for value in [0, 1]:
    s, e = run(fast, f'outcome{value}', args=['--outcomes', str(value)])
    starts = kinds(e, 'OperationStart')
    assert len(starts) == 1 + value
    assert bool(kinds(e, 'ConditionCancelled')) == (value == 0)
    assert kinds(e, 'ResultReady')[0]['value'] == value
    assert kinds(e, 'FastResultVisible')
    assert kinds(e, 'CpuResultVisible')

false_condition = compile_case('fast_false', (fast / 'program.S').read_text().split('_start:\n', 1)[1].replace('t1, t1, 1', 't1, t1, 0'))
for value in [0, 1]:
    s, e = run(false_condition, f'outcome{value}', args=['--outcomes', str(value)])
    assert len(kinds(e, 'OperationStart')) == 2 - value
    assert bool(kinds(e, 'ConditionCancelled')) == (value == 1)

measurement = compile_case('drain', '''li t0, 8
qadvance t0
li t1, 4
qappend s0, x0, t1
qend''')
s, e = run(measurement, profile={'fast_result_latency': 50})
ready = kinds(e, 'ResultReady')[0]['tick']
assert s['stop_tick'] > ready + 49 * 20
assert kinds(e, 'InstructionRetired')[-1]['tick'] < ready
for tick in [1200, 1220]:
    s, e = run(measurement, f'reset{tick}', args=['--reset', str(tick)])
    assert all(x['epoch'] == 2 for x in kinds(e, 'ResultReady'))
    assert len(kinds(e, 'ResultReady')) == 1

late = compile_case('late', 'li t0, 1\nqadvance t0\nli t1, 1\nqappend x0, x0, t1\nqend')
run(late, profile={'start': 0}, fault='LateAdmission')
loop = compile_case('watchdog', 'j _start')
run(loop, profile={'watchdog': 1200}, fault='Watchdog')

# Raw machine words follow the same decoder; output identity is asserted.
raw = a.output / 'raw.bin'
raw.write_bytes(struct.pack('<II', 0x00700093, 0x0000400b))
r = subprocess.run([str(a.simulator), '--program', str(raw), '--raw-base', '0', '--trace', str(a.output / 'raw.jsonl'),
                    '--summary', str(a.output / 'raw.json')], capture_output=True, text=True)
assert r.returncode == 0, r.stderr
assert json.loads((a.output / 'raw.json').read_text())['registers'][1] == 7
count += 1
run_config = a.output / 'raw-run.json'
run_config.write_text(json.dumps({
    'schema': 1, 'program': 'raw.bin', 'raw_base': 0,
    'trace': 'nested/raw.jsonl', 'summary': 'nested/raw.json', 'inspect': [0],
    'profile': {'seed': 17}
}))
r = subprocess.run([str(a.simulator), '--config', str(run_config)],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stderr
configured = json.loads((a.output / 'nested/raw.json').read_text())
assert configured['registers'][1] == 7 and configured['configuration']['seed'] == 17
assert configured['memory']['0'] == 0x00700093
assert (a.output / 'nested/raw.jsonl').is_file()
count += 1
r = subprocess.run([str(a.simulator), '--config', str(run_config), '--seed', '23'],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stderr
assert json.loads((a.output / 'nested/raw.json').read_text())['configuration']['seed'] == 23
count += 1
for bad in [
    {'schema': 2, 'program': 'raw.bin'},
    {'schema': 1, 'program': 'raw.bin', 'unexpected': 1},
    {'schema': 1, 'program': 'raw.bin', 'inspect': [-1]},
    {'schema': 1, 'program': 'raw.bin', 'outcomes': [1]},
    {'schema': 1, 'program': 'raw.bin', 'profile': {'unexpected': 1}}
]:
    config = a.output / 'bad-run.json'
    config.write_text(json.dumps(bad))
    r = subprocess.run([str(a.simulator), '--config', str(config)],
                       capture_output=True, text=True)
    assert r.returncode == 2 and 'InvalidProfile' in r.stderr, r.stderr
    count += 1
for bad in [{'unexpected': 1}, {'cpu_result_latency': 0}, {'ports': -1}, {'seed': 2**32}, {'start': 1}]:
    config = a.output / 'bad.json'
    config.write_text(json.dumps(bad))
    r = subprocess.run([str(a.simulator), '--program', str(raw), '--raw-base', '0', '--profile', str(config)],
                       capture_output=True, text=True)
    assert r.returncode == 2 and 'InvalidProfile' in r.stderr, r.stderr
    count += 1
print(f'PASS {count} fresh-process use cases')
