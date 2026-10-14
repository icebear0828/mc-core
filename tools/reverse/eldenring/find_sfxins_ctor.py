#!/usr/bin/env python3
"""
disasm_fx_spawner.py
Search for references to "CSFD4SfxIns" creation or where SFX instances are attached to ChrIns or ModelIns.
"""
from pathlib import Path
import pefile
import struct
import capstone

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

# CSFD4SfxIns vtable is at 0x2BC17A8
sfx_vt_va = base + 0x2BC17A8
pattern = struct.pack("<Q", sfx_vt_va)

pos = 0
found = []
while True:
    idx = raw_data.find(pattern, pos)
    if idx == -1:
        break
    for sec in pe.sections:
        if sec.PointerToRawData <= idx < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (idx - sec.PointerToRawData)
            found.append(rva)
            break
    pos = idx + 1

print(f"References to CSFD4SfxIns vtable (0x{sfx_vt_va:X}):")
for rva in found:
    print(f"  0x{base + rva:X} (RVA 0x{rva:X})")
