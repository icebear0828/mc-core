#!/usr/bin/env python3
"""
disasm_crash_1f109f9.py
Disassemble Elden Ring at RVA 0x1F109F9 to see what crashed!
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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Find callers of 0x1F10940 (the function containing 0x1F10BDF)
print("Scanning for callers of DLKR::MemoryPool functions...")
for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress
import struct

# Look for calls to RVA around 0x1F10900 - 0x1F10C00
for off in range(0, len(text_bytes) - 5):
    if text_bytes[off] == 0xE8:
        rel = struct.unpack("<i", text_bytes[off+1:off+5])[0]
        cur_rva = text_rva + off
        target_rva = cur_rva + 5 + rel
        if 0x1F10900 <= target_rva <= 0x1F10A00:
            print(f"Call to 0x{target_rva:X} from RVA 0x{cur_rva:X}")

