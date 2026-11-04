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
    
    # We want calls specifically for offset 0x50 (v[10])
    # 0xFF 0x50..57 0x50 (call [reg + 0x50])
    # or 0xFF 0x90..97 0x50 0x00 0x00 0x00
    # or 41 FF 50..57 0x50
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    print("Searching for v[10] calls (offset 0x50)...")
    for i in range(len(data) - 4):
        b0 = data[i]
        b1 = data[i+1]
        b2 = data[i+2]
        if b0 == 0xFF and (b1 & 0xF8) == 0x50 and b2 == 0x50:
            rva = base_va + i
            print(f"Found v[10] call at RVA 0x{rva:X}")
            # disasm 30 bytes before and 15 bytes after
            start_i = max(0, i - 25)
            insns = list(md.disasm(data[start_i:i+15], base_va + start_i))
            for ins in insns:
                mark = "==>" if ins.address == rva else "   "
                print(f"  {mark} 0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

if __name__ == "__main__":
    main()
