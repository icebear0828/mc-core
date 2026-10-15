#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

target_va = base + 0x498BF0
pattern = struct.pack("<Q", target_va)

pos = 0
while True:
    idx = raw_data.find(pattern, pos)
    if idx == -1:
        break
    for sec in pe.sections:
        if sec.PointerToRawData <= idx < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (idx - sec.PointerToRawData)
            print(f"Pointer at RVA 0x{rva:X} (0x{base + rva:X}) in {sec.Name.decode().strip(chr(0))}")
            break
    pos = idx + 1
