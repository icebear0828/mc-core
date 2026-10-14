#!/usr/bin/env python3
"""
locate_sfx_slots.py
Find vtables of ChrMultiSfxSlot, ChrConditionSfxSeSlot, ChrSingleSfxSlot, CSFD4MultiSfxIns, CSChrSfxModule.
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

target_classes = [
    b".?AVCSChrSfxModule@CS@@\x00",
    b".?AVChrConditionSfxSeSlot@CS@@\x00",
    b".?AVChrMultiSfxSlot@CS@@\x00",
    b".?AVChrSingleSfxSlot@CS@@\x00",
    b".?AVCSFD4SfxIns@CS@@\x00",
    b".?AVCSFD4MultiSfxIns@CS@@\x00",
    b".?AVCSFD4SingleSfxIns@CS@@\x00",
]

for cls in target_classes:
    idx = raw_data.find(cls)
    if idx == -1:
        continue
    td_off = idx - 0x10
    td_rva = None
    for sec in pe.sections:
        if sec.PointerToRawData <= td_off < sec.PointerToRawData + sec.SizeOfRawData:
            td_rva = sec.VirtualAddress + (td_off - sec.PointerToRawData)
            break
    td_va = base + td_rva
    # Now find Complete Object Locator pointing to td_va (or td_rva in 64-bit COL: COL has rva of td at col+0x0C)
    # In x64 RTTI: COL struct:
    # dword signature (0 or 1)
    # dword offset
    # dword cdOffset
    # dword pTypeDescriptor (RVA to TD)
    # dword pClassHierarchy (RVA)
    # dword pSelf (RVA to COL itself)
    # Search for struct with pTypeDescriptor == td_rva
    pattern = struct.pack("<I", td_rva)
    pos = 0
    print(f"\nClass: {cls.decode('ascii')}")
    while True:
        col_pos = raw_data.find(pattern, pos)
        if col_pos == -1:
            break
        # col_pos is col + 0x0C
        col_offset = col_pos - 0x0C
        # check signature at col_offset
        sig = struct.unpack("<I", raw_data[col_offset:col_offset+4])[0]
        if sig in (0, 1):
            col_rva = None
            for sec in pe.sections:
                if sec.PointerToRawData <= col_offset < sec.PointerToRawData + sec.SizeOfRawData:
                    col_rva = sec.VirtualAddress + (col_offset - sec.PointerToRawData)
                    break
            if col_rva:
                col_va = base + col_rva
                # Now search for pointer to col_va in .rdata (this is right before vtable!)
                col_va_bytes = struct.pack("<Q", col_va)
                vt_pos = 0
                while True:
                    found_vt = raw_data.find(col_va_bytes, vt_pos)
                    if found_vt == -1:
                        break
                    vtable_offset = found_vt + 8
                    vtable_rva = None
                    for sec in pe.sections:
                        if sec.PointerToRawData <= vtable_offset < sec.PointerToRawData + sec.SizeOfRawData:
                            vtable_rva = sec.VirtualAddress + (vtable_offset - sec.PointerToRawData)
                            break
                    if vtable_rva:
                        print(f"  VTable at RVA 0x{vtable_rva:X} (0x{base + vtable_rva:X}), COL RVA 0x{col_rva:X}")
                    vt_pos = found_vt + 1
        pos = col_pos + 1
