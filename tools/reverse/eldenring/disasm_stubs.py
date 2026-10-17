#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=300, func_name=""):
    print(f"\n==================================================")
    print(f"Function {func_name} at RVA 0x{rva:X}")
    print(f"==================================================")
    offset = pe.get_offset_from_rva(rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(max_bytes)
        
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    insns = list(md.disasm(code, rva))
    
    for ins in insns:
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        if ins.mnemonic in ("ret",):
            break

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    # v[7] target: 0x4ea2d5c
    disasm_func(pe, 0x4EA2D5C, max_bytes=200, func_name="v[7] landing 0x4EA2D5C")
    # v[8] target: 0xe4dd57
    disasm_func(pe, 0xE4DD57, max_bytes=200, func_name="v[8] landing 0xE4DD57")
    # v[9] target: 0x5a1ac8c
    disasm_func(pe, 0x5A1AC8C, max_bytes=200, func_name="v[9] landing 0x5A1AC8C")

if __name__ == "__main__":
    main()
