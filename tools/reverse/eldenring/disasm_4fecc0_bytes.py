#!/usr/bin/env python3
"""
disasm_4fecc0_bytes.py
Print raw bytes around 0x1404FECC0 to see if it's data or aligned code.
"""
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

off = rva_to_offset(0x4FECC0)
print(raw_data[off : off + 32].hex(' '))
