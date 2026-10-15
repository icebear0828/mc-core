#!/usr/bin/env python3
"""Check vtable pointer and calls to 0x140BA95C0 in eldenring.exe."""
from pathlib import Path
import pefile

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    with exe_path.open("rb") as f:
        data = f.read()

    target_va = 0x140000000 + 0xBA95C0
    target_bytes = target_va.to_bytes(8, "little")

    for s in pe.sections:
        name = s.Name.decode("latin1").strip("\x00")
        raw = data[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        pos = 0
        while True:
            pos = raw.find(target_bytes, pos)
            if pos == -1:
                break
            rva = s.VirtualAddress + pos
            print(f"Found function pointer in {name} at RVA 0x{rva:X} (VA 0x{0x140000000 + rva:X})")
            pos += 8

if __name__ == "__main__":
    main()
