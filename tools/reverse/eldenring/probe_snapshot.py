pid = pid_of(); P = Proc(pid); B = P.base
print("pid", pid, "base", hex(B))
S = {"WorldChrMan": (0x3D69FF8, ".?AVWorldChrManImp@CS@@"), "CSMenuMan": (0x3D6F820, ".?AVCSMenuManImp@CS@@"),
     "CSNowLoadingHelper": (0x3D64F28, ".?AVCSNowLoadingHelperImp@CS@@"), "CSFade": (0x3D763B0, ".?AVCSFadeImp@CS@@")}
inst = {}
for k, (rva, tn) in S.items():
    p = P.u64(B + rva); t = P.typename(p) if p else None
    inst[k] = p
    print(f"A3 {k:20s} ptr={p and hex(p)} type={t} {'OK' if t==tn else 'MISMATCH/NULL'}")
world = inst["WorldChrMan"]
if not world: print("not in world (WorldChrMan null)"); sys.exit()
player = P.u64(world + 0x1E508)
print("player", player and hex(player), P.typename(player))
def modules(chr_):
    c = P.u64(chr_ + 0x190); return c
def vitals(chr_):
    c = modules(chr_); 
    if not c: return None
    m = P.u64(c + 0)
    if not m: return None
    if P.typename(m) != ".?AVCSChrDataModule@CS@@" or P.u64(m + 8) != chr_: return None
    return P.i32(m + 0x138), P.i32(m + 0x13C), P.i32(m + 0x148), P.i32(m + 0x14C), P.i32(m + 0x154), P.i32(m + 0x158)
def phys(chr_):
    c = modules(chr_)
    if not c: return None
    m = P.u64(c + 0x0D * 8)
    if not m or P.typename(m) != ".?AVCSChrPhysicsModule@CS@@" or P.u64(m + 8) != chr_: return None
    return P.f32n(m + 0x70, 3), P.f32n(m + 0x50, 4), P.u8(m + 0x92), P.u8(m + 0x93), P.u8(m + 0x1D0), P.u8(m + 0x1D1)
if player:
    print("A5 vitals hp,maxhp,fp,maxfp,stam,maxstam =", vitals(player))
    pp = phys(player); print("   player phys pos/quat/ground flags:", pp)
    print("   player team", P.u8(player + 0x6C), "chr_type", P.u32(player + 0x68), "npc_id", P.i32(player + 0x64), "handle", hex(P.u64(player + 8) or 0))
# camera
cam = P.u64(world + 0x1ECE0)
if cam:
    print("A10 camera type", P.typename(cam))
    m = P.f32n(cam + 0x10, 16); fov = P.f32(cam + 0x50)
    print("   right", m[0:3], "up", m[4:7], "fwd", m[8:11], "pos", m[12:15], "fovY(rad)", fov)
# menu / loading / fade
mm = inst["CSMenuMan"]
if mm:
    print("B1 menu: +0x1A cursor_disabled =", P.u8(mm + 0x1A), " +0x1C =", hex(P.u8(mm + 0x1C) or 0), " +0x1D =", P.u8(mm + 0x1D), " popup +0x798 =", hex(P.u64(mm + 0x798) or 0), " +0x790 =", P.u8(mm + 0x790))
    ui = P.read(mm + 0x90, 0x47)
    print("   ui_states non-zero:", {hex(i): v for i, v in enumerate(ui) if v} if ui else None)
nl = inst["CSNowLoadingHelper"]
if nl:
    print("B3 loading: job +0xE0 =", hex(P.u64(nl + 0xE0) or 0), " countdown +0xE8 =", P.f32(nl + 0xE8), " counter +0x44 =", P.i32(nl + 0x44), " +0x5C =", P.i32(nl + 0x5C), " +0xD0 =", P.i32(nl + 0xD0), " +0xED =", P.u8(nl + 0xED))
fd = inst["CSFade"]
if fd:
    print("B5 fade: +0x58 =", P.u32(fd + 0x58), " +0x5C =", P.f32(fd + 0x5C))
    for i in range(9):
        pl = P.u64(fd + 0x10 + i * 8)
        print(f"   plate[{i}] ptr={pl and hex(pl)} rgba={P.f32n(pl + 0x10, 4) if pl else None} timer(+0x48)={P.f32(pl + 0x48) if pl else None} dur(+0x58)={P.f32(pl + 0x58) if pl else None}")
# enumerate
ppos = phys(player)[0] if player and phys(player) else None
matrix_base = B + 0x3B283F0
kinds = {B + 0x3B1C0C0: "HOSTILE", B + 0x3B1C0B8: "FRIEND", B + 0x3B1C0D0: "NEUTRAL", B + 0x3B1C0C8: "ALL"}
vtk = {0x3B1C0C0: 0x2A4F888, 0x3B1C0B8: 0x2A4F878, 0x3B1C0D0: 0x2A4F868, 0x3B1C0C8: 0x2A4F898}
okv = all(P.u64(B + r) == B + v for r, v in vtk.items())
print("A5/A7 relation singleton vtables ok:", okv)
def rel(a, b):
    if a >= 79 or b >= 79: return "?"
    e = P.u64(matrix_base + (a * 79 + b) * 8); return kinds.get(e, "?")
pteam = P.u8(player + 0x6C)
found = {}; per_set = {}
for s in range(196):
    sp = P.u64(world + 0x1DED8 + s * 8)
    if not sp: continue
    cap = P.u32(sp + 0x10); ent = P.u64(sp + 0x18)
    if not cap or not ent or cap > 4096: continue
    n = 0
    raw = P.read(ent, cap * 16)
    if not raw: per_set[s] = ("unreadable", cap); continue
    for i in range(cap):
        chr_, h = struct.unpack_from("<QQ", raw, i * 16)
        if not chr_ or chr_ == player or chr_ in found: continue
        tn = P.typename(chr_)
        if tn not in (".?AVEnemyIns@CS@@", ".?AVPlayerIns@CS@@"): continue
        npc = P.i32(chr_ + 0x64)
        if npc == 1000: continue
        v = vitals(chr_); ph = phys(chr_)
        if not v or not ph or v[0] <= 0: continue
        team = P.u8(chr_ + 0x6C)
        d = [ph[0][k] - ppos[k] for k in range(3)] if ppos else None
        found[chr_] = (s, tn.split("@")[0][4:], npc, team, v[0], v[1], d, rel(pteam, team))
        n += 1
    per_set[s] = (cap, n)
print("A6 sets with entries (set: (capacity, live_listed)):", {k: v for k, v in per_set.items() if v[1] if not isinstance(v[0], str)})
print("A6 total listed:", len(found))
from collections import Counter
print("   by team/relation:", Counter((v[3], v[7]) for v in found.values()))
near = sorted(found.items(), key=lambda kv: math.dist((0, 0, 0), kv[1][6]) if kv[1][6] else 1e9)[:12]
for chr_, (s, cls, npc, team, hp, mx, d, r) in near:
    print(f"   set{s:3d} {cls:9s} npc={npc:6d} team={team:2d} rel={r:8s} hp={hp}/{mx} dist={math.dist((0,0,0), d):.1f}m rel=({d[0]:.1f},{d[1]:.1f},{d[2]:.1f}) chr={hex(chr_)}")
