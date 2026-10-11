#!/usr/bin/env python3
"""
Find all RTTI classes in eldenring.exe that inherit from ChrIns or are related to ChrIns.
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        # Search for .?AV.*Ins@CS@@ in .data
        rdata = None
        for s in sections:
            if s["name"] == ".data":
                rdata = s
                break
                
        if not rdata:
            print("No .rdata section found")
            return
            
        f.seek(rdata["raw_offset"])
        data = f.read(rdata["raw_size"])
        
        # Search for type descriptors (.?AV)
        print("=== Searching for Character-related TypeDescriptors ===")
        pos = 0
        while True:
            idx = data.find(b".?AV", pos)
            if idx == -1:
                break
            # Find null terminator
            end = data.find(b"\x00", idx)
            if end != -1:
                name = data[idx:end].decode("latin-1", errors="ignore")
                if ("Ins" in name or "Chr" in name) and "@CS@@" in name:
                    rva = rdata["vrva"] + idx
                    print(f"  RVA 0x{rva:08X}: {name}")
            pos = idx + 4

if __name__ == "__main__":
    main()
