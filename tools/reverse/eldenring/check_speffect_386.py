#!/usr/bin/env python3
from pathlib import Path

speffect_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(speffect_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        first = line.split()[0] if line.split() else ""
        if first == "386" or first == "0x182":
            print("FOUND 386:", line.strip())
        elif "phantom" in line.lower() and len(first) <= 4:
            print("PHANTOM:", line.strip())
