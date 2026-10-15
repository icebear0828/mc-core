#!/usr/bin/env python3
from pathlib import Path

p = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(p, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        val = line.split()[0] if line.split() else ""
        if val.isdigit() and 380 <= int(val) <= 400:
            print(line.strip())
