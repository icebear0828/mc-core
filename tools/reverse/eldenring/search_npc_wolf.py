#!/usr/bin/env python3
from pathlib import Path

npc_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\NpcParam.txt")
with open(npc_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        low = line.lower()
        if "wolf" in low or "23200000" in low or "lone" in low:
            print(line.strip())
