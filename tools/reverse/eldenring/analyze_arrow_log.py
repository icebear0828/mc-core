#!/usr/bin/env python3
"""
Analyze bow, bullet fire, and projectile hit lines in mc_er.log.
"""

import re
import sys
from pathlib import Path

def main() -> int:
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    log_file = game_dir / "mc_er.log"
    if not log_file.exists():
        print(f"[ERROR] Log file not found: {log_file}")
        return 1

    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    keywords = ["bow:", "bullet fire:", "PROJ-HIT:", "ARROW:", "ARROW-TIMING", "PDC-STATS:"]
    matched = []
    for line in lines:
        if any(kw in line for kw in keywords):
            matched.append(line.strip())

    print(f"Total matched lines: {len(matched)}")
    for line in matched[-50:]:  # show last 50
        print(line)

    return 0

if __name__ == "__main__":
    sys.exit(main())
