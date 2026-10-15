#!/usr/bin/env python3
"""Find function start and callers for crash address RVA 0xBA965B."""
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    crash_rva = 0xBA965B

    # Read binary back up to 2KB
    offset = pe.get_offset_from_rva(crash_rva)
    back_len = 0x800
    start_offset = max(0, offset - back_len)

    with exe_path.open("rb") as f:
        f.seek(start_offset)
        code = f.read(back_len + 0x200)

    # Search backwards for typical function entry (e.g. 0xCC, 0xCC followed by push/mov/sub rsp)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    instructions = list(md.disasm(code, image_base + crash_rva - back_len))

    # Find the function entry before crash_rva
    fn_start = None
    for i, ins in enumerate(instructions):
        rva = ins.address - image_base
        # look for sub rsp or push rbp after int3 or ret
        if i > 0 and instructions[i-1].mnemonic in ('ret', 'int3'):
            if ins.address <= image_base + crash_rva:
                fn_start = ins.address

    if fn_start:
        fn_rva = fn_start - image_base
        print(f"Likely Function Start: 0x{fn_start:016X} (RVA 0x{fn_rva:08X})")
    else:
        print("Function start not definitively found in backward scan")

    # Let's inspect what thread was running at the crash:
    # Look at the log: tid was 45516.
    print(f"Crash happened at RVA 0x{crash_rva:08X}")

if __name__ == "__main__":
    main()
