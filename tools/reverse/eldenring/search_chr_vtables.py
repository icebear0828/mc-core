#!/usr/bin/env python3
"""
Inspect all vtables in eldenring.exe whose CompleteObjectLocator / TypeDescriptor
inherits from ChrIns or has ChrIns in its class hierarchy descriptor.
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        # In MSVC x64, CompleteObjectLocator (COL) is placed in .rdata:
        # struct RTTICompleteObjectLocator {
        #    DWORD signature; // 1
        #    DWORD offset;
        #    DWORD cdOffset;
        #    DWORD pTypeDescriptor; // RVA to TypeDescriptor in .data
        #    DWORD pClassDescriptor; // RVA to RTTIClassHierarchyDescriptor in .rdata
        #    DWORD pSelf; // RVA to this COL in .rdata
        # };
        
        rdata = next(s for s in sections if s["name"] == ".rdata")
        f.seek(rdata["raw_offset"])
        rdata_bytes = f.read(rdata["raw_size"])
        rdata_rva = rdata["vrva"]
        
        # We know ChrIns TypeDescriptor is at RVA 0x03C8037C (from our previous search)
        # EnemyIns TypeDescriptor is at RVA 0x03C84E74
        # PlayerIns TypeDescriptor is at RVA 0x03C85DEC
        
        target_td_rvas = {
            0x03C8037C - 0x10: "ChrIns",
            0x03C84E74 - 0x10: "EnemyIns",
            0x03C85DEC - 0x10: "PlayerIns",
        }
        
        print("=== Searching for COLs referencing ChrIns, EnemyIns, PlayerIns ===")
        # Search rdata for COLs with pTypeDescriptor in target_td_rvas
        # COL signature is 1 (0x00000001)
        cols = []
        for i in range(0, len(rdata_bytes) - 24, 4):
            sig, offset, cdOffset, td_rva = struct.unpack("<IIII", rdata_bytes[i:i+16])
            if sig == 1 and td_rva in target_td_rvas:
                col_rva = rdata_rva + i
                cols.append((col_rva, target_td_rvas[td_rva], offset))
                
        print(f"Found {len(cols)} COL entries:")
        for col_rva, name, offset in cols:
            col_va = IMAGE_BASE + col_rva
            print(f"  COL RVA 0x{col_rva:08X} (VA 0x{col_va:016X}): {name} (offset {offset})")
            
            # Now search .rdata for pointers to this COL (which precedes the vtable by 8 bytes)
            # vtable is immediately after pointer to COL
            col_va_bytes = struct.pack("<Q", col_va)
            pos = 0
            while True:
                idx = rdata_bytes.find(col_va_bytes, pos)
                if idx == -1:
                    break
                vtable_rva = rdata_rva + idx + 8
                print(f"    --> VTable RVA 0x{vtable_rva:08X} (VA 0x{IMAGE_BASE + vtable_rva:016X})")
                pos = idx + 8

if __name__ == "__main__":
    main()
