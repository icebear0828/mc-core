#!/usr/bin/env python3
"""
Search eldenring.exe for all Menu classes (e.g. .?AV.*Menu.*@CS@@).
"""

import struct
from search_grace_strings import read_pe, EXE_PATH

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        for s in sections:
            if s["name"] in (".data", ".rdata"):
                f.seek(s["raw_offset"])
                data = f.read(s["raw_size"])
                pos = 0
                while True:
                    idx = data.find(b".?AV", pos)
                    if idx == -1:
                        break
                    end = data.find(b"\x00", idx)
                    if end != -1 and (end - idx) < 160:
                        tn = data[idx:end].decode("latin-1", errors="ignore")
                        if "Menu" in tn and "@CS@@" in tn:
                            rva = s["vrva"] + idx
                            print(f"  RVA 0x{rva:08X}: {tn}")
                    pos = idx + 4

if __name__ == "__main__":
    main()
