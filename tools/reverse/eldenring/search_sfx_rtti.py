#!/usr/bin/env python3
"""
search_sfx_rtti.py
Search for all RTTI type descriptors containing 'Sfx' or 'Fx' or 'Particle' in eldenring.exe.
"""
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

for sec in pe.sections:
    if sec.Name.startswith(b'.data'):
        data_sec = sec
        break

data_bytes = raw_data[data_sec.PointerToRawData : data_sec.PointerToRawData + data_sec.SizeOfRawData]
pos = 0
found = []
while True:
    idx = data_bytes.find(b".?AV", pos)
    if idx == -1:
        break
    end = data_bytes.find(b"\x00", idx)
    if end != -1:
        name = data_bytes[idx:end].decode("latin-1", errors="ignore")
        low = name.lower()
        if "sfx" in low or "fx" in low or "particle" in low or "emitter" in low or "phantom" in low:
            rva = data_sec.VirtualAddress + idx
            found.append((rva, name))
    pos = idx + 4

print(f"Found {len(found)} matching RTTI types:")
for rva, name in found:
    print(f"  RVA 0x{rva:X}: {name}")
