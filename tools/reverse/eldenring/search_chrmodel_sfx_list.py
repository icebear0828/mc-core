#!/usr/bin/env python3
"""
search_chrmodel_sfx_list.py
Search for how CSChrModelIns holds or updates its SFX instances.
"""
from pathlib import Path
import pefile
import capstone
import struct

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

# Look at CSChrModelIns vfuncs:
# vfunc[0]: 0x1409F0390
# vfunc[1]: 0x1409F1C60
# vfunc[4]: 0x1409F2D00
# vfunc[5]: 0x1409F2DF0
for rva in [0x9F0390, 0x9F1C60, 0x9F2D00, 0x9F2DF0]:
    off = rva_to_offset(rva)
    code = raw_data[off : off + 80]
    print(f"\nCSChrModelIns at 0x{base + rva:X} (RVA 0x{rva:X}):")
    for ins in list(cs.disasm(code, base + rva))[:12]:
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
