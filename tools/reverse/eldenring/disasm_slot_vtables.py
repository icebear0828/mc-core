#!/usr/bin/env python3
"""
disasm_slot_vtables.py
Inspect the virtual functions of ChrMultiSfxSlot and ChrSingleSfxSlot.
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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

slots = [
    ("ChrMultiSfxSlot", 0x2A43400),
    ("ChrSingleSfxSlot", 0x2A43AA8),
    ("ChrConditionSfxSeSlot", 0x2A428B0),
]

for name, vt_rva in slots:
    print(f"\n================ {name} (VTable 0x{vt_rva:X}) ================")
    off = rva_to_offset(vt_rva)
    for i in range(12):
        va = struct.unpack("<Q", raw_data[off + i*8 : off + (i+1)*8])[0]
        rva = va - base
        print(f"  vfunc[{i}]: 0x{va:X} (RVA 0x{rva:X})")
