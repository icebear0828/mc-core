#!/usr/bin/env python3
"""Disassemble functions at RVA 0xBA8C50 and 0xBA8FE0."""
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with exe_path.open("rb") as f:
        data = f.read()

    md = Cs(CS_ARCH_X86, CS_MODE_64)

    for rva in (0xBA8C40, 0xBA8FD0):
        print(f"\n--- Disassembly at RVA 0x{rva:X} ---")
        offset = pe.get_offset_from_rva(rva)
        code = data[offset:offset + 80]
        for ins in md.disasm(code, image_base + rva):
            print(f"  0x{ins.address:016X} (RVA 0x{ins.address - image_base:08X}): {ins.mnemonic:8} {ins.op_str}")

if __name__ == "__main__":
    main()
