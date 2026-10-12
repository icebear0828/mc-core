#!/usr/bin/env python3
"""
Find exact TypeDescriptor RVAs for EnemyIns, PlayerIns, ChrIns and their COLs/Vtables.
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        data_sec = next(s for s in sections if s["name"] == ".data")
        f.seek(data_sec["raw_offset"])
        data_bytes = f.read(data_sec["raw_size"])
        
        rdata_sec = next(s for s in sections if s["name"] == ".rdata")
        f.seek(rdata_sec["raw_offset"])
        rdata_bytes = f.read(rdata_sec["raw_size"])
        
        target_names = [b".?AVEnemyIns@CS@@", b".?AVPlayerIns@CS@@", b".?AVChrIns@CS@@"]
        
        td_map = {}
        for name in target_names:
            idx = data_bytes.find(name)
            if idx != -1:
                string_rva = data_sec["vrva"] + idx
                td_rva = string_rva - 16
                td_map[td_rva] = name.decode()
                print(f"Found {name.decode()}: TD RVA 0x{td_rva:08X} (string at 0x{string_rva:08X})")

        print("\nSearching .rdata for COLs referencing these TDs...")
        for td_rva, name in td_map.items():
            pattern = struct.pack("<I", td_rva)
            pos = 0
            while True:
                idx = rdata_bytes.find(pattern, pos)
                if idx == -1:
                    break
                # In COL, td_rva is at offset 12. So COL start is idx - 12
                if idx >= 12:
                    col_idx = idx - 12
                    sig, off, cdoff, td = struct.unpack("<IIII", rdata_bytes[col_idx:col_idx+16])
                    if sig == 1 and td == td_rva:
                        col_rva = rdata_sec["vrva"] + col_idx
                        col_va = IMAGE_BASE + col_rva
                        print(f"  COL for {name} (offset {off}) at RVA 0x{col_rva:08X} (VA 0x{col_va:016X})")
                        
                        # Find pointers to this COL in .rdata
                        col_va_bytes = struct.pack("<Q", col_va)
                        p_pos = 0
                        while True:
                            p_idx = rdata_bytes.find(col_va_bytes, p_pos)
                            if p_idx == -1:
                                break
                            vtable_rva = rdata_sec["vrva"] + p_idx + 8
                            print(f"    --> VTable RVA 0x{vtable_rva:08X} (VA 0x{IMAGE_BASE + vtable_rva:016X})")
                            p_pos = p_idx + 8
                pos = idx + 4

if __name__ == "__main__":
    main()
