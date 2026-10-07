"""Process memory helpers for runtime reverse engineering of a running game (Windows, stdlib only).

Everything here is read-only unless you call Proc.write(). Reads never raise: an unreadable range
returns None so scanners can walk pointer trees without guarding every access.
"""
from __future__ import annotations

import ctypes
import re
import struct
import subprocess
import sys

IS_WINDOWS = sys.platform == "win32"

PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_VM_OPERATION = 0x0008
PROCESS_QUERY_INFORMATION = 0x0400

if IS_WINDOWS:
    import ctypes.wintypes as wt

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wt.HANDLE
    k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]

    class MODULEENTRY32(ctypes.Structure):
        _fields_ = [
            ("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
            ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD),
            ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD), ("hModule", wt.HMODULE),
            ("szModule", ctypes.c_char * 256), ("szExePath", ctypes.c_char * 260),
        ]


def find_pid(name: str) -> int | None:
    """PID of the first process whose image name matches `name` (case-insensitive), else None."""
    out = subprocess.run(["tasklist", "/fi", f"imagename eq {name}", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == name.lower():
            return int(parts[1])
    return None


class Proc:
    """An open handle on the game process. Pass writable=True only when you intend to modify memory."""

    def __init__(self, pid: int, writable: bool = False):
        if not IS_WINDOWS:
            raise OSError("tools/reverse only works on Windows (it inspects a running Windows game)")
        self.pid = pid
        access = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
        if writable:
            access |= PROCESS_VM_WRITE | PROCESS_VM_OPERATION
        self.h = k32.OpenProcess(access, False, pid)
        if not self.h:
            raise OSError(f"OpenProcess failed: {ctypes.get_last_error()}")
        self.modules = self._modules()

    @classmethod
    def attach(cls, image_name: str, writable: bool = False) -> "Proc":
        pid = find_pid(image_name)
        if pid is None:
            raise SystemExit(f"{image_name} is not running (start the game and load into a world first)")
        return cls(pid, writable)

    def _modules(self) -> dict[str, tuple[int, int]]:
        snap = k32.CreateToolhelp32Snapshot(0x8 | 0x10, self.pid)  # SNAPMODULE | SNAPMODULE32
        entry = MODULEENTRY32()
        entry.dwSize = ctypes.sizeof(entry)
        mods: dict[str, tuple[int, int]] = {}
        ok = k32.Module32First(snap, ctypes.byref(entry))
        while ok:
            mods[entry.szModule.decode().lower()] = (entry.modBaseAddr, entry.modBaseSize)
            ok = k32.Module32Next(snap, ctypes.byref(entry))
        k32.CloseHandle(snap)
        return mods

    def main_module(self, image_name: str) -> tuple[int, int]:
        return self.modules[image_name.lower()]

    # ------------------------------------------------------------------ reading
    def read(self, addr: int, size: int) -> bytes | None:
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t(0)
        ok = k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
        if not ok and n.value == 0:
            return None
        return buf.raw[: n.value]

    def read_image(self, base: int, size: int, chunk: int = 1 << 20) -> tuple[bytes, int]:
        """Whole module image; unreadable chunks stay zero. Returns (bytes, number_of_unreadable_bytes)."""
        data = bytearray(size)
        bad = 0
        for off in range(0, size, chunk):
            n = min(chunk, size - off)
            b = self.read(base + off, n)
            if b is None:
                bad += n
                continue
            data[off : off + len(b)] = b
        return bytes(data), bad

    def u64(self, addr: int) -> int | None:
        b = self.read(addr, 8)
        return struct.unpack("<Q", b)[0] if b and len(b) == 8 else None

    def u32(self, addr: int) -> int | None:
        b = self.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def f32s(self, addr: int, n: int) -> list[float] | None:
        b = self.read(addr, 4 * n)
        return list(struct.unpack(f"<{n}f", b)) if b and len(b) == 4 * n else None

    def cstring(self, addr: int, limit: int = 128) -> str | None:
        raw = self.read(addr, limit)
        return raw.split(b"\0")[0].decode("ascii", "replace") if raw else None

    def chain(self, root: int, offsets: list[int]) -> int:
        """Follow pointers: addr = [root + o0]; addr = [addr + o1]; ... Returns 0 if any link is null/unreadable."""
        addr = root
        for off in offsets:
            addr = self.u64(addr + off) or 0
            if not addr:
                return 0
        return addr

    # ------------------------------------------------------------------ writing
    def write(self, addr: int, data: bytes) -> bool:
        n = ctypes.c_size_t(0)
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        ok = k32.WriteProcessMemory(self.h, ctypes.c_void_p(addr), buf, len(data), ctypes.byref(n))
        return bool(ok) and n.value == len(data)


def aob(pattern: str) -> "re.Pattern[bytes]":
    """'48 8B 05 ?? ?? ?? ??' -> compiled regex over raw bytes (? and ?? are wildcards)."""
    parts = pattern.split()
    return re.compile(b"".join(b"." if p in ("?", "??") else re.escape(bytes([int(p, 16)])) for p in parts), re.S)


def rip_target(image: bytes, match_offset: int, disp_offset: int = 3, instr_len: int = 7) -> int:
    """RVA a RIP-relative instruction (e.g. `mov rax,[rip+rel32]`) refers to."""
    rel = struct.unpack_from("<i", image, match_offset + disp_offset)[0]
    return match_offset + instr_len + rel
