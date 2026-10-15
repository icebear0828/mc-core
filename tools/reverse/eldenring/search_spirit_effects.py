#!/usr/bin/env python3
"""
search_spirit_effects.py
Search for how spirits are rendered and what SpEffects/FX are attached to team 47 or summons.
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

# Look at 0x2C7210 (checked earlier in inspect_render_speffect.py)
# What is the function containing 0x2C7210?
off = rva_to_offset(0x2C7210)
code = raw_data[off - 0x20 : off + 0x60]
print("Disassembly around 0x2C7210:")
for ins in cs.disasm(code, base + 0x2C7210 - 0x20):
    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
