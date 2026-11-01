#!/usr/bin/env python3
"""
scan_shadow_pass.py
Searches eldenring.exe for shadow map render pass signatures, CSM (Cascaded Shadow Maps),
light constants, and render queue submissions.
"""
import pefile
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = None
    for s in pe.sections:
        if s.Name.startswith(b'.rdata'):
            rdata = s
            break
            
    data = rdata.get_data()
    base_va = rdata.VirtualAddress
    
    keywords = [
        b"Shadow",
        b"shadow",
        b"Cascade",
        b"CSM",
        b"DepthOnly",
        b"CapsuleShadow",
        b"ChrShadow",
        b"PlayerShadow",
        b"CSChrShadow",
    ]
    
    print(f"Scanning .rdata section (size {len(data)} bytes)...")
    matches = []
    for kw in keywords:
        start = 0
        while True:
            idx = data.find(kw, start)
            if idx == -1:
                break
            # Find null terminated string bounds
            s_start = idx
            while s_start > 0 and data[s_start - 1] != 0 and (idx - s_start) < 60:
                s_start -= 1
            s_end = idx
            while s_end < len(data) and data[s_end] != 0 and (s_end - idx) < 60:
                s_end += 1
            string_val = data[s_start:s_end].decode("ascii", errors="ignore")
            rva = base_va + s_start
            matches.append((rva, string_val))
            start = idx + len(kw)
            
    # Deduplicate matches
    seen = set()
    unique_matches = []
    for rva, s in matches:
        if s not in seen and len(s) >= 4:
            seen.add(s)
            unique_matches.append((rva, s))
            
    print(f"Found {len(unique_matches)} unique shadow-related strings:")
    for rva, s in unique_matches[:40]:
        print(f"  RVA 0x{rva:X}: {s}")

if __name__ == "__main__":
    main()
