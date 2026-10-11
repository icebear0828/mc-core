#!/usr/bin/env python3
"""
Inspect ChrSet constructor, methods, callers, and WorldChrMan structure in eldenring.exe.
"""

import os
import struct
import sys

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

def rva_to_offset(sections, rva):
    for sec in sections:
        if sec["vrva"] <= rva < sec["vrva"] + max(sec["vsize"], sec["raw_size"]):
            return sec["raw_offset"] + (rva - sec["vrva"])
    return None

def read_bytes(f, sections, rva, length):
    off = rva_to_offset(sections, rva)
    if off is None:
        return None
    f.seek(off)
    return f.read(length)

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        # 1. Inspect ChrSet constructors around 0x0049272B and 0x00492BBB
        print("=== ChrSet Constructor 1 (around RVA 0x0049272B) ===")
        # Dump 0x00492700 - 0x00492800
        code1 = read_bytes(f, sections, 0x00492700, 256)
        if code1:
            for offset in range(0, len(code1), 16):
                chunk = code1[offset:offset+16]
                hex_str = " ".join(f"{b:02X}" for b in chunk)
                rva = 0x00492700 + offset
                print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

        print("\n=== ChrSet Constructor 2 (around RVA 0x00492BBB) ===")
        code2 = read_bytes(f, sections, 0x00492B90, 256)
        if code2:
            for offset in range(0, len(code2), 16):
                chunk = code2[offset:offset+16]
                hex_str = " ".join(f"{b:02X}" for b in chunk)
                rva = 0x00492B90 + offset
                print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

        # Search for direct CALLs to 0x00492700 or 0x00492B90/0x00492BA0
        # Let's find callers of these functions across .text sections
        print("\n=== Callers of ChrSet functions ===")
        targets = [0x00492700, 0x00492710, 0x00492720, 0x00492B90, 0x00492BA0]
        text_sections = [s for s in sections if s["name"].startswith(".text")]
        
        for sec in text_sections:
            f.seek(sec["raw_offset"])
            data = f.read(sec["raw_size"])
            sec_rva = sec["vrva"]
            for i in range(len(data) - 5):
                if data[i] == 0xE8: # call rel32
                    disp = struct.unpack("<i", data[i+1:i+5])[0]
                    dest_rva = (sec_rva + i + 5) + disp
                    for t in targets:
                        if abs(dest_rva - t) < 0x20:
                            call_rva = sec_rva + i
                            print(f"  Call from 0x{IMAGE_BASE + call_rva:016X} (RVA 0x{call_rva:08X}) to 0x{IMAGE_BASE + dest_rva:016X} (RVA 0x{dest_rva:08X})")

if __name__ == "__main__":
    main()
