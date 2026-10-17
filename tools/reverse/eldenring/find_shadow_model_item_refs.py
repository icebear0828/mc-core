#!/usr/bin/env python3
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=300, name=""):
    print(f"\n{'='*60}\n{name} at RVA 0x{rva:X}\n{'='*60}")
    offset = pe.get_offset_from_rva(rva)
    with open(EXE_PATH, "rb") as f:
        f.seek(offset)
        code = f.read(max_bytes)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for ins in md.disasm(code, rva):
        print(f"0x{ins.address:X}:  {ins.mnemonic:<8} {ins.op_str}")
        if ins.mnemonic == "ret":
            break

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    # Target RVA: 0x2B76200
    target_rva = 0x2B76200
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    data = text.get_data()
    base_va = text.VirtualAddress
    
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    print("Searching references to wide string L'CSFD4ShadowModelItem'...")
    for i in range(len(data) - 7):
        disp = struct.unpack_from("<i", data, i)[0]
        if (base_va + i + 4) + disp == target_rva:
            rva = base_va + i
            print(f"Found reference at RVA 0x{rva:X}")
            start_i = max(0, i - 15)
            insns = list(md.disasm(data[start_i:i+40], base_va + start_i))
            for ins in insns:
                print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

if __name__ == "__main__":
    main()
