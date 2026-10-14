#!/usr/bin/env python3
"""Hex dump bytes around 0xBA95BC to find true function boundary."""
from pathlib import Path
import pefile

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    offset = pe.get_offset_from_rva(0xBA95A0)
    with exe_path.open("rb") as f:
        f.seek(offset)
        raw = f.read(64)

    for i in range(0, len(raw), 16):
        chunk = raw[i:i+16]
        hex_str = " ".join(f"{b:02X}" for b in chunk)
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        rva = 0xBA95A0 + i
        print(f"RVA 0x{rva:08X}: {hex_str:48}  {ascii_str}")

if __name__ == "__main__":
    main()
