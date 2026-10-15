#!/usr/bin/env python3
"""
disasm_sfx_creators.py
Disassemble 0x140D89C4C and 0x140D8C76E.
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

for rva in [0xD89C4C, 0xD8C76E]:
    print(f"\n--- Disasm around 0x{base + rva:X} (RVA 0x{rva:X}) ---")
    start = max(0, rva - 40)
    off = rva_to_offset(start)
    code = raw_data[off : off + 100]
    for ins in cs.disasm(code, base + start):
        marker = "===>" if ins.address == base + rva else "    "
        print(f"{marker} 0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
