#!/usr/bin/env python3
from pathlib import Path

# search for SpEffectParam in Seep-Reverse-Lab or universal-modder
p = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if line.startswith("386 ") or line.startswith("386\t"):
            print("Row 386:", line.strip())
