#!/usr/bin/env python3
"""
disasm_sfx_spawner_top.py
Find the function that calls 0x140D8C83B (RVA 0xD8C83B) or 0x140D8C750 (RVA 0xD8C750).
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

target_rva = 0xD8C811

text_sec = None
for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_start = text_sec.PointerToRawData
text_size = text_sec.SizeOfRawData
text_va = text_sec.VirtualAddress

print(f"Scanning for callers of 0x{target_rva:X}...")
hits = []
for i in range(text_size - 5):
    if raw_data[text_start + i] == 0xE8:
        rel = struct.unpack("<i", raw_data[text_start + i + 1 : text_start + i + 5])[0]
        cur_rva = text_va + i
        if cur_rva + 5 + rel == target_rva:
            hits.append(cur_rva)

print(f"Found {len(hits)} direct callers:")
for h in hits:
    print(f"  0x{base + h:X} (RVA 0x{h:X})")
