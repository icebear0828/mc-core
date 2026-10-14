#!/usr/bin/env python3
"""Find who calls the CSGrassRenderClusterModel render method."""
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with exe_path.open("rb") as f:
        data = f.read()

    # The vtable pointer is at 0x142B7FAE8.
    # Where is 0x142B7FAE8 referenced? (Constructors of CSGrassRenderClusterModel)
    vt_va = 0x140000000 + 0x2B7FAE8
    vt_bytes = vt_va.to_bytes(8, "little")

    for s in pe.sections:
        name = s.Name.decode("latin1").strip("\x00")
        raw = data[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        pos = 0
        while True:
            pos = raw.find(vt_bytes, pos)
            if pos == -1:
                break
            rva = s.VirtualAddress + pos
            print(f"CSGrassRenderClusterModel vtable stored/referenced in {name} at RVA 0x{rva:X} (VA 0x{image_base + rva:X})")
            pos += 8

if __name__ == "__main__":
    main()
