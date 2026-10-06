"""Inspect Sekiro binary for memory patterns and structure offsets."""
from pathlib import Path
import struct


def find_pattern(data: bytes, pattern: bytes, mask: str) -> list[int]:
    matches = []
    p_len = len(pattern)
    for i in range(len(data) - p_len):
        match = True
        for j in range(p_len):
            if mask[j] == 'x' and data[i + j] != pattern[j]:
                match = False
                break
        if match:
            matches.append(i)
    return matches


def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro\sekiro.exe")
    if not exe_path.exists():
        print(f"Exe not found: {exe_path}")
        return

    print(f"Reading {exe_path}...")
    with open(exe_path, "rb") as f:
        data = f.read()

    print(f"File size: {len(data)} bytes")

    # Pattern for WorldChrMan in Sekiro 1.04-1.06:
    # 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 58 38
    p1 = bytes([0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x00, 0x48, 0x8B])
    m1 = "xxx????xxxx?xx"
    m = find_pattern(data, p1, m1)
    print(f"WorldChrMan pattern matches: {len(m)}")
    for off in m[:5]:
        rel = struct.unpack("<i", data[off + 3:off + 7])[0]
        # RIP relative offset: off + 7 + rel
        target_rva = off + 7 + rel
        print(f"  Match at file offset 0x{off:X}, RIP-rel RVA: 0x{target_rva:X}")


if __name__ == "__main__":
    main()
