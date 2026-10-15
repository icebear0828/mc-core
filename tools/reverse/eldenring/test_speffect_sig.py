#!/usr/bin/env python3
"""
test_speffect_sig.py
Test whether sigs::kHasSpEffect matches uniquely in eldenring.exe.
"""
from pathlib import Path
import pefile

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

sig_str = (
    "44 0F B7 D2 48 B8 89 88 88 88 88 88 88 88 66 45 85 D2 4C 8B D9 45 8B C2 41 0F 95 C1 49 F7 E2 48 C1 EA 03 48 6B C2 0F BA 0F 00 00 00 4C 2B C0 "
    "43 8D 04 08 C0 E0 02 0F B6 C8 48 D3 E2 49 85 53 28 74 34 49 8B 43 08"
)
bytes_pattern = bytes.fromhex(sig_str.replace(" ", ""))

matches = []
pos = 0
while True:
    idx = raw_data.find(bytes_pattern, pos)
    if idx == -1:
        break
    matches.append(idx)
    pos = idx + len(bytes_pattern)

print(f"kHasSpEffect matches count: {len(matches)}")
for idx in matches:
    for sec in pe.sections:
        if sec.PointerToRawData <= idx < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (idx - sec.PointerToRawData)
            print(f"  Match at offset 0x{idx:X} (RVA 0x{rva:X}, VA 0x{base + rva:X})")
