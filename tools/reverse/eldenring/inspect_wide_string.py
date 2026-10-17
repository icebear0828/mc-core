#!/usr/bin/env python3
import pefile
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
    raw = rdata.get_data()
    # print characters at RVA 0x2B76200
    off = pe.get_offset_from_rva(0x2B76200)
    with open(EXE_PATH, "rb") as f:
        f.seek(off)
        data = f.read(128)
    print("Hex around 0x2B76200:")
    print(data.hex())
    print("Decoded utf-16le:")
    print(data.decode("utf-16le", errors="ignore"))

if __name__ == "__main__":
    main()
