#!/usr/bin/env python3
"""Inspect vtable for CSGrassRenderClusterModel."""
from pathlib import Path
import pefile
import struct

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with exe_path.open("rb") as f:
        data = f.read()

    vtable_rva = 0x2B7FB00
    print(f"=== Vtable around 0x{image_base + vtable_rva:X} ===")

    for i in range(-4, 10):
        slot_rva = vtable_rva + i * 8
        offset = pe.get_offset_from_rva(slot_rva)
        val = struct.unpack("<Q", data[offset:offset+8])[0]
        rel_fn_rva = val - image_base if (val >= image_base and val < image_base + pe.OPTIONAL_HEADER.SizeOfImage) else 0
        marker = "===> " if i == 0 else "     "
        print(f"{marker}slot[{i:2d}] at RVA 0x{slot_rva:X}: 0x{val:016X} (fn RVA 0x{rel_fn_rva:X})")

if __name__ == "__main__":
    main()
