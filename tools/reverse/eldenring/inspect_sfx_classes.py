#!/usr/bin/env python3
"""
inspect_sfx_classes.py
Find RTTI classes related to SFX and their vtables, to find all SFX instance classes.
"""
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

# Search for type descriptors containing Sfx
# In MSVC x64 RTTI, type descriptor has name at +0x10 (.?AV...)
rdata_sec = None
data_sec = None
for sec in pe.sections:
    name = sec.Name.rstrip(b'\x00').decode('ascii', errors='ignore')
    if name == '.rdata':
        rdata_sec = sec
    elif name == '.data':
        data_sec = sec

print("Searching for Sfx RTTI type descriptors...")
hits = []
pos = 0
while True:
    idx = raw_data.find(b".?AVCSFD4Sfx", pos)
    if idx == -1:
        break
    # type descriptor starts at idx - 0x10
    td_offset = idx - 0x10
    td_rva = None
    for sec in pe.sections:
        if sec.PointerToRawData <= td_offset < sec.PointerToRawData + sec.SizeOfRawData:
            td_rva = sec.VirtualAddress + (td_offset - sec.PointerToRawData)
            break
    end_str = raw_data.find(b"\x00", idx)
    name = raw_data[idx:end_str].decode('ascii', errors='ignore')
    hits.append((td_rva, name))
    pos = idx + 1

for rva, name in hits:
    print(f"  TypeDesc at RVA 0x{rva:X}: {name}")
