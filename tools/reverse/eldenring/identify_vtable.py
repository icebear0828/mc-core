#!/usr/bin/env python3
"""
identify_vtable.py
Find RTTI class name for a given vtable RVA.
"""
import sys
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
    # In MSVC x64, pointer at vtable_rva - 8 points to CompleteObjectLocator (VA)
    col_ptr_off = rva_to_offset(vtable_rva - 8)
    if not col_ptr_off:
        return "Unknown (no offset)"
    col_va = struct.unpack("<Q", raw_data[col_ptr_off : col_ptr_off + 8])[0]
    col_rva = col_va - base
    col_off = rva_to_offset(col_rva)
    if not col_off:
        return f"Unknown COL at VA 0x{col_va:X}"
    # COL has td_rva at col + 0x0C
    td_rva = struct.unpack("<I", raw_data[col_off + 0x0C : col_off + 0x10])[0]
    td_off = rva_to_offset(td_rva)
    if not td_off:
        return f"Unknown TD at RVA 0x{td_rva:X}"
    # Name is at td + 0x10
    name_end = raw_data.find(b"\x00", td_off + 0x10)
    return raw_data[td_off + 0x10 : name_end].decode('ascii', errors='ignore')

vtables = [0x2A4D490, 0x2BC17A8, 0x2A3D930, 0x2A428B0]
for vt in vtables:
    name = get_class_name_for_vtable(vt)
    print(f"VTable 0x{vt:X}: {name}")
