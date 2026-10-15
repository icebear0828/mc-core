#!/usr/bin/env python3
"""
Print instructions between 0x00504CD0 and 0x00505200.
"""
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        start_rva = 0x00504CD0
        length = 0x400
        code = read_bytes(f, sections, start_rva, length)
        for ins in md.disasm(code, IMAGE_BASE + start_rva):
            print(f"0x{ins.address:016X} (RVA 0x{ins.address - IMAGE_BASE:08X}): {ins.mnemonic:8s} {ins.op_str}")

if __name__ == "__main__":
    main()
