#!/usr/bin/env python3
"""
find_callers_speffect.py
Find all callers of HasSpEffect (0x1404FA370) in eldenring.exe.
"""
from pathlib import Path
import pefile
import struct
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

for sec in pe.sections:
    if sec.Name.startswith(b'.text'):
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

target = 0x4FA370
print(f"Finding callers of 0x{target:X} in .text...")
callers = []
for off in range(0, len(text_bytes) - 5):
    if text_bytes[off] == 0xE8:
        disp = struct.unpack("<i", text_bytes[off+1:off+5])[0]
        ins_rva = text_rva + off
        next_rva = ins_rva + 5
        if next_rva + disp == target:
            callers.append(ins_rva)

print(f"Total callers found: {len(callers)}")
for rva in callers:
    # Print the instructions leading up to the call
    start_rva = rva - 20
    off = rva_to_offset(start_rva)
    code = raw_data[off : off + 25]
    print(f"\nCaller at RVA 0x{rva:X} (VA 0x{base + rva:X}):")
    for ins in cs.disasm(code, base + start_rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
