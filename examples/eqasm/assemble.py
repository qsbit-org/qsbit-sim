"""Assemble SMIS, QWAIT, NOP, STOP and quantum bundles into little-endian 32-bit words."""

import argparse
import json
from pathlib import Path
import re
import struct


def field(value, bits):
    if not 0 <= value < (1 << bits):
        raise ValueError(f"{value} does not fit an unsigned {bits}-bit field")
    return value


def smis(register, mask):
    return (0x20 << 25) | (field(register, 5) << 20) | field(mask, 7)


def qwait(interval):
    return (0x30 << 25) | field(interval, 20)


def bundle(interval, *operations):
    if len(operations) > 2:
        raise ValueError("an instruction word holds at most two quantum operations")
    word = 0x80000000 | field(interval, 3)
    for (opcode, register), shift in zip(operations, (17, 3)):
        word |= ((field(opcode, 9) << 5) | field(register, 5)) << shift
    return word


def assemble(source, settings):
    opcodes = {name.upper(): code for name, code in settings["opcodes"].items()}
    arity = {entry["opcode"]: len(entry["targets"]) for entry in settings["eqasm"]["microcode"]}
    result = []
    for line_number, original in enumerate(source.splitlines(), 1):
        text = original.split("#", 1)[0].strip().upper()
        if not text:
            continue
        try:
            if match := re.fullmatch(r"SMIS\s+S(\d+)\s*,\s*\{([\d,\s]*)\}", text):
                qubits = [int(q) for q in match[2].split(",") if q.strip()]
                mask = 0
                for qubit in qubits:
                    if qubit > 6 or mask & (1 << qubit):
                        raise ValueError("invalid or duplicate SMIS target")
                    mask |= 1 << qubit
                word = smis(int(match[1]), mask)
            elif match := re.fullmatch(r"QWAIT\s+(0X[\dA-F]+|\d+)", text):
                word = qwait(int(match[1], 0))
            elif text in ("STOP", "NOP"):
                word = 0x10000000 if text == "STOP" else 0
            else:
                interval = 1
                if match := re.match(r"(\d+)\s*,\s*(.+)", text):
                    interval, text = int(match[1]), match[2]
                operations = []
                names = [operation.strip() for operation in text.split("|")]
                for operation in names:
                    if operation == "QNOP":
                        operations.append((0, 0))
                        continue
                    match = re.fullmatch(r"(\w+)\s+([ST])(\d+)", operation)
                    if not match or match[1] not in opcodes:
                        raise ValueError(f"unsupported instruction: {operation}")
                    opcode = opcodes[match[1]]
                    if (match[2] == "T") != (arity[opcode] == 2):
                        raise ValueError("quantum opcode and target register types differ")
                    operations.append((opcode, int(match[3])))
                field(interval, 3)
                for offset in range(0, len(operations), 2):
                    pi = interval if offset == 0 else 0
                    result.append((bundle(pi, *operations[offset:offset + 2]),
                                   f"{pi}, {' | '.join(names[offset:offset + 2])}"))
                continue
            result.append((word, original.strip()))
        except (KeyError, ValueError) as error:
            raise ValueError(f"line {line_number}: {error}") from error
    if not result:
        raise ValueError("empty eQASM program")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--settings", type=Path, default=Path(__file__).with_name("experiment.json"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    words = assemble(args.source.read_text(), json.loads(args.settings.read_text()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(struct.pack(f"<{len(words)}I", *(word for word, _ in words)))
