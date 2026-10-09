import ctypes, ctypes.wintypes as wt, struct, sys, time, math
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x10; PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPMODULE = 0x8; TH32CS_SNAPMODULE32 = 0x10
class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD), ("GlblcntUsage", wt.DWORD),
                ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD), ("hModule", wt.HMODULE),
                ("szModule", wt.WCHAR * 256), ("szExePath", wt.WCHAR * 260)]
k32.OpenProcess.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
def find_base(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    me = MODULEENTRY32W(); me.dwSize = ctypes.sizeof(me)
    base = None
    ok = k32.Module32FirstW(snap, ctypes.byref(me))
    while ok:
        if me.szModule.lower() in ("start_protected_game.exe", "eldenring.exe"):
            base = me.modBaseAddr; break
        ok = k32.Module32NextW(snap, ctypes.byref(me))
    k32.CloseHandle(snap)
    return base
class Proc:
    def __init__(self, pid):
        self.h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h: raise RuntimeError("OpenProcess failed %d" % ctypes.get_last_error())
        self.base = find_base(pid)
        if not self.base: raise RuntimeError("module base not found")
    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            return None
        return buf.raw
    def u64(self, a):
        b = self.read(a, 8); return struct.unpack("<Q", b)[0] if b else None
    def u32(self, a):
        b = self.read(a, 4); return struct.unpack("<I", b)[0] if b else None
    def i32(self, a):
        b = self.read(a, 4); return struct.unpack("<i", b)[0] if b else None
    def u8(self, a):
        b = self.read(a, 1); return b[0] if b else None
    def f32(self, a):
        b = self.read(a, 4); return struct.unpack("<f", b)[0] if b else None
    def f32n(self, a, n):
        b = self.read(a, 4 * n); return struct.unpack("<%df" % n, b) if b else None
    def typename(self, obj):
        if not obj: return None
        vt = self.u64(obj)
        if not vt: return None
        col = self.u64(vt - 8)
        if not col: return None
        if self.u32(col) != 1: return None
        td = self.u32(col + 0xC)
        if not td: return None
        out = b""
        for i in range(160):
            c = self.read(self.base + td + 0x10 + i, 1)
            if not c: return None
            if c == b"\0": break
            out += c
        return out.decode("latin1")
def pid_of():
    import subprocess
    o = subprocess.run(["tasklist", "/fi", "imagename eq start_protected_game.exe", "/fo", "csv", "/nh"], capture_output=True).stdout.decode("gbk", "ignore")
    for line in o.splitlines():
        p = line.replace('"', '').split(",")
        if len(p) > 1 and p[0].lower().startswith("start_protected_game"): return int(p[1])
    return None
