#!/usr/bin/env python3
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def dump_vt(pe, vtable_rva, name):
    offset = pe.get_offset_from_rva(vtable_rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        raw = f.read(64 * 8)
    vfuncs = struct.unpack(f"<{len(raw)//8}Q", raw)
    print(f"\n{name} vtable entries at RVA 0x{vtable_rva:X}:")
    for i, vf in enumerate(vfuncs):
        rva = vf - 0x140000000
        if 0x1000 <= rva <= 0x3000000:
            print(f"  v[{i:2d}] = RVA 0x{rva:X}")
        else:
            print(f"  v[{i:2d}] = 0x{vf:X} (end of table)")
            break

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    dump_vt(pe, 0x2B35980, "CSChrAsmModelIns")
    dump_vt(pe, 0x2B35C68, "CSChrModelIns")

if __name__ == "__main__":
    main()
