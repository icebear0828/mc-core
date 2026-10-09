import sys
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 600.0
u32 = ctypes.WinDLL("user32")
pid = pid_of(); P = Proc(pid); B = P.base
world = P.u64(B + 0x3D69FF8); mm = P.u64(B + 0x3D6F820); nl = P.u64(B + 0x3D64F28); fd = P.u64(B + 0x3D763B0)
movie = P.u64(B + 0x458B928)
print("monitor2 start; pid", pid, flush=True)
t0 = time.time(); last = {}; mark = 0; keyprev = False
def snap():
    d = {}
    for off in (0x1A, 0x1C, 0x1D, 0x654C): d["menu.%X" % off] = P.u8(mm + off)
    d["popup"] = P.u64(mm + 0x798)
    ui = P.read(mm + 0x90, 0x47) or b"\0" * 0x47
    for i, v in enumerate(ui): d["ui[%#x]" % i] = v
    d["load.job"] = P.u64(nl + 0xE0); d["load.44"] = P.i32(nl + 0x44); d["load.5C"] = P.i32(nl + 0x5C); d["load.D0"] = P.i32(nl + 0xD0)
    d["load.EC"] = P.u8(nl + 0xEC); d["load.ED"] = P.u8(nl + 0xED)
    cd = P.f32(nl + 0xE8); d["load.cd"] = None if cd is None else round(cd, 1)
    for i in range(9):
        pl = P.u64(fd + 0x10 + i * 8); a = P.f32(pl + 0x1C) if pl else None
        d["fade.a%d" % i] = None if a is None else round(a, 2)
    d["fade.58"] = P.u32(fd + 0x58)
    d["movie.40"] = P.u64(movie + 0x40) if movie else None
    pl = P.u64(world + 0x1E508)
    if pl:
        c = P.u64(pl + 0x190); dm = P.u64(c); pm = P.u64(c + 0x68)
        d["hp"] = P.i32(dm + 0x138); d["g92"] = P.u8(pm + 0x92); d["g93"] = P.u8(pm + 0x93); d["f1D0"] = P.u8(pm + 0x1D0); d["t1D1"] = P.u8(pm + 0x1D1)
    return d
def fmt(v): return hex(v) if isinstance(v, int) and v > 0xFFFF else v
while time.time() - t0 < DURATION:
    down = bool(u32.GetAsyncKeyState(0x78) & 0x8000)  # F9
    if down and not keyprev:
        mark += 1; print("%6.1fs ===== MARK %d =====" % (time.time() - t0, mark), flush=True)
    keyprev = down
    cur = snap()
    ch = {k: v for k, v in cur.items() if last.get(k) != v}
    if not last:
        print("initial", {k: fmt(v) for k, v in cur.items() if not (k.startswith("ui[") and v == 0)}, flush=True)
    elif ch:
        print("%6.1fs" % (time.time() - t0), {k: fmt(v) for k, v in ch.items()}, flush=True)
    last = cur
    time.sleep(0.1)
print("monitor2 end", flush=True)
