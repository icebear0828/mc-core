#!/usr/bin/env python3
"""
Disassemble all 8 occurrences of 0x1DED8 and sample occurrences of 0x10EE0 in eldenring.exe.
"""

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        
        hits_1ded8 = [
            0x0050583B,
            0x00508A0C,
            0x00508BF9,
            0x00508C30,
            0x0050BE89,
            0x0050BF7E,
            0x0050E40D,
            0x0050EF8F,
        ]
        
        print("=== Disassembly of 0x1DED8 hits ===")
        for rva in hits_1ded8:
            print(f"\n--- Around RVA 0x{rva:08X} (VA 0x{IMAGE_BASE + rva:016X}) ---")
            # Disassemble from rva - 32 to rva + 48
            start = rva - 32
            length = 80
            code = read_bytes(f, sections, start, length)
            if code:
                for ins in md.disasm(code, IMAGE_BASE + start):
                    prefix = "==>" if ins.address == IMAGE_BASE + rva else "   "
                    print(f"{prefix} 0x{ins.address:016X}: {ins.mnemonic:8s} {ins.op_str}")

if __name__ == "__main__":
    main()
