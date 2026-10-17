#!/usr/bin/env python3
import pefile
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    data_sec = None
    for s in pe.sections:
        if s.Name.startswith(b'.data'):
            data_sec = s
            break
            
    data = data_sec.get_data()
    base_va = data_sec.VirtualAddress
    
    print(f"Scanning .data section for RTTI names (size {len(data)} bytes)...")
    pos = 0
    while True:
        idx = data.find(b".?AV", pos)
        if idx == -1: break
        end = data.find(b"\x00", idx)
        if end != -1:
            name = data[idx:end].decode("latin-1", errors="ignore")
            nl = name.lower()
            if any(k in nl for k in ["shadow", "light", "csm", "cascade", "render", "capsule", "draw"]):
                rva = base_va + idx
                print(f"  RVA 0x{rva:08X}: {name}")
        pos = idx + 4

if __name__ == "__main__":
    main()
