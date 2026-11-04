#!/usr/bin/env python3
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
    
    # Search for all strings starting with .?AVCS and containing Shadow
    print("Searching for RTTI class names matching CS...Shadow...")
    start = 0
    while True:
        idx = data.find(b".?AVCS", start)
        if idx == -1: break
        end = data.find(b"\x00", idx)
        if end != -1:
            s = data[idx:end].decode("ascii", errors="ignore")
            if any(k in s.lower() for k in ["shadow", "csm", "cascade", "light", "render"]):
                rva = base_va + idx
                print(f"RVA 0x{rva:X}: {s}")
        start = idx + 6

if __name__ == "__main__":
    main()
