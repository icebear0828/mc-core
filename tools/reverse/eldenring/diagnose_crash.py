#!/usr/bin/env python3
"""
Diagnostic script to collect the latest crash event and mod log from Elden Ring.
"""

import os
import subprocess
import sys
from pathlib import Path

def get_last_mod_log():
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    log_path = game_dir / "mc_er.log"
    print(f"=== Reading Mod Log: {log_path} ===")
    if not log_path.exists():
        print("mc_er.log does not exist.")
        return
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    print(f"Total lines: {len(lines)}")
    print("--- Last 40 lines ---")
    for line in lines[-40:]:
        print(line, end="")
    print("\n---------------------")

def get_event_log():
    print("=== Querying Windows Application Event Log ===")
    ps_cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", str(Path(__file__).parent / "get_crash_event.ps1")
    ]
    try:
        res = subprocess.run(ps_cmd, capture_output=True, text=True, check=False)
        print(res.stdout)
        if res.stderr:
            print("[stderr]", res.stderr)
    except Exception as e:
        print(f"Error querying event log: {e}")

if __name__ == "__main__":
    get_last_mod_log()
    get_event_log()
