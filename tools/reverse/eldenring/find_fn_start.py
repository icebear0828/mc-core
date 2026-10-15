#!/usr/bin/env python3
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

# Scan backwards from 0x49A660 for CC CC (int3 int3 padding) or 48 89 / 40 55 function prologue
off = rva_to_offset(0x49A660)
for i in range(off, off - 500, -1):
    if raw_data[i] == 0xCC and raw_data[i-1] == 0xCC:
        fn_start_off = i + 1
        fn_start_rva = pe.sections[0].VirtualAddress + (fn_start_off - pe.sections[0].PointerToRawData)
        print(f"Function starts at RVA 0x{fn_start_rva:X} (VA 0x{base + fn_start_rva:X})")
        break
