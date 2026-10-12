#!/usr/bin/env python3
"""
Inspect the latest lines of mc_er.log from the game directory to see what happened during the test.
"""

import sys
from pathlib import Path

def main() -> int:
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    log_file = game_dir / "mc_er.log"
    if not log_file.exists():
        print(f"[ERROR] Log file not found: {log_file}")
        return 1

    print(f"Log file: {log_file} (Size: {log_file.stat().st_size:,} bytes)")
    
    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    total_lines = len(lines)
    print(f"Total lines: {total_lines:,}")
    
    # Show last 100 lines
    last_n = min(100, total_lines)
    print(f"\n--- Last {last_n} lines of mc_er.log ---")
    for line in lines[-last_n:]:
        sys.stdout.write(line)

    return 0

if __name__ == "__main__":
    sys.exit(main())
