#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

target_va = base + 0x49A560
pat = struct.pack("<Q", target_va)
pos = 0
found = []
while True:
    idx = raw_data.find(pat, pos)
    if idx == -1: break
    rva = offset_to_rva(idx)
    print(f"Found pointer to 0x{target_va:X} at offset 0x{idx:X} (RVA 0x{rva:X})")
    pos = idx + 8
