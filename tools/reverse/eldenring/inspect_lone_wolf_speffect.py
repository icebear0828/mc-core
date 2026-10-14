#!/usr/bin/env python3
from pathlib import Path

speffect_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(speffect_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if "20297130" in line:
            print(line.strip())
