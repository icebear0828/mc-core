#!/usr/bin/env python3
"""
disasm_all_vfuncs.py
Disassemble the complete code of all 11 virtual functions of CSChrModelIns (vtable 0x2B35C68)
and identify all cross-references to these functions from eldenring.exe.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func_full(pe, rva, max_bytes=800, name=""):
    print(f"\n{'='*60}")
    print(f"Function {name} at RVA 0x{rva:X}")
    print(f"{'='*60}")
    offset = pe.get_offset_from_rva(rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(max_bytes)
        
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    insns = list(md.disasm(code, rva))
    
    for ins in insns:
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        if ins.mnemonic == "ret":
            break

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    vfuncs = [
        (0x9F0390, "v[0] (Destructor/Delete)"),
        (0x9F1C60, "v[1]"),
        (0x9F03E0, "v[2] (returns 0)"),
        (0x9F26D0, "v[3] (set disp flags)"),
        (0x9F2D00, "v[4] (update disp 0xb324f0)"),
        (0x9F2DF0, "v[5] (update pose to +0x130)"),
        (0x9F2410, "v[6] (matrix transform to +0x130)"),
        (0x9F1740, "v[7]"),
        (0x9F17B0, "v[8]"),
        (0x9F03D0, "v[9]"),
        (0x9F1610, "v[10]"),
    ]
    for rva, name in vfuncs:
        disasm_func_full(pe, rva, max_bytes=300, name=name)

if __name__ == "__main__":
    main()
