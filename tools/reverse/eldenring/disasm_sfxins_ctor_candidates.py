#!/usr/bin/env python3
"""
disasm_sfxins_ctor_candidates.py
Disassemble the three candidates referencing CSFD4SfxIns vtable.
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

candidates = [0xD2946E, 0xD8B350, 0xD8B400]

for cand in candidates:
    print(f"\n--- Disasm around 0x{base + cand:X} (RVA 0x{cand:X}) ---")
    start = max(0, cand - 20)
    off = rva_to_offset(start)
    code = raw_data[off : off + 70]
    for ins in cs.disasm(code, base + start):
        marker = "===>" if ins.address == base + cand else "    "
        print(f"{marker} 0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
