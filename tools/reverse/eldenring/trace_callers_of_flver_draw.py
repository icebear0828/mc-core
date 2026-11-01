#!/usr/bin/env python3
"""
trace_callers_of_1c400a0.py
Scan callers of GXFlverModelRendererDrawImplDefault::Draw (RVA 0x1C400A0 and 0x1C40230)
to find the exact character filtering condition!
"""
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
    
    # 0x1C400A0 vtable entry is called via vtable + 0x00
    # Also direct calls (E8 rel32)
    targets = [0x1C400A0, 0x1C40230, 0x1C402A0]
    print(f"Scanning for callers of FLVER draw methods in .text...")
    
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for i in range(len(data) - 5):
        if data[i] == 0xE8:
            rel = struct.unpack_from("<i", data, i+1)[0]
            cur_rva = base_va + i
            dest_rva = cur_rva + 5 + rel
            if dest_rva in targets:
                print(f"Direct call at RVA 0x{cur_rva:X} -> 0x{dest_rva:X}")
                # Disassemble 30 bytes before
                start_i = max(0, i - 30)
                insns = list(md.disasm(data[start_i:i+10], base_va + start_i))
                for ins in insns:
                    mark = "==>" if ins.address == cur_rva else "   "
                    print(f"  {mark} 0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

if __name__ == "__main__":
    main()
