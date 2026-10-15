#!/usr/bin/env python3
"""
Inspect mc_er.log lines around [ 130.000 ] - [ 140.000 ] and [ 645.000 ] - [ 655.000 ].
"""

LOG_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log"

def main():
    with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    # Find the last session start
    session_starts = [i for i, l in enumerate(lines) if "build: OUR adapter" in l or "adapter loaded" in l]
    start_idx = session_starts[-1] if session_starts else 0
    
    session_lines = lines[start_idx:]
    print(f"Total session lines: {len(session_lines)}")
    
    # Print lines around 130-140 and 645-655
    print("=== Lines around 130s - 140s ===")
    for l in session_lines:
        if any(f"[ 13{i}." in l for i in range(10)) or any(f"[ 140." in l for i in range(1)):
            print(l.strip())

    print("\n=== Lines around 645s - 652s ===")
    for l in session_lines:
        if any(f"[ 64{i}." in l for i in range(5, 10)) or any(f"[ 65{i}." in l for i in range(3)):
            print(l.strip())

if __name__ == "__main__":
    main()
