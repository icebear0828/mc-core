#!/usr/bin/env python3
"""
Disassemble function around 0x14050BE00 to see what is inserted into [WorldChrMan + 0x1DED8].
"""

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        
        start_rva = 0x0050BE00
        length = 0x150
        code = read_bytes(f, sections, start_rva, length)
        for ins in md.disasm(code, IMAGE_BASE + start_rva):
            print(f"0x{ins.address:016X} (RVA 0x{ins.address - IMAGE_BASE:08X}): {ins.mnemonic:8s} {ins.op_str}")

if __name__ == "__main__":
    main()
