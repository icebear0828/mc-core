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
    # 0x1AA8FEA: lea rax, [rip + 0x217dbef] (-> 0x3C26BE0)
    # The function containing this is the constructor of GXFlverModelRenderer!
    # Let's find callers of this constructor!
    constructor_rva = 0x1AA8FDE
    print(f"GXFlverModelRenderer constructor at RVA 0x{constructor_rva:X}")
    
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    data = text.get_data()
    base_va = text.VirtualAddress
    
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    print("Searching for callers of GXFlverModelRenderer constructor...")
    for i in range(len(data) - 5):
        if data[i] == 0xE8:
            rel = struct.unpack_from("<i", data, i+1)[0]
            cur = base_va + i
            dest = cur + 5 + rel
            if dest == constructor_rva:
                print(f"Found call at RVA 0x{cur:X}")
                start_i = max(0, i - 20)
                insns = list(md.disasm(data[start_i:i+20], base_va + start_i))
                for ins in insns:
                    print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

if __name__ == "__main__":
    main()
