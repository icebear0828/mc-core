#!/usr/bin/env python3
"""
survey_coordinates.py
Inspect live Elden Ring process memory to verify:
1. Module base and WorldChrMan pointer.
2. PlayerIns and CSChrPhysicsModule.
3. Player position (+0x70, +0x80, +0x120), orientation (+0x50).
4. ChrCam pointer, camera matrix (+0x10), FOV (+0x50), position (+0x40).
5. Offset between camera position and player position.
6. World origin / map grid sector pointers.
"""

import sys
from pathlib import Path

# Add current dir to path to import probe_common
cur_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(cur_dir))

from probe_common import pid_of, Proc

def main():
    pid = pid_of()
    if not pid:
        print("[SURVEY] Elden Ring process (start_protected_game.exe / eldenring.exe) is NOT running.")
        return 0

    print(f"[SURVEY] Found Elden Ring PID: {pid}")
    try:
        P = Proc(pid)
    except Exception as e:
        print(f"[SURVEY] Failed to attach to process: {e}")
        return 1

    B = P.base
    print(f"[SURVEY] Base address: {hex(B)}")

    # 1. WorldChrMan
    world = P.u64(B + 0x3D69FF8)
    print(f"[SURVEY] WorldChrMan: {hex(world) if world else 'NULL'}")
    if not world:
        print("[SURVEY] Not currently in game world.")
        return 0

    # 2. Player
    player = P.u64(world + 0x1E508)
    print(f"[SURVEY] PlayerIns: {hex(player) if player else 'NULL'}")
    if player:
        c = P.u64(player + 0x190) # modules
        print(f"[SURVEY] Player ModuleContainer: {hex(c) if c else 'NULL'}")
        if c:
            pm = P.u64(c + 0x0D * 8) # CSChrPhysicsModule
            print(f"[SURVEY] CSChrPhysicsModule: {hex(pm) if pm else 'NULL'}")
            if pm:
                pos70 = P.f32n(pm + 0x70, 3)
                pos80 = P.f32n(pm + 0x80, 3)
                vel120 = P.f32n(pm + 0x120, 3)
                quat50 = P.f32n(pm + 0x50, 4)
                print(f"[SURVEY] Player Pos (+0x70): {pos70}")
                print(f"[SURVEY] Player Pos (+0x80): {pos80}")
                print(f"[SURVEY] Player Vel (+0x120): {vel120}")
                print(f"[SURVEY] Player Quat (+0x50): {quat50}")

    # 3. ChrCam
    cam = P.u64(world + 0x1ECE0)
    print(f"[SURVEY] ChrCam: {hex(cam) if cam else 'NULL'}")
    if cam:
        m = P.f32n(cam + 0x10, 16)
        fov = P.f32(cam + 0x50)
        right = m[0:3]
        up = m[4:7]
        fwd = m[8:11]
        cam_pos = m[12:15]
        print(f"[SURVEY] ChrCam Right: {right}")
        print(f"[SURVEY] ChrCam Up:    {up}")
        print(f"[SURVEY] ChrCam Fwd:   {fwd}")
        print(f"[SURVEY] ChrCam Pos (+0x40): {cam_pos}")
        print(f"[SURVEY] ChrCam FOV_Y: {fov}")

        if player and pm and pos70:
            dx = cam_pos[0] - pos70[0]
            dy = cam_pos[1] - pos70[1]
            dz = cam_pos[2] - pos70[2]
            print(f"[SURVEY] Delta (Cam - Player): ({dx:.3f}, {dy:.3f}, {dz:.3f}), dist={((dx**2+dy**2+dz**2)**0.5):.3f}m")

    # 4. MapStudio / FieldArea / World info
    # Let's inspect WorldChrMan surrounding fields
    field_area = P.u64(world + 0x1ECD0) # adjacent to cam
    print(f"[SURVEY] FieldArea / WorldSub at +0x1ECD0: {hex(field_area) if field_area else 'NULL'}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
