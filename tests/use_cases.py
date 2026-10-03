"""Fresh-process pipeline, reset, crossing, and CLI contract regressions."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser()
for key in ['simulator', 'assembler', 'linker', 'objdump', 'source', 'output']:
    p.add_argument('--' + key, type=Path, required=True)
p.add_argument('--cpu-model', choices=['rv32', 'vliw'], default='rv32')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
count = 0


def compile_case(name, body):
    root = a.output / name
    root.mkdir(exist_ok=True)
    (root / 'program.S').write_text('.include "quantum.inc"\n.global _start\n.text\n_start:\n' + body + '\n')
    subprocess.run([str(a.assembler), '-march=rv32i', '-mabi=ilp32', '-mno-relax', '-I', str(a.source / 'examples/common'),
                    '-o', str(root / 'program.o'), str(root / 'program.S')], check=True, capture_output=True)
    subprocess.run([str(a.linker), '-m', 'elf32lriscv', '--no-relax', '-T', str(a.source / 'examples/common/link.ld'),
                    '-o', str(root / 'program.elf'), str(root / 'program.o')], check=True, capture_output=True)
    disassembly = subprocess.check_output([str(a.objdump), '-d', str(root / 'program.elf')], text=True)
    (root / 'disassembly.txt').write_text(disassembly)
    return root


def run(root, name='normal', profile=None, args=(), fault=None):
    global count
    config = root / (name + '.profile.json')
    config.write_text(json.dumps(profile or {}))
    summary, trace = root / (name + '.json'), root / (name + '.jsonl')
    cmd = [str(a.simulator), '--cpu-model', a.cpu_model,
           '--program', str(root / 'program.elf'), '--profile', str(config),
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
cw.r.r x0, x0
.word 0xffffffff
taken:
sw t5, 0(t0)
sim_exit''')
for latency in [1, 2, 7]:
    s, e = run(hazards, f'latency{latency}', {'memory_latency': latency})
    assert s['memory']['4096'] == 509 and not kinds(e, 'CodewordQueued')
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

older_fault = compile_case('older_fault', 'li t0, 1\nlw t1, 0(t0)\ncw.r.r x0, x0\nsim_exit')
s, e = run(older_fault, fault='LoadMisaligned')
assert not kinds(e, 'CodewordQueued')

unsupported = compile_case('unsupported', '.insn r 0x0b, 6, 0, x0, x0, x0\nsim_exit')
run(unsupported, fault='UnsupportedSynchronization')
illegal = compile_case('illegal', '.word 0x02000033\nsim_exit')
run(illegal, fault='IllegalInstruction')

same_group = compile_case('same_group', '''li t0, 8
wait.r t0
li t1, 1
cw.r.r x0, t1
wait.r x0
cw.r.r t1, t1
sim_exit''')
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

flushed = compile_case('read_then_wait', 'wait.i 8\ncw.i.i 0, 1\nfmr t2, 1\nwait.i 2\ncw.i.i 1, 1\nsim_exit')
s, e = run(flushed)
assert [x['tick'] for x in kinds(e, 'OperationStart')] == [1160, 1200]
invalid_flush = compile_case('cw_after_fmr', 'wait.i 8\ncw.i.i 0, 1\nfmr t2, 1\ncw.i.i 1, 1\nsim_exit')
run(invalid_flush, fault='Protocol')

for cpu, tcu in [(7, 20), (5, 13), (11, 17)]:
    profile = {'cpu': {'period': cpu, 'phase': 2}, 'tcu': {'period': tcu, 'phase': 3}, 'start': tcu * 100 + 3}
    s, e = run(same_group, f'clocks{cpu}-{tcu}', profile)
    r, re = run(same_group, f'reverse-clocks{cpu}-{tcu}', profile, ['--reverse-registration'])
    assert s == r and e == re
    assert {x['tick'] for x in kinds(e, 'OperationStart')} == {profile['start'] + 8 * tcu}

fast = compile_case('fast', '''li t0, 8
wait.r t0
li t1, 4
cw.r.r x0, t1
li t0, 100
wait.r t0
cw.i.i 0, 7
sim_exit''')
for value in [0, 1]:
    s, e = run(fast, f'outcome{value}', args=['--outcomes', str(value)])
    starts = kinds(e, 'OperationStart')
    assert len(starts) == 1 + value
    assert bool(kinds(e, 'ConditionCancelled')) == (value == 0)
    assert kinds(e, 'ResultReady')[0]['value'] == value
    assert kinds(e, 'ExecutionFlagsUpdated')
    assert kinds(e, 'MeasurementRegisterUpdated')

false_condition = compile_case('fast_false', (fast / 'program.S').read_text().split('_start:\n', 1)[1].replace('cw.i.i 0, 7', 'cw.i.i 0, 8'))
for value in [0, 1]:
    s, e = run(false_condition, f'outcome{value}', args=['--outcomes', str(value)])
    assert len(kinds(e, 'OperationStart')) == 2 - value
    assert bool(kinds(e, 'ConditionCancelled')) == (value == 1)

latest = compile_case('latest_measurement', '''wait.i 8
cw.i.i 0, 4
wait.i 10
cw.i.i 0, 4
wait.i 10
cw.i.i 0, 7
fmr s0, 0
fmr s1, 0
li t0, 4096
sw s0, 0(t0)
sim_exit''')
for outcomes in ['1,0', '0,1']:
    value = int(outcomes[-1])
    s, e = run(latest, outcomes, args=['--outcomes', outcomes])
    assert s['memory']['4096'] == value and s['registers'][9] == value
    assert len(kinds(e, 'MeasurementRegisterRead')) == 2
    assert len(kinds(e, 'OperationStart')) == 2 + value
    assert s['measurement_registers'][0] == {'pending': 0, 'valid': True, 'value': bool(value)}
    assert all(r['tick'] >= kinds(e, 'MeasurementRegisterUpdated')[-1]['tick']
               for r in kinds(e, 'MeasurementRegisterRead'))

equal = compile_case('equal_flag', (latest / 'program.S').read_text().split('_start:\n', 1)[1]
                     .replace('cw.i.i 0, 7', 'cw.i.i 0, 9'))
for outcomes in ['0,0', '1,1', '0,1', '1,0']:
    _, e = run(equal, outcomes, args=['--outcomes', outcomes])
    assert len(kinds(e, 'OperationStart')) == 2 + (outcomes[0] == outcomes[-1])

read_reset = compile_case('read_reset', 'fmr s0, 0\nfmr s1, 1\nsim_exit')
s, _ = run(read_reset)
assert s['registers'][8:10] == [0, 0]
invalid_register = compile_case('invalid_register', 'fmr s0, 2\nsim_exit')
run(invalid_register, fault='InvalidOperand')
run(fast, 'disabled', {'fast_feedback': False}, fault='UnsupportedCapability')

reuse = compile_case('delivery_capacity_reuse', '''wait.i 8
cw.i.i 0, 4
fmr s0, 0
wait.i 100
cw.i.i 0, 4
fmr s1, 0
sim_exit''')
s, e = run(reuse, profile={'result_capacity': 1, 'fast_feedback': False},
           args=['--outcomes', '1,0'])
assert s['registers'][8:10] == [1, 0]
assert len(kinds(e, 'MeasurementRegisterUpdated')) == 2

for outcomes in ['1,0', '0,1']:
    s, e = run(latest, 'slow-cpu-' + outcomes, {'cpu_result_latency': 100},
               args=['--outcomes', outcomes])
    assert len(kinds(e, 'OperationStart')) == 2 + int(outcomes[-1])
    assert kinds(e, 'MeasurementRegisterRead')[0]['tick'] > 1560

for field in (['last_one', 'last_zero', 'equal'] if a.cpu_model == 'rv32' else []):
    mapping = {'port': 0, 'codeword': 1, 'actions': [
        {'kind': 'acquire', 'operation': 'measure', 'targets': [0], 'execution_flag': field}]}
    config = a.output / f'invalid-{field}.json'
    config.write_text(json.dumps({'mappings': [mapping]}))
    result = subprocess.run([str(a.simulator), '--program', str(fast / 'program.elf'),
                             '--profile', str(config)], capture_output=True, text=True)
    assert result.returncode == 2 and 'InvalidProfile' in result.stderr

measurement = compile_case('drain', '''li t0, 8
wait.r t0
li t1, 4
cw.r.r x0, t1
sim_exit''')
s, e = run(measurement, profile={'fast_result_latency': 50})
ready = kinds(e, 'ResultReady')[0]['tick']
assert s['stop_tick'] > ready + 49 * 20
assert kinds(e, 'InstructionRetired')[-1]['tick'] < ready
for tick in [1200, 1220]:
    s, e = run(measurement, f'reset{tick}', args=['--reset', str(tick)])
    assert all(x['epoch'] == 2 for x in kinds(e, 'ResultReady'))
    assert len(kinds(e, 'ResultReady')) == 1

late = compile_case('late', 'li t0, 1\nwait.r t0\nli t1, 1\ncw.r.r x0, t1\nsim_exit')
run(late, profile={'start': 0}, fault='LateAdmission')
loop = compile_case('watchdog', 'j _start')
run(loop, profile={'watchdog': 1200}, fault='Watchdog')

if a.cpu_model == 'vliw':
    print(f'PASS {count} fresh-process use cases')
    raise SystemExit(0)

# Raw machine words follow the same decoder; output identity is asserted.
raw = a.output / 'raw.bin'
raw.write_bytes(struct.pack('<IIII', 0x00700093, 0x00000513, 0x05d00893, 0x00000073))
r = subprocess.run([str(a.simulator), '--program', str(raw), '--raw-base', '0', '--trace', str(a.output / 'raw.jsonl'),
                    '--summary', str(a.output / 'raw.json')], capture_output=True, text=True)
assert r.returncode == 0, r.stderr
assert json.loads((a.output / 'raw.json').read_text())['registers'][1] == 7
count += 1
missing = a.output / 'missing.elf'
r = subprocess.run([str(a.simulator), '--program', str(missing)],
                   capture_output=True, text=True)
assert r.returncode == 2 and f'InvalidImage: cannot read program: {missing}' in r.stderr, r.stderr
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
