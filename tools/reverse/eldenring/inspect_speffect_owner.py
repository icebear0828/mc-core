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

# Disassemble 0x1404F4030 (the destructor/cleanup called in 0x1404F4450)
rva = 0x4F4030
off = rva_to_offset(rva)
if off:
    code = raw_data[off : off + 100]
    print(f"\nDisassembly of SpecialEffect cleanup at 0x{base + rva:X} (RVA 0x{rva:X}):")
    for ins in list(cs.disasm(code, base + rva))[:20]:
        print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
