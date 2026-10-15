#!/usr/bin/env python3
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

target_va = 0x1404FA370
target_rva = 0x4FA370

# Search for CALL target_rva in .text section
text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_size = text_sec.SizeOfRawData
text_rva = text_sec.VirtualAddress

callers = []
for i in range(text_start, text_start + text_size - 5):
    if raw_data[i] == 0xE8: # CALL rel32
        rel32 = struct.unpack("<i", raw_data[i+1:i+5])[0]
        cur_rva = text_rva + (i - text_start)
        dest_rva = cur_rva + 5 + rel32
        if dest_rva == target_rva:
            callers.append(cur_rva)

print(f"Found {len(callers)} direct callers to HasSpEffect (0x1404FA370):")
for c in callers[:20]:
    print(f"  Caller RVA: 0x{c:X} (VA 0x{base + c:X})")
