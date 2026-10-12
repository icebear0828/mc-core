#!/usr/bin/env python3
"""
Scan mc_er.log across all sessions for:
1. When enemies count was unexpectedly 0 or small.
2. What npc_id / teams have been encountered.
3. Cases where click: no target occurred while player was close to an enemy.
"""

import re

LOG_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log"

def main():
    with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    print(f"Total lines: {len(lines)}")
    
    no_target_dist = []
    enemy_stats = []
    
    for l in lines:
        if "click: no target" in l:
            # Check detail buffer
            m = re.findall(r"dist ([\d\.]+) t ([\d\.]+) off-ray ([\d\.]+)", l)
            if m:
                for dist, t, off_ray in m:
                    no_target_dist.append((float(dist), float(t), float(off_ray), l.strip()))
            elif "enemies<=8m: [ ]" in l:
                no_target_dist.append((999.0, 999.0, 999.0, l.strip()))
                
        if "enemies=" in l and "hostile=" in l:
            m = re.search(r"enemies=(\d+)\s+hostile=(\d+)\s+nearest=([\d\.]+)", l)
            if m:
                enemy_stats.append((int(m.group(1)), int(m.group(2)), float(m.group(3))))

    print(f"Total 'click: no target' occurrences: {len(no_target_dist)}")
    empty_8m = [x for x in no_target_dist if x[0] == 999.0]
    near_8m = [x for x in no_target_dist if x[0] <= 8.0]
    in_reach_off_ray = [x for x in near_8m if x[0] <= 4.0 and x[2] > 0.9]
    out_of_reach = [x for x in near_8m if x[0] > 4.0]
    
    print(f"  enemies<=8m empty: {len(empty_8m)}")
    print(f"  enemies within 8m: {len(near_8m)}")
    print(f"    in reach (dist<=4m) but off-ray > 0.9m: {len(in_reach_off_ray)}")
    print(f"    out of reach (4m < dist <= 8m): {len(out_of_reach)}")

    print("\nSample in reach (dist<=4m) but off-ray > 0.9m (first 10):")
    for dist, t, off_ray, l in in_reach_off_ray[:10]:
        print(f"  dist={dist:.1f} t={t:.1f} off-ray={off_ray:.1f} | {l}")

    print("\nSample out of reach (4.0m < dist <= 5.0m) (first 10):")
    close_out_of_reach = [x for x in out_of_reach if x[0] <= 5.0]
    for dist, t, off_ray, l in close_out_of_reach[:10]:
        print(f"  dist={dist:.1f} t={t:.1f} off-ray={off_ray:.1f} | {l}")

if __name__ == "__main__":
    main()
