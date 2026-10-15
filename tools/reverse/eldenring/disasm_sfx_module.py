#!/usr/bin/env python3
"""
disasm_sfx_module.py
Disassemble CSChrSfxModule vtable methods (0x2A3D930) and find constructors/methods
to see what fields it has and where it attaches to ChrIns.
"""
from pathlib import Path
import pefile
import struct
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

vtable_rva = 0x2A3D930
off = rva_to_offset(vtable_rva)
print(f"CSChrSfxModule vtable at RVA 0x{vtable_rva:X}:")
vfuncs = []
for i in range(10):
    va = struct.unpack("<Q", raw_data[off + i*8 : off + (i+1)*8])[0]
    vfuncs.append(va - base)
    print(f"  vfunc[{i}]: 0x{va:X} (RVA 0x{va - base:X})")

# Also find where CSChrSfxModule vtable is stored (constructor!)
pattern = struct.pack("<Q", base + vtable_rva)
pos = 0
ctors = []
while True:
    found = raw_data.find(pattern, pos)
    if found == -1:
        break
    # Check section
    for sec in pe.sections:
        if sec.PointerToRawData <= found < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (found - sec.PointerToRawData)
            ctors.append(rva)
            break
    pos = found + 1

print("\nReferences to CSChrSfxModule vtable:")
for rva in ctors:
    print(f"  0x{base + rva:X} (RVA 0x{rva:X})")
    # Disassemble 40 bytes around it
    dis_off = rva_to_offset(rva - 20)
    code = raw_data[dis_off : dis_off + 60]
    for ins in cs.disasm(code, base + rva - 20):
        print(f"    0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
