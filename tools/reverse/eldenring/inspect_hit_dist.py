#!/usr/bin/env python3
"""
Calculate exact distance and timing for the arrow hit in mc_er.log.
"""

import math
import sys
from pathlib import Path

def main():
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    log_file = game_dir / "mc_er.log"
    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        if "ARROW: the player's shot hit" in line and "169.937" in line:
            print("Found hit line at index", i)
            # print surrounding lines
            for j in range(max(0, i - 15), min(len(lines), i + 5)):
                print(lines[j].strip())

if __name__ == "__main__":
    main()
