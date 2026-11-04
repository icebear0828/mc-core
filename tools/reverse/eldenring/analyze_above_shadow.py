#!/usr/bin/env python3
"""
analyze_above_shadow.py
Analyze CSChrAboveShadowTestModule in eldenring.exe:
RTTI, vtable, cross references, and member functions.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = None
    text = None
    for s in pe.sections:
        if s.Name.startswith(b'.rdata'):
            rdata = s
        elif s.Name.startswith(b'.text'):
            text = s
            
    rdata_bytes = rdata.get_data()
    rdata_va = rdata.VirtualAddress
    
    # 1. Search for ".?AVCSChrAboveShadowTestModule@CS@@"
    name_str = b".?AVCSChrAboveShadowTestModule@CS@@"
    idx = rdata_bytes.find(name_str)
    if idx == -1:
        # try without @CS@@
        idx = rdata_bytes.find(b"CSChrAboveShadowTestModule")
        print(f"Direct class search index: {idx}")
    
    if idx != -1:
        str_rva = rdata_va + idx
        print(f"Found string at RVA 0x{str_rva:X}")
        # Search for type_info descriptor pointing to this string (str_rva - 0x10)
        # In MSVC x64 RTTI: TypeDescriptor is [vtable_ptr (8 bytes), spare (8 bytes), name (string)]
        # So TypeDescriptor RVA is (str_rva - 0x10)
        td_rva = str_rva - 0x10
        print(f"TypeDescriptor RVA: 0x{td_rva:X}")
        
        # Search for RTTI Complete Object Locator pointing to this TypeDescriptor (rdata contains td_rva as uint32)
        td_rva_bytes = struct.pack("<I", td_rva)
        col_indices = []
        start = 0
        while True:
            pos = rdata_bytes.find(td_rva_bytes, start)
            if pos == -1: break
            col_indices.append(pos)
            start = pos + 1
            
        print(f"Found {len(col_indices)} references to TypeDescriptor:")
        for pos in col_indices:
            col_rva = rdata_va + pos - 0xC # In COL, type_descriptor_rva is at offset +0x0C
            print(f"  Candidate CompleteObjectLocator at RVA 0x{col_rva:X}")
            
            # Now find vtable pointing to this COL (VA = 0x140000000 + col_rva, placed right before vtable, at vtable[-1])
            col_va = 0x140000000 + col_rva
            col_va_bytes = struct.pack("<Q", col_va)
            vtable_idx = rdata_bytes.find(col_va_bytes)
            if vtable_idx != -1:
                vtable_rva = rdata_va + vtable_idx + 8
                print(f"  ==> FOUND VTABLE for CSChrAboveShadowTestModule at RVA 0x{vtable_rva:X}!")
                
                # Read vtable methods
                with open(EXE_PATH, "rb") as f:
                    f.seek(pe.get_offset_from_rva(vtable_rva))
                    raw_vfuncs = f.read(16 * 8)
                vfuncs = struct.unpack(f"<{len(raw_vfuncs)//8}Q", raw_vfuncs)
                for i, vf in enumerate(vfuncs):
                    vrva = vf - 0x140000000
                    if 0x1000 <= vrva <= 0x3000000:
                        print(f"      v[{i}] = RVA 0x{vrva:X}")
                    else:
                        break

if __name__ == "__main__":
    main()
