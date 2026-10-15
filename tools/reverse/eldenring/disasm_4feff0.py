#!/usr/bin/env python3
"""
disasm_4feff0.py
Disassemble 0x1404FEFF0 to see what it does.
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

off = rva_to_offset(0x4FEFF0)
code = raw_data[off : off + 200]
print("Disasm 0x1404FEFF0:")
for ins in cs.disasm(code, base + 0x4FEFF0):
    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
