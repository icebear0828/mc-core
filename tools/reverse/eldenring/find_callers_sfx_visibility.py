#!/usr/bin/env python3
"""
find_callers_sfx_visibility.py
Find callers of 0x140D8B840 (SetVisible) and 0x140D8B5F0 (SetKillBit) in eldenring.exe
"""
from pathlib import Path
import pefile
import capstone
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

# Search for direct calls (E8 rel32) to 0xD8B840 or 0xD8B5F0
targets = {
    0xD8B840: "SetVisible",
    0xD8B5F0: "SetKillBit",
}

text_sec = None
for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_start = text_sec.PointerToRawData
text_size = text_sec.SizeOfRawData
text_va = text_sec.VirtualAddress

print(f"Scanning .text: RVA 0x{text_va:X} to 0x{text_va + text_size:X}...")

callers = []
for i in range(text_size - 5):
    if raw_data[text_start + i] == 0xE8:
        rel = struct.unpack("<i", raw_data[text_start + i + 1 : text_start + i + 5])[0]
        cur_rva = text_va + i
        target_rva = cur_rva + 5 + rel
        if target_rva in targets:
            callers.append((cur_rva, targets[target_rva]))

print(f"Found {len(callers)} direct callers:")
for rva, name in callers:
    print(f"  0x{base + rva:X} (RVA 0x{rva:X}) -> {name}")
