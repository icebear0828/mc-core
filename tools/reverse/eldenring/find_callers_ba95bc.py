#!/usr/bin/env python3
"""Find all callers and xrefs to 0x140BA95BC in eldenring.exe."""
from pathlib import Path
import struct
import pefile

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    target_rva = 0x00BA95BC
    target_va = image_base + target_rva
    print(f"Target VA: 0x{target_va:016X}")

    with exe_path.open("rb") as f:
        data = f.read()

    # Search for direct relative calls: E8 xx xx xx xx
    # where rel32 = target_rva - (call_rva + 5)
    print("Searching for direct relative CALL/JMP...")
    for section in pe.sections:
        if section.Name.decode('utf-8', errors='replace').strip('\x00') == '.text':
            sec_start = section.PointerToRawData
            sec_size = section.SizeOfRawData
            sec_rva = section.VirtualAddress
            sec_bytes = data[sec_start:sec_start + sec_size]

            for i in range(len(sec_bytes) - 5):
                b = sec_bytes[i]
                if b in (0xE8, 0xE9):
                    rel = struct.unpack("<i", sec_bytes[i+1:i+5])[0]
                    curr_rva = sec_rva + i
                    dest_rva = curr_rva + 5 + rel
                    if dest_rva == target_rva:
                        call_type = "CALL" if b == 0xE8 else "JMP"
                        print(f"  Found {call_type} at RVA 0x{curr_rva:08X} (VA 0x{image_base + curr_rva:016X})")

    # Search for absolute pointers in .rdata (vtables)
    target_bytes = struct.pack("<Q", target_va)
    print("\nSearching for pointer xrefs in .rdata...")
    for section in pe.sections:
        sec_name = section.Name.decode('utf-8', errors='replace').strip('\x00')
        if sec_name in ('.rdata', '.data'):
            sec_start = section.PointerToRawData
            sec_size = section.SizeOfRawData
            sec_rva = section.VirtualAddress
            sec_bytes = data[sec_start:sec_start + sec_size]

            idx = 0
            while True:
                idx = sec_bytes.find(target_bytes, idx)
                if idx == -1:
                    break
                curr_rva = sec_rva + idx
                print(f"  Found pointer in {sec_name} at RVA 0x{curr_rva:08X} (VA 0x{image_base + curr_rva:016X})")
                idx += 8

if __name__ == "__main__":
    main()
