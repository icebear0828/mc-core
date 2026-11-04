#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=500, name=""):
    print(f"\n{'='*60}\n{name} at RVA 0x{rva:X}\n{'='*60}")
    offset = pe.get_offset_from_rva(rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(max_bytes)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for ins in md.disasm(code, rva):
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        if ins.mnemonic == "ret":
            break

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    # GXFlverModelRenderer v[0] at RVA 0x1AA90B0
    disasm_func(pe, 0x1AA90B0, max_bytes=400, name="GXFlverModelRenderer v[0] (Destructor)")
    # Let's inspect where GXFlverModelRenderer is created/used!
    # TypeDescriptor RVA: 0x03D2F348 - 0x10 = 0x3D2F338
    # Vtable RVA: 0x2F1F1B0
    # Let's find references to vtable 0x2F1F1B0 in .text!

if __name__ == "__main__":
    main()
