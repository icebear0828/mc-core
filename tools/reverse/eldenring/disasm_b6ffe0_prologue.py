#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=300, name=""):
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
    # Start of function around 0xB6FFE0
    # Let's inspect from 0xB6FF00 to find function prologue
    offset = pe.get_offset_from_rva(0xB6FF00)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(250)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    insns = list(md.disasm(code, 0xB6FF00))
    for ins in insns:
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        if ins.address == 0xB6FFE0:
            print("  ===> TARGET 0xB6FFE0 REACHED")

if __name__ == "__main__":
    main()
