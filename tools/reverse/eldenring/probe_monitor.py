import sys
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
pid = pid_of(); P = Proc(pid); B = P.base
world = P.u64(B + 0x3D69FF8); mm = P.u64(B + 0x3D6F820); nl = P.u64(B + 0x3D64F28); fd = P.u64(B + 0x3D763B0)
movie_ptr = P.u64(B + 0x458B928)
print("monitor start; pid", pid, "movie singleton", movie_ptr and hex(movie_ptr), flush=True)
t0 = time.time(); last = {}
def snap():
    d = {}
    d["menu.1A"] = P.u8(mm + 0x1A); d["menu.1C"] = P.u8(mm + 0x1C); d["menu.1D"] = P.u8(mm + 0x1D)
    d["menu.popup"] = P.u64(mm + 0x798); d["menu.790"] = P.u8(mm + 0x790); d["menu.654C"] = P.u8(mm + 0x654C)
    ui = P.read(mm + 0x90, 0x47) or b"\0" * 0x47
    for i, v in enumerate(ui): d["ui[%#x]" % i] = v
    d["load.job"] = P.u64(nl + 0xE0); d["load.cnt44"] = P.i32(nl + 0x44); d["load.ED"] = P.u8(nl + 0xED)
    cd = P.f32(nl + 0xE8); d["load.cd"] = None if cd is None else round(cd, 2)
    for i in (7, 8):
        pl = P.u64(fd + 0x10 + i * 8)
        a = P.f32(pl + 0x1C) if pl else None
        d["fade.p%d.a" % i] = None if a is None else round(a, 2)
    d["movie.40"] = P.u64(movie_ptr + 0x40) if movie_ptr else None
    pl = P.u64(world + 0x1E508)
    if pl:
        c = P.u64(pl + 0x190); dm = P.u64(c); pm = P.u64(c + 0x68)
        d["hp"] = P.i32(dm + 0x138); d["fp"] = P.i32(dm + 0x148)
        d["g92"] = P.u8(pm + 0x92); d["g93"] = P.u8(pm + 0x93); d["fall1D0"] = P.u8(pm + 0x1D0); d["touch1D1"] = P.u8(pm + 0x1D1)
        pos = P.f32n(pm + 0x70, 3)
        d["posY"] = None if pos is None else round(pos[1], 1)
    else:
        d["player"] = None
    return d
while time.time() - t0 < DURATION:
    cur = snap()
    ch = {k: v for k, v in cur.items() if last.get(k) != v}
    if ch and last:
        print("%6.1fs" % (time.time() - t0), {k: (hex(v) if isinstance(v, int) and v > 0xFFFF else v) for k, v in ch.items()}, flush=True)
    elif not last:
        print("initial", {k: (hex(v) if isinstance(v, int) and v > 0xFFFF else v) for k, v in cur.items() if not k.startswith("ui[") or v}, flush=True)
    last = cur
    time.sleep(0.1)
print("monitor end", flush=True)
