#!/usr/bin/env python3
"""
find_sfxins_rva_refs.py
Find references to 0x2BC17A8 as 32-bit RVA (e.g. lea rax, [rip + rel32]) in .text section!
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

target_rva = 0x2BC17A8

text_sec = None
for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_start = text_sec.PointerToRawData
text_size = text_sec.SizeOfRawData
text_va = text_sec.VirtualAddress

print(f"Scanning .text for RIP-relative references to 0x{target_rva:X}...")

hits = []
for i in range(text_size - 4):
    cur_rva = text_va + i
    # 4-byte displacement is at cur_rva + 3 (e.g. 48 8d 05 [rel32])
    # Next instruction address is cur_rva + 7
    rel = struct.unpack("<i", raw_data[text_start + i : text_start + i + 4])[0]
    # If this rel32 points to target_rva:
    # RIP at end of instruction = cur_rva + 4 (if rel32 is at end of instruction)
    if (cur_rva + 4 + rel) == target_rva:
        hits.append(cur_rva - 3) # approximately instruction start

print(f"Found {len(hits)} candidates:")
for h in hits:
    print(f"  0x{base + h:X} (RVA 0x{h:X})")
