#!/usr/bin/env python3
"""
disasm_4fecb0_exact.py
Disassemble instructions starting at 0x1404FECB4 / 0x1404FECC0.
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

for start_rva in [0x4FECBE, 0x4FECC0, 0x4FECC4]:
    print(f"\n--- Disasm from 0x{base + start_rva:X} ---")
    off = rva_to_offset(start_rva)
    code = raw_data[off : off + 60]
    for ins in cs.disasm(code, base + start_rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
