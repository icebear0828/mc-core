#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    text_sec = None
    for s in pe.sections:
        if s.Name.startswith(b'.text'):
            text_sec = s
            break
            
    data = text_sec.get_data()
    base_va = text_sec.VirtualAddress
    
    # We want to search for vtable calls like:
    # call qword ptr [rax + 0x28] (v[5])
    # call qword ptr [rax + 0x30] (v[6])
    # call qword ptr [rax + 0x50] (v[10])
    
    # In x86_64: FF 50 28 (call [rax+0x28]), FF 50 30 (call [rax+0x30]), FF 50 50 (call [rax+0x50])
    # Also with other base registers: FF 51/52/53/56/57 ...
    print("Searching for indirect calls to vtable offsets 0x28, 0x30, 0x50...")
    
    patterns = {
        0x08: "v[1]",
        0x18: "v[3]",
        0x20: "v[4]",
        0x28: "v[5]",
        0x30: "v[6]",
        0x38: "v[7]",
        0x40: "v[8]",
        0x48: "v[9]",
        0x50: "v[10]",
    }
    
    found = 0
    for i in range(len(data) - 3):
        # Look for FF 50..57 / FF 90..97 / 41 FF 50..
        b0 = data[i]
        b1 = data[i+1]
        b2 = data[i+2]
        
        # FF 50+reg offset8
        if b0 == 0xFF and (b1 & 0xF8) == 0x50:
            off = b2
            if off in patterns:
                rva = base_va + i
                reg = b1 & 0x7
                print(f"RVA 0x{rva:X}: call [reg_{reg} + 0x{off:X}] ({patterns[off]})")
                found += 1
                if found >= 40:
                    break
                    
    print(f"Total matching calls found: {found}")

if __name__ == "__main__":
    main()
