#!/usr/bin/env python3
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

# Look backwards from 0x49A660 to find function prologue (e.g. 48 89 5c 24 or 40 55 or push rbp...)
off = rva_to_offset(0x49A500)
code = raw_data[off : off + 200]
print("Disassembly from 0x14049A500:")
for ins in cs.disasm(code, base + 0x49A500):
    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
