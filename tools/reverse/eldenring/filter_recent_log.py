#!/usr/bin/env python3
from pathlib import Path

log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()

# find lines matching keywords in the last 2000 lines
keywords = ["crash-veh", "exception 0x"]
matches = []
for l in lines:
    low = l.lower()
    for kw in keywords:
        if kw in low:
            matches.append(l)
            break

print(f"Found {len(matches)} crash lines:")
for m in matches[-30:]:
    print(m)
