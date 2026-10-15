#!/usr/bin/env python3
"""Find writes to offset +0x20 in CSGrassRenderClusterModel methods."""
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
    start_rva = 0xBA8000
    end_rva = 0xBAA800
    offset = pe.get_offset_from_rva(start_rva)
    code = data[offset : offset + (end_rva - start_rva)]

    for ins in md.disasm(code, image_base + start_rva):
        # look for instructions with 0x20
        if "0x20]" in ins.op_str and ins.mnemonic.startswith("mov"):
            print(f"0x{ins.address:016X} (RVA 0x{ins.address - image_base:08X}): {ins.mnemonic:8} {ins.op_str}")

if __name__ == "__main__":
    main()
