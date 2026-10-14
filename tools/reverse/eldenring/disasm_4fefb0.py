#!/usr/bin/env python3
"""
disasm_4fefb0.py
Disassemble 0x1404FEFB0 and 0x1404FEF40.
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

for rva, name in [(0x4FEFB0, "RemoveFX / StopFX"), (0x4FEF40, "4FEF40")]:
    print(f"\n--- {name} at 0x{base + rva:X} (RVA 0x{rva:X}) ---")
    off = rva_to_offset(rva)
    code = raw_data[off : off + 80]
    for ins in cs.disasm(code, base + rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
