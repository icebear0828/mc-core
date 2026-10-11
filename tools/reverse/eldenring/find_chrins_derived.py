#!/usr/bin/env python3
"""
Find ALL classes in eldenring.exe that inherit from ChrIns by checking BaseClassArray.
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        rdata_sec = next(s for s in sections if s["name"] == ".rdata")
        f.seek(rdata_sec["raw_offset"])
        rdata_bytes = f.read(rdata_sec["raw_size"])
        rdata_rva = rdata_sec["vrva"]
        
        data_sec = next(s for s in sections if s["name"] == ".data")
        f.seek(data_sec["raw_offset"])
        data_bytes = f.read(data_sec["raw_size"])
        data_rva = data_sec["vrva"]
        
        chrins_td_rva = 0x03C80368 # TD RVA for ChrIns
        
        # Search for BaseClassDescriptor referencing ChrIns:
        # struct _RTTIBaseClassDescriptor {
        #    int pTypeDescriptor; // RVA
        #    DWORD numContainedBases;
        #    ...
        # }
        pattern = struct.pack("<I", chrins_td_rva)
        pos = 0
        bcd_rvas = set()
        while True:
            idx = rdata_bytes.find(pattern, pos)
            if idx == -1:
                break
            bcd_rvas.add(rdata_rva + idx)
            pos = idx + 4
            
        print(f"Found {len(bcd_rvas)} BaseClassDescriptors for ChrIns")
        
        # Now search for BaseClassArray (array of RVAs to BCD) referencing any of these BCDs
        bca_rvas = set()
        for bcd in bcd_rvas:
            p = struct.pack("<I", bcd)
            pos = 0
            while True:
                idx = rdata_bytes.find(p, pos)
                if idx == -1:
                    break
                bca_rvas.add(rdata_rva + idx)
                pos = idx + 4
                
        # Now search for ClassHierarchyDescriptor referencing these BCAs
        # struct _RTTIClassHierarchyDescriptor {
        #    DWORD signature;
        #    DWORD attributes;
        #    DWORD numBaseClasses;
        #    int pBaseClassArray; // RVA
        # }
        chd_rvas = set()
        for bca in bca_rvas:
            p = struct.pack("<I", bca)
            pos = 0
            while True:
                idx = rdata_bytes.find(p, pos)
                if idx == -1:
                    break
                if idx >= 12:
                    chd_rvas.add(rdata_rva + (idx - 12))
                pos = idx + 4
                
        # Now search for COLs referencing these CHDs
        # In COL: chd_rva is at offset 16 (4th dword)
        cols = []
        for chd in chd_rvas:
            p = struct.pack("<I", chd)
            pos = 0
            while True:
                idx = rdata_bytes.find(p, pos)
                if idx == -1:
                    break
                if idx >= 16:
                    col_rva = rdata_rva + (idx - 16)
                    # Read td_rva at col_rva + 12
                    td_rva = struct.unpack("<I", rdata_bytes[col_rva - rdata_rva + 12:col_rva - rdata_rva + 16])[0]
                    # Read name from td_rva + 16
                    td_offset = td_rva - data_rva
                    if 0 <= td_offset < len(data_bytes):
                        raw_name = data_bytes[td_offset + 16:].split(b"\x00")[0].decode("latin-1", errors="ignore")
                        cols.append((col_rva, td_rva, raw_name))
                pos = idx + 4
                
        print(f"\nFound {len(cols)} classes inheriting from ChrIns:")
        for col_rva, td_rva, name in cols:
            col_va = IMAGE_BASE + col_rva
            # Find vtable
            col_va_bytes = struct.pack("<Q", col_va)
            p_idx = rdata_bytes.find(col_va_bytes)
            vt_info = ""
            if p_idx != -1:
                vt_rva = rdata_rva + p_idx + 8
                vt_info = f"-> Vtable RVA 0x{vt_rva:08X}"
            print(f"  {name:35s} (COL 0x{col_rva:08X}) {vt_info}")

if __name__ == "__main__":
    main()
