#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    data = text.get_data()
    base_va = text.VirtualAddress
    
    # We want references to vtable 0x2F1EF78 (GXFlverModelRendererDrawImplDefault)
    # and 0x2F1F1B0 (GXFlverModelRenderer)
    vt1 = 0x2F1EF78
    vt2 = 0x2F1F1B0
    
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    print("Searching for constructor references to vtables...")
    for i in range(len(data) - 7):
        disp = struct.unpack_from("<i", data, i)[0]
        target = (base_va + i + 4) + disp
        if target in (vt1, vt2):
            rva = base_va + i
            print(f"Found reference at RVA 0x{rva:X} -> 0x{target:X}")
            start_i = max(0, i - 15)
            insns = list(md.disasm(data[start_i:i+30], base_va + start_i))
            for ins in insns:
                print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

if __name__ == "__main__":
    main()
