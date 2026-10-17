#!/usr/bin/env python3
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata_sec = None
    for s in pe.sections:
        if s.Name.startswith(b'.rdata'):
            rdata_sec = s
            break
            
    vtable_rva = 0x2B35C68 # CSChrModelIns vtable
    offset = pe.get_offset_from_rva(vtable_rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        # read 64 pointers
        raw = f.read(64 * 8)
        
    vfuncs = struct.unpack(f"<{len(raw)//8}Q", raw)
    print(f"CSChrModelIns vtable entries at RVA 0x{vtable_rva:X}:")
    for i, vf in enumerate(vfuncs):
        # Check if vf looks like a text address (image base 0x140000000)
        rva = vf - 0x140000000
        if 0x1000 <= rva <= 0x3000000:
            print(f"  v[{i:2d}] = RVA 0x{rva:X}")
        else:
            print(f"  v[{i:2d}] = 0x{vf:X} (end of table?)")
            break

if __name__ == "__main__":
    main()
