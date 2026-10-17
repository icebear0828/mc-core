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
    disasm_func(pe, 0x9F1740, max_bytes=100, func_name="v[7] at 0x9F1740")
    # Follow jmp 0x4ea2d5c - wait, let's check what 0x4EA2D5C is!
    # In 64-bit RIP relative: jmp 0x4ea2d5c is an RVA or VA?
    # Capstone shows target address directly as RVA if base is 0.
    # But wait! 0x4EA2D5C is > image size? Let's check image size:
    print(f"Image base: 0x{pe.OPTIONAL_HEADER.ImageBase:X}, SizeOfImage: 0x{pe.OPTIONAL_HEADER.SizeOfImage:X}")

if __name__ == "__main__":
    main()
