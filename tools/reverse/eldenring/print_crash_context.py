#!/usr/bin/env python3
"""Print last 50 lines before the crash from mc_er.log safely."""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    with log_path.open("rb") as f:
        f.seek(max(0, log_path.stat().st_size - 32768))
        chunk = f.read()

    lines = chunk.decode("utf-8", errors="replace").splitlines()
    for l in lines[-50:]:
        # sanitize for console
        safe_line = l.encode("ascii", errors="backslashreplace").decode("ascii")
        print(safe_line)

if __name__ == "__main__":
    main()
