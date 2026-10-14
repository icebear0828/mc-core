#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct
import capstone

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

td_pos = 0x3CF4B78 - 0x10
td_rva = offset_to_rva(td_pos)
print(f"CSFD4ModelItem TypeDescriptor RVA: 0x{td_rva:X}")

# Search for COL
col_pattern = struct.pack("<III", 1, 0, 0) + struct.pack("<I", td_rva)
col_off = raw_data.find(col_pattern)
if col_off != -1:
    col_rva = offset_to_rva(col_off)
    print(f"COL RVA: 0x{col_rva:X}")
    # Search for vtable pointing to COL
    col_va = base + col_rva
    vt_pattern = struct.pack("<Q", col_va)
    vt_off = raw_data.find(vt_pattern)
    if vt_off != -1:
        vt_rva = offset_to_rva(vt_off + 8)
        print(f"CSFD4ModelItem vtable RVA: 0x{vt_rva:X}")
        for i in range(8):
            entry_va = struct.unpack("<Q", raw_data[vt_off + 8 + i*8 : vt_off + 8 + (i+1)*8])[0]
            print(f"  vfunc[{i}]: 0x{entry_va:X} (RVA 0x{entry_va - base:X})")
