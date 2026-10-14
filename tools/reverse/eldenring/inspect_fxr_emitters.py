#!/usr/bin/env python3
"""
inspect_fxr_emitters.py
Search for functions that update or spawn FXR emitters on CSChrModelIns or ChrIns.
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

# Look for RTTI for FXR or FX-related classes in Elden Ring
# Examples: "FXR", "Fx", "CSChrModelIns"
for name in ["CSChrModelIns", "CSFD4ModelItem", "FD4FxParam", "FxItem"]:
    pat = b".?AV" + name.encode()
    pos = raw_data.find(pat)
    print(f"RTTI {name}: {'Found' if pos != -1 else 'Not found'} (pos=0x{pos:X})")
