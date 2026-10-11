#!/usr/bin/env python3
"""
Inspect BulletParam structure and bullet velocity in eldenring.exe.
RVA 0x14039E8B0 is CSBulletIns::Init where initial velocity and orientation are loaded.
"""

import pefile
import capstone
import sys
from pathlib import Path

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    if not exe_path.exists():
        print("exe not found")
        return 1

    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"Image Base: 0x{image_base:X}")

    # CSBulletIns::Init is at RVA ~ 0x39E8B0
    # Let's inspect the code around 0x14039E8B0
    init_rva = 0x39E8B0
    
    with open(exe_path, "rb") as f:
        # Read from file offset
        # Convert RVA to file offset
        for section in pe.sections:
            sec_name = section.Name.decode().strip('\x00')
            sec_start = section.VirtualAddress
            sec_end = sec_start + section.Misc_VirtualSize
            if sec_start <= init_rva < sec_end:
                file_off = section.PointerToRawData + (init_rva - sec_start)
                f.seek(file_off)
                code = f.read(512)
                break
        else:
            print("Section not found for RVA")
            return 1

    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print(f"\n--- Disassembly at RVA 0x{init_rva:X} ---")
    for ins in cs.disasm(code, image_base + init_rva):
        print(f"0x{ins.address:X}: {ins.mnemonic:8} {ins.op_str}")

if __name__ == "__main__":
    main()
