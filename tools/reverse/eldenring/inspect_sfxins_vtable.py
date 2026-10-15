#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct

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

vtable_rva = 0x2BC17A8
off = rva_to_offset(vtable_rva)
print(f"CSFD4SfxIns vtable at RVA 0x{vtable_rva:X}:")
for i in range(16):
    va = struct.unpack("<Q", raw_data[off + i*8 : off + (i+1)*8])[0]
    print(f"  vfunc[{i}]: 0x{va:X} (RVA 0x{va - base:X})")
