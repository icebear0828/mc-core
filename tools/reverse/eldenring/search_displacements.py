#!/usr/bin/env python3
"""
Search eldenring.exe for accesses to:
1. 0x1DED8 (our current kChrSetPointerArrayInWorldChrMan)
2. 0x10EE0 (embedded ChrSet 0)
3. 0x1E270 (mentioned in REVERSE_INTERFACES.md)
4. Any other ChrSet arrays or list in WorldChrMan
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        text_sections = [s for s in sections if s["name"].startswith(".text")]
        
        target_constants = [0x1DED8, 0x10EE0, 0x10F38, 0x10F90, 0x10FE8, 0x11040, 0x1E270]
        
        print("=== Searching for displacements ===")
        for c in target_constants:
            pattern = struct.pack("<I", c)
            hits = []
            for sec in text_sections:
                f.seek(sec["raw_offset"])
                data = f.read(sec["raw_size"])
                sec_rva = sec["vrva"]
                pos = 0
                while True:
                    idx = data.find(pattern, pos)
                    if idx == -1:
                        break
                    rva = sec_rva + idx
                    hits.append(rva)
                    pos = idx + 1
            print(f"Constant 0x{c:05X}: {len(hits)} hits")
            for h in hits[:10]:
                print(f"  RVA: 0x{h:08X} (VA: 0x{IMAGE_BASE + h:016X})")

if __name__ == "__main__":
    main()
