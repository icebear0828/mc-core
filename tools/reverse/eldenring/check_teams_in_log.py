#!/usr/bin/env python3
"""
Search mc_er.log for all team numbers that appeared in list or hit logs.
"""

import re

LOG_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log"

def main():
    with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    teams_hit = set()
    teams_f8 = set()
    
    for l in lines:
        m = re.search(r"team=(\d+)", l)
        if m:
            teams_hit.add(int(m.group(1)))
        m2 = re.search(r"F8: .* team=(\d+)", l)
        if m2:
            teams_f8.add(int(m2.group(1)))

    print(f"Teams in log: {sorted(list(teams_hit))}")
    print(f"Teams targeted by F8: {sorted(list(teams_f8))}")

if __name__ == "__main__":
    main()
