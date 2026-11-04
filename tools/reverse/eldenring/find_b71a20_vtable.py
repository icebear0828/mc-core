#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    target_rva = 0xB71A20
    target_va = 0x140000000 + target_rva
    target_bytes = struct.pack("<Q", target_va)
    
    # Check all sections for pointer to 0xB71A20!
    for s in pe.sections:
        data = s.get_data()
        pos = 0
        while True:
            idx = data.find(target_bytes, pos)
            if idx == -1: break
            rva = s.VirtualAddress + idx
            sec_name = s.Name.decode("ascii", errors="ignore").strip("\x00")
            print(f"Found function pointer in section {sec_name} at RVA 0x{rva:X} (vtable entry?)")
            pos = idx + 8

if __name__ == "__main__":
    main()
