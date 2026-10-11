#!/usr/bin/env python3
import pefile
import capstone
import sys
from pathlib import Path

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    # spawn_bullet RVA: 0x3A2CB0
    rva = 0x3A2CB0
    with open(exe_path, "rb") as f:
        for section in pe.sections:
            sec_start = section.VirtualAddress
            sec_end = sec_start + section.Misc_VirtualSize
            if sec_start <= rva < sec_end:
                file_off = section.PointerToRawData + (rva - sec_start)
                f.seek(file_off)
                code = f.read(256)
                break

    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in cs.disasm(code, image_base + rva):
        print(f"0x{ins.address:X}: {ins.mnemonic:8} {ins.op_str}")

if __name__ == "__main__":
    main()
