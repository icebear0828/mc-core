#!/usr/bin/env python3
"""Read MSVC RTTI Complete Object Locator from vtable to find class name."""
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
    print(f"vtable slot RVA: 0x{vtable_rva:X}")

    # Read nearby slots to find where the vtable starts
    rdata_sec = next(s for s in pe.sections if s.Name.decode('latin1').startswith('.rdata'))
    sec_bytes = data[rdata_sec.PointerToRawData:rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData]

    # Let's search backward from vtable_rva to find COL pointer (starts with signature 1)
    for delta in range(0, 0x400, 8):
        check_rva = vtable_rva - delta
        offset = pe.get_offset_from_rva(check_rva)
        col_va = struct.unpack("<Q", data[offset:offset+8])[0]
        if col_va > image_base and col_va < image_base + pe.OPTIONAL_HEADER.SizeOfImage:
            col_rva = col_va - image_base
            col_offset = pe.get_offset_from_rva(col_rva)
            sig, offset_in_obj, cd_offset, type_desc_rva, chd_rva = struct.unpack("<IIIII", data[col_offset:col_offset+20])
            if sig == 1:
                # Found Complete Object Locator!
                type_desc_offset = pe.get_offset_from_rva(type_desc_rva)
                mangled_name = data[type_desc_offset + 16:type_desc_offset + 128].split(b'\x00')[0].decode('latin1', errors='replace')
                print(f"FOUND RTTI CLASS: {mangled_name} at slot -0x{delta:X}")
                return

    print("RTTI class not directly found in backward scan")

if __name__ == "__main__":
    main()
