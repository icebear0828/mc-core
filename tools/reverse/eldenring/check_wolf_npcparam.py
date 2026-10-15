#!/usr/bin/env python3
from pathlib import Path

npc_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\NpcParam.txt")
with open(npc_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if line.startswith("140700000"):
            print("LONE WOLF:", line.strip())
        elif line.startswith("40700000"):
            print("REGULAR WOLF:", line.strip())
