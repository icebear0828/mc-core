#!/usr/bin/env python3
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

for sec in pe.sections:
    if sec.Name.startswith(b'.text'):
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress

target = 0x498BF0
print(f"Finding callers of 0x{target:X} in .text...")
for off in range(0, len(text_bytes) - 5):
    if text_bytes[off] == 0xE8:
        disp = struct.unpack("<i", text_bytes[off+1:off+5])[0]
        ins_rva = text_rva + off
        next_rva = ins_rva + 5
        if next_rva + disp == target:
            print(f"  Call from RVA 0x{ins_rva:X} (VA 0x{base + ins_rva:X})")
