#!/usr/bin/env python3
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
    rdata_bytes = rdata.get_data()
    rdata_rva = rdata.VirtualAddress
    
    # 0x2B76138 is a vtable entry. Let's find the vtable base and its COL!
    # A vtable is preceded by a pointer to COL (offset -8)
    # Let's inspect pointers around 0x2B76138
    idx = pe.get_offset_from_rva(0x2B76138 - 0x100)
    with open(EXE_PATH, "rb") as f:
        f.seek(idx)
        raw = f.read(0x200)
        
    ptrs = struct.unpack(f"<{len(raw)//8}Q", raw)
    print("Inspecting pointers around 0x2B76138:")
    for i, p in enumerate(ptrs):
        cur_rva = (0x2B76138 - 0x100) + i * 8
        prva = p - 0x140000000
        mark = "===>" if cur_rva == 0x2B76138 else "    "
        print(f" {mark} RVA 0x{cur_rva:X}: 0x{p:016X} (RVA 0x{prva:X})")
        
        # Check if p is a CompleteObjectLocator
        if 0x1000 <= prva < len(rdata_bytes) + rdata_rva:
            col_offset = pe.get_offset_from_rva(prva)
            with open(EXE_PATH, "rb") as f2:
                f2.seek(col_offset)
                col_raw = f2.read(24)
            if len(col_raw) == 24:
                sig, off, cdOff, td_rva = struct.unpack("<IIII", col_raw[:16])
                if sig == 1:
                    # Read TypeDescriptor name at td_rva + 0x10
                    td_offset = pe.get_offset_from_rva(td_rva + 0x10)
                    with open(EXE_PATH, "rb") as f3:
                        f3.seek(td_offset)
                        name_raw = f3.read(64)
                    name = name_raw.split(b"\x00")[0].decode("latin-1", errors="ignore")
                    print(f"       ===> COL! Class name: {name}")

if __name__ == "__main__":
    main()
