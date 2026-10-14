#!/usr/bin/env python3
from pathlib import Path

speffect_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\SpEffectParam.txt")
with open(speffect_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if line.startswith("20297") or line.startswith("202000") or "phantom" in line.lower() or "spirit" in line.lower():
            if any(term in line.lower() for term in ["lone wolf", "kaiden", "phantom", "spirit param", "ash"]):
                print(line.strip())
