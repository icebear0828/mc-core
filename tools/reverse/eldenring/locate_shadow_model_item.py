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
    # Let's inspect string literal at 0x2B76200: "CSFD4ShadowModelItem"
    # Search for TypeDescriptor matching CSFD4ShadowModelItem in .data!
    data_sec = [s for s in pe.sections if s.Name.startswith(b'.data')][0]
    data_bytes = data_sec.get_data()
    data_va = data_sec.VirtualAddress
    
    idx = data_bytes.find(b"CSFD4ShadowModelItem")
    if idx != -1:
        td_rva = data_va + idx - 0x10
        print(f"FOUND TypeDescriptor for CSFD4ShadowModelItem at RVA 0x{td_rva:X}")
        
        # Search for COL in .rdata
        rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
        rdata_bytes = rdata.get_data()
        rdata_va = rdata.VirtualAddress
        
        td_bytes = struct.pack("<I", td_rva)
        col_pos = rdata_bytes.find(td_bytes)
        if col_pos != -1:
            col_rva = rdata_va + col_pos - 0xC
            print(f"FOUND COL for CSFD4ShadowModelItem at RVA 0x{col_rva:X}")
            col_va = 0x140000000 + col_rva
            col_va_bytes = struct.pack("<Q", col_va)
            vt_pos = rdata_bytes.find(col_va_bytes)
            if vt_pos != -1:
                vt_rva = rdata_va + vt_pos + 8
                print(f"FOUND VTABLE for CSFD4ShadowModelItem at RVA 0x{vt_rva:X}")
                with open(EXE_PATH, "rb") as f:
                    f.seek(pe.get_offset_from_rva(vt_rva))
                    raw_vfs = f.read(16 * 8)
                vfs = struct.unpack(f"<{len(raw_vfs)//8}Q", raw_vfs)
                for i, vf in enumerate(vfs):
                    vrva = vf - 0x140000000
                    if 0x1000 <= vrva <= 0x3000000:
                        print(f"  v[{i}] = RVA 0x{vrva:X}")
                    else:
                        break

if __name__ == "__main__":
    main()
