#!/usr/bin/env python3
"""
disasm_node_fields.py
Search where node->param (node+0) or other fields of SpecialEffect node are used.
Check functions in 0x1404FAxxx or 0x1404FBxxx.
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

for rva, name in [(0x4FA470, "4FA470"), (0x4FA500, "4FA500"), (0x4FA580, "4FA580")]:
    print(f"\n--- {name} at 0x{base + rva:X} ---")
    off = rva_to_offset(rva)
    code = raw_data[off : off + 70]
    for ins in cs.disasm(code, base + rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
