#!/usr/bin/env python3
"""
search_fxr_spawner.py
Search for functions that spawn FXR or create SFX on character.
"""
from pathlib import Path
import pefile
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

# Let's search for string "FXR" or ".fxr" in .rdata
for sec in pe.sections:
    if sec.Name.startswith(b'.rdata'):
        rdata_sec = sec
        break

rdata_bytes = raw_data[rdata_sec.PointerToRawData : rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData]
pos = 0
found = []
while True:
    idx = rdata_bytes.find(b".fxr", pos)
    if idx == -1:
        break
    rva = rdata_sec.VirtualAddress + idx
    # find surrounding string
    start = max(0, idx - 20)
    end = min(len(rdata_bytes), idx + 20)
    found.append((rva, rdata_bytes[start:end]))
    pos = idx + 4

print(f"Found {len(found)} '.fxr' references in .rdata:")
for rva, snippet in found[:10]:
    print(f"  RVA 0x{rva:X}: {snippet}")
