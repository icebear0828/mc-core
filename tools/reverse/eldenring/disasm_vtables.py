#!/usr/bin/env python3
"""
disasm_vtables.py
Disassembles the virtual functions of CSChrModelIns and CSChrAsmModelIns from eldenring.exe.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=160, func_name=""):
    print(f"\n==================================================")
    print(f"Function {func_name} at RVA 0x{rva:X}")
    print(f"==================================================")
    
    # Map RVA to file offset
    offset = pe.get_offset_from_rva(rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(max_bytes)
        
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    insns = list(md.disasm(code, rva))
    
    for ins in insns:
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        # Stop at unconditional return or jump to another function
        if ins.mnemonic in ("ret",):
            break
        if ins.mnemonic == "jmp" and not ins.op_str.startswith("short") and not ins.op_str.startswith("0x"):
            # jmp [rax+...] or jmp rcx etc
            break

def main():
    if not EXE_PATH.is_file():
        print(f"{EXE_PATH} does not exist!")
        return 1
        
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    
    # Functions from base_chr_model (CSChrModelIns vtable at 0x2B35C68)
    base_chr_vfuncs = [
        (0x9F0390, "base_chr v[0]"),
        (0x9F1C60, "base_chr v[1]"),
        (0x9F03E0, "base_chr v[2]"),
        (0x9F26D0, "base_chr v[3]"),
        (0x9F2D00, "base_chr v[4]"),
        (0x9F2DF0, "base_chr v[5]"),
        (0x9F2410, "base_chr v[6]"),
        (0x9F1740, "base_chr v[7]"),
        (0x9F17B0, "base_chr v[8]"),
        (0x9F03D0, "base_chr v[9]"),
        (0x9F1610, "base_chr v[10]"),
    ]
    
    for rva, name in base_chr_vfuncs:
        disasm_func(pe, rva, max_bytes=200, func_name=name)
        
    # Also asm_model v[0]
    disasm_func(pe, 0x9EAA00, max_bytes=200, func_name="asm_model v[0]")
    
    return 0

if __name__ == "__main__":
    main()
