#!/usr/bin/env python3
"""
Search eldenring.exe for Grace / Bonfire strings and RTTI classes.
"""

import os
import struct

EXE_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe"
IMAGE_BASE = 0x140000000

def read_pe(f):
    f.seek(0x3C)
    e_lfanew = struct.unpack("<I", f.read(4))[0]
    f.seek(e_lfanew + 0x14)
    size_of_opt = struct.unpack("<H", f.read(2))[0]
    opt_header_offset = e_lfanew + 0x18
    section_headers_offset = opt_header_offset + size_of_opt
    f.seek(e_lfanew + 0x6)
    num_sections = struct.unpack("<H", f.read(2))[0]
    sections = []
    f.seek(section_headers_offset)
    for _ in range(num_sections):
        sec_data = f.read(40)
        name = sec_data[:8].rstrip(b"\x00").decode("latin-1", errors="ignore")
        vsize, vrva, raw_size, raw_offset = struct.unpack("<IIII", sec_data[8:24])
        sections.append({
            "name": name,
            "vsize": vsize,
            "vrva": vrva,
            "raw_size": raw_size,
            "raw_offset": raw_offset,
        })
    return sections

def main():
    if not os.path.exists(EXE_PATH):
        print(f"Error: {EXE_PATH} not found")
        return

    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        for s in sections:
            if s["name"] in (".rdata", ".data"):
                f.seek(s["raw_offset"])
                data = f.read(s["raw_size"])
                print(f"Scanning section {s['name']} (size {len(data)})...")
                # Search for Bonfire, Campfire, Rest in general
                keywords = [b"Bonfire", b"Campfire", b"Rest", b"Sit", b"EventFlag"]
                for kw in keywords:
                    pos = 0
                    matches = 0
                    while True:
                        idx = data.find(kw, pos)
                        if idx == -1 or matches >= 25:
                            break
                        start = max(0, data.rfind(b"\x00", 0, idx) + 1)
                        end = data.find(b"\x00", idx)
                        if end != -1 and (end - start) < 160:
                            s_text = data[start:end].decode("latin-1", errors="ignore")
                            rva = s["vrva"] + start
                            if any(c in s_text for c in ["Menu", "Ins", "Action", "Module", "CS", "State", "Chr", "Event", "Param"]):
                                print(f"  [{s['name']}] RVA 0x{rva:08X}: {s_text}")
                                matches += 1
                        pos = idx + len(kw)

if __name__ == "__main__":
    main()
