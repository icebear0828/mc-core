#!/usr/bin/env python3
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    target_va = 0x140000000 + 0x2B76200
    target_bytes = struct.pack("<Q", target_va)
    for s in pe.sections:
        data = s.get_data()
        pos = 0
        while True:
            idx = data.find(target_bytes, pos)
            if idx == -1: break
            rva = s.VirtualAddress + idx
            sec = s.Name.decode("ascii", errors="ignore").strip("\x00")
            print(f"Absolute pointer in {sec} at RVA 0x{rva:X}")
            pos = idx + 8

if __name__ == "__main__":
    main()
