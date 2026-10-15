#!/usr/bin/env python3
from pathlib import Path

speffect_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(speffect_path, "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

print(f"Total SpEffectParam lines: {len(lines)}")
keywords = ["spirit", "buddy", "phantom", "summon", "ghost", "wolf", "kaiden"]

for line in lines:
    low = line.lower()
    for kw in keywords:
        if kw in low:
            print(line.strip())
            break
