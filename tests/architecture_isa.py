"""Run unmodified, pinned RV32I architecture-test bodies with a bare-metal adapter."""

import argparse
import json
from pathlib import Path
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_X0, UC_RISCV_REG_PC

REVISION = '37e6e0022814d880375e4a310b4a9a10fb9b268a'
EXCLUDED = {'misalign1-jalr-01.S': 'Requires a privileged trap handler; v1 instead tests a typed instruction-alignment fault.'}


def run_case(args, source):
    root = args.output / source.stem
    root.mkdir(parents=True, exist_ok=True)
    env = args.suite / 'riscv-test-suite/env'
    assembly = root / 'test.s'
    with assembly.open('w') as out:
        subprocess.run(['cpp', '-P', '-nostdinc', '-undef', '-DXLEN=32', '-DFLEN=0', '-DTEST_CASE_1',
                        '-I', str(args.adapter), '-I', str(env), str(source)], stdout=out, check=True)
    subprocess.run([str(args.assembler), '-march=rv32i', '-mabi=ilp32', '-mno-relax',
                    '-o', str(root / 'test.o'), str(assembly)], check=True, capture_output=True)
    subprocess.run([str(args.linker), '-m', 'elf32lriscv', '--no-relax', '-T', str(args.script),
                    '-o', str(root / 'test.elf'), str(root / 'test.o')], check=True, capture_output=True)
    size = 4 * 1024 * 1024
    with (root / 'test.elf').open('rb') as stream:
        elf = ELFFile(stream)
        segments = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        entry = elf.header['e_entry']
        assert elf.header['e_flags'] == 0, 'unexpected extension flags'
    config = root / 'profile.json'
    config.write_text(json.dumps({'watchdog': 10000000}))
    subprocess.run([str(args.simulator), '--program', str(root / 'test.elf'), '--memory-size', str(size),
                    '--profile', str(config), '--trace', str(root / 'trace.jsonl'),
                    '--summary', str(root / 'summary.json'), '--memory-dump', str(root / 'memory.bin')],
                   check=True, capture_output=True, timeout=60)
    emulator = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
    emulator.mem_map(0, size)
    for address, data in segments:
        emulator.mem_write(address, data)
    emulator.reg_write(UC_RISCV_REG_PC, entry)
    count = 0
    with (root / 'trace.jsonl').open() as stream:
        for line in stream:
            event = json.loads(line)
            if event['kind'] != 'InstructionRetired' or event['word'] == 0x0000400B:
                continue
            assert emulator.reg_read(UC_RISCV_REG_PC) == event['pc'], (source.name, count, 'PC', event)
            emulator.emu_start(event['pc'], 0xFFFFFFFF, count=1)
            expected = [emulator.reg_read(UC_RISCV_REG_X0 + r) for r in range(32)]
            assert event['registers'] == expected, (source.name, count, 'registers', event, expected)
            assert event['next_pc'] == emulator.reg_read(UC_RISCV_REG_PC), (source.name, count, 'next PC')
            count += 1
    assert emulator.reg_read(UC_RISCV_REG_PC) == symbols['qsbit_test_halt']
    start, end = symbols['begin_signature'], symbols['end_signature']
    assert end > start, 'empty signature is not a passing test'
    with (root / 'memory.bin').open('rb') as stream:
        stream.seek(start)
        actual = stream.read(end - start)
    assert actual == bytes(emulator.mem_read(start, end - start)), (source.name, 'signature mismatch')
    return {'source': source.name, 'retirements': count, 'signature_bytes': end - start}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for option in ['suite', 'assembler', 'linker', 'script', 'simulator', 'adapter', 'output']:
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--filter', default='')
    args = parser.parse_args()
    actual = subprocess.check_output(['git', '-C', str(args.suite), 'rev-parse', 'HEAD'], text=True).strip()
    assert actual == REVISION, ('unpinned suite', actual)
    cases = []
    for source in sorted((args.suite / 'riscv-test-suite/rv32i_m/I/src').glob('*.S')):
        if source.name in EXCLUDED or args.filter not in source.name:
            continue
        try:
            cases.append(run_case(args, source))
            print(f'PASS {source.name}: {cases[-1]["retirements"]} retirements', flush=True)
        except subprocess.CalledProcessError as error:
            print(error.stderr.decode() if isinstance(error.stderr, bytes) else error.stderr, flush=True)
            raise
    assert cases
    (args.output / 'report.json').write_text(json.dumps({'revision': REVISION, 'passed': cases,
        'excluded': EXCLUDED, 'reference': 'Unicorn 2.1.4',
        'scope': 'Unprivileged RV32I bodies and signatures; this is not an official certification result.'}, indent=2))
