#!/usr/bin/env python3
from pathlib import Path

game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
log_path = game_dir / "mc_er.log"

with open(log_path, "r", encoding="utf-8", errors="replace") as f:
    for i in range(100):
        line = f.readline()
        if not line:
            break
        print(line, end="")
