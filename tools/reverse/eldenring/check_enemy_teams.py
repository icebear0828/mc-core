#!/usr/bin/env python3
"""
check_enemy_teams.py
Search mc_er.log for enemy list, team values, or chr addresses.
"""
from pathlib import Path

log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()

# search for team in last 20000 lines
team_lines = [l for l in lines[-20000:] if "team" in l.lower() or "summon" in l.lower()]
print(f"Total matching lines in last 20000: {len(team_lines)}")
for l in team_lines[:40]:
    print(l)
