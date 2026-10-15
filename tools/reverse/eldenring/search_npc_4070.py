#!/usr/bin/env python3
from pathlib import Path

npc_path = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache\NpcParam.txt")
with open(npc_path, "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if line.startswith("4070") or line.startswith("4050") or "lone wolf" in line.lower() or "kaiden" in line.lower():
            print(line.strip())
