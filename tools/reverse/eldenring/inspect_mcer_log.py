#!/usr/bin/env python3
"""
Inspect mc_er.log from Elden Ring game folder to see:
1. How many enemies are enumerated in the latest session.
2. What happened during click: no target vs click: ok.
3. Why enemies were not picked or if they were missing.
"""

import os

LOG_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log"

def main():
    if not os.path.exists(LOG_PATH):
        print(f"Log not found: {LOG_PATH}")
        return

    with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    print(f"Total lines in mc_er.log: {len(lines)}")
    
    # Split into sessions (find last "build: OUR adapter" or "=====" or restart)
    session_starts = []
    for i, line in enumerate(lines):
        if "build: OUR adapter" in line or "adapter loaded" in line or "InitHooks" in line:
            session_starts.append(i)

    print(f"Found {len(session_starts)} session starts.")
    last_session_start = session_starts[-1] if session_starts else 0
    print(f"Analyzing last session from line {last_session_start} to {len(lines)}...")
    
    session_lines = lines[last_session_start:]
    
    click_no_target = [l.strip() for l in session_lines if "click: no target" in l]
    click_ok = [l.strip() for l in session_lines if "click: item=" in l]
    damage_lines = [l.strip() for l in session_lines if "DAMAGE:" in l]
    
    print(f"\nLast session stats:")
    print(f"  click: no target count: {len(click_no_target)}")
    print(f"  click: queued/ok count: {len(click_ok)}")
    print(f"  DAMAGE lines count: {len(damage_lines)}")
    
    print("\nSample 'click: no target' (last 10):")
    for l in click_no_target[-10:]:
        print(f"  {l}")

    print("\nSample 'click: item=' (last 10):")
    for l in click_ok[-10:]:
        print(f"  {l}")

if __name__ == "__main__":
    main()
