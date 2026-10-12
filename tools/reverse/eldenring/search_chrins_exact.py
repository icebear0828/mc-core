#!/usr/bin/env python3
"""
Search for exact EnemyIns, PlayerIns, ChrIns in .data section.
"""

from disasm_worldchrman import read_pe, read_bytes, EXE_PATH, IMAGE_BASE

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        for s in sections:
            if s["name"] == ".data":
                f.seek(s["raw_offset"])
                data = f.read(s["raw_size"])
                for term in [b"EnemyIns", b"PlayerIns", b"ChrIns"]:
                    pos = 0
                    print(f"=== Matches for {term.decode()} ===")
                    while True:
                        idx = data.find(term, pos)
                        if idx == -1:
                            break
                        start = max(0, idx - 8)
                        end = data.find(b"\x00", idx)
                        rva = s["vrva"] + idx
                        full_str = data[start:end].decode("latin-1", errors="ignore")
                        print(f"  RVA 0x{rva:08X}: {full_str}")
                        pos = idx + len(term)

if __name__ == "__main__":
    main()
