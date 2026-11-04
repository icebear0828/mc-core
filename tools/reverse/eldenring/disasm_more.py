#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=350, func_name=""):
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
    # Let's inspect base_chr v[1] target: 0xC4BAB0
    disasm_func(pe, 0xC4BAB0, max_bytes=250, func_name="0xC4BAB0")
    # Let's inspect base_chr v[6] continue: 0x9F2410
    disasm_func(pe, 0x9F2410, max_bytes=350, func_name="base_chr v[6] 0x9F2410 full")

if __name__ == "__main__":
    main()
