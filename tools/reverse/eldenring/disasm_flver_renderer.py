#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
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

def dump_vtable(pe, vt_rva, count=10, name=""):
    print(f"\n--- {name} Vtable at 0x{vt_rva:X} ---")
    offset = pe.get_offset_from_rva(vt_rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        raw = f.read(count * 8)
    vfuncs = struct.unpack(f"<{len(raw)//8}Q", raw)
    for i, vf in enumerate(vfuncs):
        vrva = vf - 0x140000000
        if 0x1000 <= vrva <= 0x3000000:
            print(f"  v[{i}] = RVA 0x{vrva:X}")
        else:
            print(f"  v[{i}] = 0x{vf:X}")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    dump_vtable(pe, 0x2F1F1B0, count=12, name="GXFlverModelRenderer")
    disasm_func(pe, 0x1AE6470, max_bytes=200, name="GXCascadedShadowScene 0x1AE6470")

if __name__ == "__main__":
    main()
