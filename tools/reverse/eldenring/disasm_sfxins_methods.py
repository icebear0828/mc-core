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

for rva in [0xD8B4C0, 0xD8B620, 0xD8B670, 0xD8B830, 0xD8B840]:
    off = rva_to_offset(rva)
    code = raw_data[off : off + 30]
    print(f"\nDisassembly of 0x{base + rva:X} (RVA 0x{rva:X}):")
    for ins in cs.disasm(code, base + rva):
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
