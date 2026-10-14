#!/usr/bin/env python3
"""
identify_derived_sfx.py
Check vtables set at 0x140D89C52 and 0x140D8C774.
"""
from pathlib import Path
import pefile
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

def get_class_name_for_vtable(vtable_rva):
    col_ptr_off = rva_to_offset(vtable_rva - 8)
    if not col_ptr_off:
        return "Unknown (no offset)"
    col_va = struct.unpack("<Q", raw_data[col_ptr_off : col_ptr_off + 8])[0]
    col_rva = col_va - base
    col_off = rva_to_offset(col_rva)
    if not col_off:
        return f"Unknown COL at VA 0x{col_va:X}"
    td_rva = struct.unpack("<I", raw_data[col_off + 0x0C : col_off + 0x10])[0]
    td_off = rva_to_offset(td_rva)
    if not td_off:
        return f"Unknown TD at RVA 0x{td_rva:X}"
    name_end = raw_data.find(b"\x00", td_off + 0x10)
    return raw_data[td_off + 0x10 : name_end].decode('ascii', errors='ignore')

vt1_rva = 0xD89C52 + 7 + 0x1E37927
vt2_rva = 0xD8C774 + 7 + 0x1E35425

print(f"VTable 1 at RVA 0x{vt1_rva:X}: {get_class_name_for_vtable(vt1_rva)}")
print(f"VTable 2 at RVA 0x{vt2_rva:X}: {get_class_name_for_vtable(vt2_rva)}")
