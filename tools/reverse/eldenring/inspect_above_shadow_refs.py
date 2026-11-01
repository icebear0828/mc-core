#!/usr/bin/env python3
import pefile
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = None
    text = None
    for s in pe.sections:
        if s.Name.startswith(b'.rdata'):
            rdata = s
        elif s.Name.startswith(b'.text'):
            text = s
            
    rdata_bytes = rdata.get_data()
    rdata_va = rdata.VirtualAddress
    
    idx = rdata_bytes.find(b"CSChrAboveShadowTestModule")
    str_rva = rdata_va + idx
    print(f"String at RVA 0x{str_rva:X}")
    print(f"Surrounding 64 bytes: {rdata_bytes[idx-20:idx+40]}")
    
    # Check references to str_rva or text references
    import struct
    str_va = 0x140000000 + str_rva
    str_va_bytes = struct.pack("<Q", str_va)
    
    text_bytes = text.get_data()
    text_va = text.VirtualAddress
    
    # Search for rip-relative references in .text
    print("Searching for RIP-relative references in .text...")
    refs = []
    for i in range(len(text_bytes) - 7):
        # 4-byte displacement
        disp = struct.unpack_from("<i", text_bytes, i)[0]
        target_rva = (text_va + i + 4) + disp
        if target_rva == str_rva:
            refs.append(text_va + i)
            
    print(f"Found {len(refs)} references to string:")
    for r in refs:
        print(f"  Ref at RVA 0x{r:X}")

if __name__ == "__main__":
    main()
