#!/usr/bin/env python3
"""Search thread creation or activity in mc_er.log."""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    with log_path.open("rb") as f:
        # Search backwards 10MB
        f.seek(max(0, log_path.stat().st_size - 10 * 1024 * 1024))
        data = f.read()

    lines = data.decode("utf-8", errors="replace").splitlines()
    # Find session start
    session_start_idx = 0
    for i, l in enumerate(lines):
        if "build: OUR adapter" in l or "=== Elden Ring" in l:
            session_start_idx = i

    print(f"Session starts at line {session_start_idx} (of {len(lines)}):")
    if session_start_idx < len(lines):
        print(lines[session_start_idx])

    # Search for tid 45516 or thread logs
    for l in lines[session_start_idx:]:
        if "45516" in l or "thread" in l.lower() or "CRASH" in l:
            print(l[:160])

if __name__ == "__main__":
    main()
