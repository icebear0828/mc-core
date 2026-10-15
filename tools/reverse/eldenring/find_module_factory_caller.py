#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

target_fn_va = base + 0x43DB50
print(f"Target factory fn: 0x{target_fn_va:X}")

pattern = struct.pack("<Q", target_fn_va)
pos = 0
while True:
    idx = raw_data.find(pattern, pos)
    if idx == -1:
        break
    # Find section
    for sec in pe.sections:
        if sec.PointerToRawData <= idx < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (idx - sec.PointerToRawData)
            print(f"Pointer at RVA 0x{rva:X} in {sec.Name.decode('ascii', errors='ignore')}")
            break
    pos = idx + 1
