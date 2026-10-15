#!/usr/bin/env python3
"""
Inspect WorldChrMan and ChrSet references in eldenring.exe (2.7.1.0).
Analyzes cross-references to WorldChrMan (RVA 0x3D69FF8) and ChrSet vtable (RVA 0x2A41348),
and inspects how character sets and enemy lists are referenced.
"""

import os
import struct
import sys

EXE_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe"
IMAGE_BASE = 0x140000000
WORLD_CHR_MAN_RVA = 0x3D69FF8
CHR_SET_VMT_RVA = 0x2A41348

def read_pe_sections(f):
    f.seek(0x3C)
    e_lfanew = struct.unpack("<I", f.read(4))[0]
    f.seek(e_lfanew)
    sig = f.read(4)
    if sig != b"PE\x00\x00":
        raise ValueError("Invalid PE signature")
    
    machine, num_sections = struct.unpack("<HH", f.read(4))
    f.seek(e_lfanew + 0x14) # SizeOfOptionalHeader offset
    size_of_opt = struct.unpack("<H", f.read(2))[0]
    
    opt_header_offset = e_lfanew + 0x18
    section_headers_offset = opt_header_offset + size_of_opt
    
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

def rva_to_file_offset(sections, rva):
    for sec in sections:
        if sec["vrva"] <= rva < sec["vrva"] + max(sec["vsize"], sec["raw_size"]):
            return sec["raw_offset"] + (rva - sec["vrva"])
    return None

def file_offset_to_rva(sections, offset):
    for sec in sections:
        if sec["raw_offset"] <= offset < sec["raw_offset"] + sec["raw_size"]:
            return sec["vrva"] + (offset - sec["raw_offset"])
    return None

def main():
    if not os.path.exists(EXE_PATH):
        print(f"Error: {EXE_PATH} does not exist")
        return 1

    file_size = os.path.getsize(EXE_PATH)
    print(f"Analyzing {EXE_PATH} (size: {file_size} bytes)")

    with open(EXE_PATH, "rb") as f:
        sections = read_pe_sections(f)
        print("PE Sections:")
        text_sections = []
        for s in sections:
            print(f"  {s['name']:10s} RVA: 0x{s['vrva']:08X} Size: 0x{s['vsize']:08X} FileOffset: 0x{s['raw_offset']:08X}")
            if s['name'].startswith(".text"):
                text_sections.append(s)

        world_va = IMAGE_BASE + WORLD_CHR_MAN_RVA
        print(f"\nWorldChrMan Global VA: 0x{world_va:016X} (RVA: 0x{WORLD_CHR_MAN_RVA:08X})")

        # Scan text sections for RIP-relative references to WorldChrMan (RVA 0x3D69FF8)
        print("\nSearching for RIP-relative references to WorldChrMan...")
        xrefs = []
        for sec in text_sections:
            f.seek(sec["raw_offset"])
            data = f.read(sec["raw_size"])
            sec_rva = sec["vrva"]
            
            # Instructions referencing WorldChrMan are typically:
            # 48 8B 05 [disp32]   (mov rax/rcx/..., [rip + disp32])
            # 48 8D 05 [disp32]   (lea rax/rcx/..., [rip + disp32])
            for i in range(len(data) - 7):
                # Check for 32-bit displacement relative to RIP (instruction end = rva + i + len)
                # Usually instruction length is 7 bytes: e.g. 48 8B 0D xx xx xx xx
                b0 = data[i]
                b1 = data[i+1]
                if b0 in (0x48, 0x4C) and b1 in (0x8B, 0x8D, 0x39, 0x3B):
                    disp = struct.unpack("<i", data[i+3:i+7])[0]
                    target_rva = (sec_rva + i + 7) + disp
                    if target_rva == WORLD_CHR_MAN_RVA:
                        ins_rva = sec_rva + i
                        xrefs.append((ins_rva, data[i:i+15]))

        print(f"Found {len(xrefs)} RIP-relative references to WorldChrMan in .text sections:")
        for rva, ins_bytes in xrefs[:30]:
            hex_str = " ".join(f"{b:02X}" for b in ins_bytes[:10])
            print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

        # Also search for references to ChrSet Vtable
        chr_set_vmt_va = IMAGE_BASE + CHR_SET_VMT_RVA
        print(f"\nChrSet Vtable VA: 0x{chr_set_vmt_va:016X} (RVA: 0x{CHR_SET_VMT_RVA:08X})")
        vmt_xrefs = []
        for sec in text_sections:
            f.seek(sec["raw_offset"])
            data = f.read(sec["raw_size"])
            sec_rva = sec["vrva"]
            for i in range(len(data) - 7):
                disp = struct.unpack("<i", data[i+3:i+7])[0]
                target_rva = (sec_rva + i + 7) + disp
                if target_rva == CHR_SET_VMT_RVA:
                    ins_rva = sec_rva + i
                    vmt_xrefs.append((ins_rva, data[i:i+15]))

        print(f"Found {len(vmt_xrefs)} RIP-relative references to ChrSet vtable:")
        for rva, ins_bytes in vmt_xrefs[:30]:
            hex_str = " ".join(f"{b:02X}" for b in ins_bytes[:10])
            print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
