#!/usr/bin/env python3
"""
inspect_all_sfx_rtti.py
Search for any RTTI class containing "Sfx" or "Fx" or "Particle"
"""
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

pos = 0
found = set()
while True:
    idx = raw_data.find(b".?AV", pos)
    if idx == -1:
        break
    end_str = raw_data.find(b"\x00", idx)
    name = raw_data[idx:end_str].decode('ascii', errors='ignore')
    lower = name.lower()
    if "sfx" in lower or "gxflver" in lower or "dummy" in lower:
        if name not in found:
            found.add(name)
            print(name)
    pos = idx + 4
