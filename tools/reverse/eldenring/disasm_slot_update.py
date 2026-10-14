#!/usr/bin/env python3
"""
disasm_slot_update.py
Disassemble ChrMultiSfxSlot::vfunc[4] (0x1404A9010) and ChrSingleSfxSlot::vfunc[4] (0x1404AE4B0)
to see where their ctrl word / skip bit is located!
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

for rva, name in [(0x4A9010, "ChrMultiSfxSlot::vfunc[4] (Update)"), (0x4AE4B0, "ChrSingleSfxSlot::vfunc[4] (Update)")]:
    print(f"\n--- {name} at 0x{base + rva:X} ---")
    off = rva_to_offset(rva)
    code = raw_data[off : off + 80]
    for ins in cs.disasm(code, base + rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
