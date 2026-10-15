#!/usr/bin/env python3
"""
disasm_4f4030_more.py
Disassemble more of 0x1404F4030 to see what it destroys and what class it is!
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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

off = rva_to_offset(0x4F4030)
code = raw_data[off : off + 200]
print("Disassembly of 0x1404F4030:")
for ins in cs.disasm(code, base + 0x4F4030):
    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

# Check what vtable is set at 0x1404F404E: lea rax, [rip + 0x255943b]
vtable_va = (base + 0x4F404E + 7) + 0x255943b
print(f"\nVTable VA: 0x{vtable_va:X} (RVA 0x{vtable_va - base:X})")
