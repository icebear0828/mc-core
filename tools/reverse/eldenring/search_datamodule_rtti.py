#!/usr/bin/env python3
"""
Check if CSChrDataModule has derived classes or multiple vtables in eldenring.exe.
"""

from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        for s in sections:
            if s["name"] == ".data":
                f.seek(s["raw_offset"])
                data = f.read(s["raw_size"])
                pos = 0
                print("=== Searching for DataModule RTTI names in .data ===")
                while True:
                    idx = data.find(b"DataModule", pos)
                    if idx == -1:
                        break
                    start = max(0, idx - 16)
                    end = data.find(b"\x00", idx)
                    rva = s["vrva"] + idx
                    full_str = data[start:end].decode("latin-1", errors="ignore")
                    print(f"  RVA 0x{rva:08X}: {full_str}")
                    pos = idx + 10

if __name__ == "__main__":
    main()
