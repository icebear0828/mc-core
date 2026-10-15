#!/usr/bin/env python3
"""
disasm_callers_d8b5f0.py
Disassemble the callers of SetKillBit (0x140D8B5F0) to see what function they are in.
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

callers = [0xD8A4FE, 0xD8A918, 0xD8B006, 0xD8D167, 0xD8D317, 0xD8D4E0, 0xD8E210, 0xD8E282]

for caller_rva in callers:
    print(f"\n--- Caller at 0x{base + caller_rva:X} (RVA 0x{caller_rva:X}) ---")
    start_rva = max(0, caller_rva - 40)
    off = rva_to_offset(start_rva)
    code = raw_data[off : off + 70]
    for ins in cs.disasm(code, base + start_rva):
        marker = "===>" if ins.address == base + caller_rva else "    "
        print(f"{marker} 0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
