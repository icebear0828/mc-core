#!/usr/bin/env python3
"""
trace_flver_submit.py
Disassemble GXFlverModelRenderer (RVA 0x2F1F1B0) and IGXFlverModelRendererDrawImpl (RVA 0x3D2F288).
Find the actual draw call submission loop that iterates over characters and draws their FLVER models into shadow map!
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def disasm_func(pe, rva, max_bytes=400, name=""):
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
    # Let's inspect GXFlverModelRenderer v[2] (0x1AAB1F0) and v[4] (0x1AAA760)
    disasm_func(pe, 0x1AAB1F0, max_bytes=250, name="GXFlverModelRenderer v[2]")
    disasm_func(pe, 0x1AAA760, max_bytes=250, name="GXFlverModelRenderer v[4]")
    
    # Also find GXFlverModelRendererDrawImplDefault vtable
    # TypeDescriptor RVA: 0x03D2F2D0 - 0x10
    rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
    rdata_bytes = rdata.get_data()
    rdata_rva = rdata.VirtualAddress
    
    td_rva = 0x03D2F2D0 - 0x10
    td_bytes = struct.pack("<I", td_rva)
    pos = rdata_bytes.find(td_bytes)
    if pos != -1:
        col_rva = rdata_rva + pos - 0xC
        col_va = 0x140000000 + col_rva
        col_va_bytes = struct.pack("<Q", col_va)
        vt_pos = rdata_bytes.find(col_va_bytes)
        if vt_pos != -1:
            vt_rva = rdata_rva + vt_pos + 8
            print(f"FOUND VTABLE for GXFlverModelRendererDrawImplDefault at RVA 0x{vt_rva:X}")
            with open(EXE_PATH, "rb") as f:
                f.seek(pe.get_offset_from_rva(vt_rva))
                raw = f.read(10 * 8)
            vfs = struct.unpack(f"<{len(raw)//8}Q", raw)
            for i, vf in enumerate(vfs):
                vrva = vf - 0x140000000
                if 0x1000 <= vrva <= 0x3000000:
                    print(f"  v[{i}] = RVA 0x{vrva:X}")
                    disasm_func(pe, vrva, max_bytes=250, name=f"ImplDefault v[{i}]")

if __name__ == "__main__":
    main()
