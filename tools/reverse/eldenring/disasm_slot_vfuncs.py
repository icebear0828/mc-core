#!/usr/bin/env python3
"""
disasm_slot_vfuncs.py
Disassemble vfunc[5] and vfunc[6] of ChrMultiSfxSlot and ChrSingleSfxSlot!
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

funcs = [
    ("ChrMultiSfxSlot::vfunc[5]", 0x4A8E10),
    ("ChrMultiSfxSlot::vfunc[6]", 0x4A8E20),
    ("ChrSingleSfxSlot::vfunc[5]", 0x4AE320),
    ("ChrSingleSfxSlot::vfunc[6]", 0x4AE340),
]

for name, rva in funcs:
    print(f"\n--- {name} at 0x{base + rva:X} ---")
    off = rva_to_offset(rva)
    code = raw_data[off : off + 50]
    for ins in cs.disasm(code, base + rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
