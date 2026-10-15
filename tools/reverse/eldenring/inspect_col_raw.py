#!/usr/bin/env python3
"""
Inspect the raw bytes of COL for WorldChrManImp (COL RVA 0x032D9A88).
"""

import struct
from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        col_rva = 0x032D9A88
        col_bytes = read_bytes(f, sections, col_rva, 32)
        print("Raw COL bytes:")
        print(" ".join(f"{b:02X}" for b in col_bytes))
        words = struct.unpack("<IIIIII", col_bytes[:24])
        print(f"words: {[hex(w) for w in words]}")

if __name__ == "__main__":
    main()
