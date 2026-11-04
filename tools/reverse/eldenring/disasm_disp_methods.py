#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=250, func_name=""):
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
    disasm_func(pe, 0xB324F0, max_bytes=250, func_name="CSModelDispEntity sub_b324f0")
    disasm_func(pe, 0xB325E0, max_bytes=250, func_name="CSModelDispEntity sub_b325e0")
    disasm_func(pe, 0xB32840, max_bytes=250, func_name="CSModelDispEntity sub_b32840")

if __name__ == "__main__":
    main()
