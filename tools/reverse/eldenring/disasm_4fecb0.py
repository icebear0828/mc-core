#!/usr/bin/env python3
"""
disasm_4fecb0.py
Disassemble around 0x1404FECB0 to find the actual function start.
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

off = rva_to_offset(0x4FEC70)
code = raw_data[off : off + 150]
print("Disassembly of 0x1404FEC70:")
for ins in cs.disasm(code, base + 0x4FEC70):
    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
