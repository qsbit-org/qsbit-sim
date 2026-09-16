"""Compare every retired RV32I state with Unicorn, including memory and branches."""

import argparse
import json
from pathlib import Path
import random
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_X0, UC_RISCV_REG_PC


def run_case(args, seed):
    rng = random.Random(seed)
    root = args.output / str(seed)
    root.mkdir(parents=True, exist_ok=True)
    lines = ['.option norvc', '.option norelax', '.section .text', '.global _start', '_start:']
    for register in range(1, 31):
        lines.append(f'li x{register}, {rng.getrandbits(32)}')
    lines.append('li x31, 0x3000')
    for index in range(160):
        rd, a, b = (rng.randrange(31) for _ in range(3))
        choice = rng.randrange(7)
        if choice == 0:
            op = rng.choice(['add', 'sub', 'and', 'or', 'xor', 'slt', 'sltu', 'sll', 'srl', 'sra'])
            lines.append(f'{op} x{rd}, x{a}, x{b}')
        elif choice == 1:
            op = rng.choice(['addi', 'andi', 'ori', 'xori', 'slti', 'sltiu'])
            lines.append(f'{op} x{rd}, x{a}, {rng.randrange(-2048, 2048)}')
        elif choice == 2:
            op = rng.choice(['slli', 'srli', 'srai'])
            lines.append(f'{op} x{rd}, x{a}, {rng.randrange(32)}')
        elif choice == 3:
            op = rng.choice(['lb', 'lbu', 'lh', 'lhu', 'lw', 'sb', 'sh', 'sw'])
            width = 4 if op.endswith('w') else 2 if 'h' in op else 1
            offset = rng.randrange(64 // width) * width
            lines.append(f'{op} x{rd}, {offset}(x31)')
        elif choice == 4:
            op = rng.choice(['beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu'])
            lines += [f'{op} x{a}, x{b}, next_{index}', f'addi x{rd}, x{rd}, 1', f'next_{index}:']
        elif choice == 5:
            op = rng.choice(['lui', 'auipc'])
            lines.append(f'{op} x{rd}, {rng.randrange(1 << 20)}')
        else:
            lines += [f'jal x{rd}, jump_{index}', f'addi x{a}, x{a}, -1', f'jump_{index}:', 'fence rw,rw']
    lines += ['.global finish', 'finish:', '.word 0x0000400b']
    source = root / 'program.S'
    source.write_text('\n'.join(lines) + '\n')
    subprocess.run([str(args.assembler), '-march=rv32i', '-mabi=ilp32', '-mno-relax',
                    '-o', str(root / 'program.o'), str(source)], check=True, capture_output=True)
    subprocess.run([str(args.linker), '-m', 'elf32lriscv', '--no-relax', '-T', str(args.script),
                    '-o', str(root / 'program.elf'), str(root / 'program.o')], check=True, capture_output=True)
    subprocess.run([str(args.simulator), '--program', str(root / 'program.elf'),
                    '--trace', str(root / 'trace.jsonl'), '--summary', str(root / 'summary.json'),
                    *[arg for address in range(0x3000, 0x3040, 4) for arg in ['--inspect', str(address)]]],
                   check=True, capture_output=True, timeout=15)
    emulator = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
    emulator.mem_map(0, 65536)
    with (root / 'program.elf').open('rb') as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                emulator.mem_write(segment['p_vaddr'], segment.data())
        entry = elf.header['e_entry']
    emulator.reg_write(UC_RISCV_REG_PC, entry)
    retired = 0
    for line in (root / 'trace.jsonl').read_text().splitlines():
        event = json.loads(line)
        if event['kind'] != 'InstructionRetired' or event['word'] == 0x0000400B:
            continue
        actual_pc = emulator.reg_read(UC_RISCV_REG_PC)
        assert actual_pc == event['pc'], (seed, retired, 'PC before', actual_pc, event)
        emulator.emu_start(actual_pc, 0xFFFFFFFF, count=1)
        expected = [emulator.reg_read(UC_RISCV_REG_X0 + register) for register in range(32)]
        assert event['registers'] == expected, (seed, retired, 'register mismatch', event, expected)
        assert event['next_pc'] == emulator.reg_read(UC_RISCV_REG_PC), (seed, retired, 'next PC', event)
        retired += 1
    result = json.loads((root / 'summary.json').read_text())
    assert result['registers'] == expected and result['success']
    for address in range(0x3000, 0x3040, 4):
        assert result['memory'][str(address)] == int.from_bytes(emulator.mem_read(address, 4), 'little')
    return retired


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for option in ['assembler', 'linker', 'script', 'simulator', 'output']:
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--cases', type=int, default=32)
    args = parser.parse_args()
    count = 0
    for seed in range(1024, 1024 + args.cases):
        try:
            count += run_case(args, seed)
        except Exception:
            print(f'FAILED seed={seed}; reproduction files: {args.output / str(seed)}', flush=True)
            raise
    print(f'PASS: {args.cases} seeds; {count} retired RV32I states match Unicorn 2.1.4')
