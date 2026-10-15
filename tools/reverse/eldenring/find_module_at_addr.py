from pathlib import Path
log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()
for l in lines[:200]:
    if "base" in l.lower() or "module" in l.lower() or "dinput8" in l.lower():
        print(l)

# Define Windows API
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

def find_eldenring_pid():
    import subprocess
    out = subprocess.check_output('tasklist /FI "IMAGENAME eq eldenring.exe" /FO CSV /NH', shell=True).decode('utf-8', errors='ignore')
    for line in out.strip().splitlines():
        parts = [p.strip(' "') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].lower() == "eldenring.exe":
            return int(parts[1])
    return None

pid = find_eldenring_pid()
if not pid:
    print("eldenring.exe not running")
    sys.exit(0)

h_proc = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
if not h_proc:
    print(f"Failed to open process {pid}")
    sys.exit(1)

target_addr = 0x00007FFF23340A5A

class MODULEINFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", wintypes.DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]

# EnumProcessModules
hMods = (wintypes.HMODULE * 1024)()
cbNeeded = wintypes.DWORD()
if psapi.EnumProcessModules(h_proc, hMods, ctypes.sizeof(hMods), ctypes.byref(cbNeeded)):
    n_mods = cbNeeded.value // ctypes.sizeof(wintypes.HMODULE)
    print(f"Total modules: {n_mods}")
    found = False
    for i in range(n_mods):
        mod = hMods[i]
        mod_name = (ctypes.c_char * 260)()
        psapi.GetModuleFileNameExA(h_proc, mod, mod_name, 260)
        info = MODULEINFO()
        psapi.GetModuleInformation(h_proc, mod, ctypes.byref(info), ctypes.sizeof(info))
        base = info.lpBaseOfDll
        size = info.SizeOfImage
        name = mod_name.value.decode('utf-8', errors='ignore')
        if base and base <= target_addr < base + size:
            print(f"MATCH: {name} (base 0x{base:X}, size 0x{size:X})")
            print(f"RVA: 0x{target_addr - base:X}")
            found = True
            break
        elif "dinput8" in name.lower() or "dxgi" in name.lower() or "d3d12" in name.lower():
            print(f"  {name}: base 0x{base:X}, size 0x{size:X}")
    if not found:
        print(f"No module contains 0x{target_addr:X}")
        # Query virtual memory
        class MEMORY_BASIC_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("BaseAddress", ctypes.c_void_p),
                ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD),
                ("RegionSize", ctypes.c_size_t),
                ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD),
                ("Type", wintypes.DWORD),
            ]
        mbi = MEMORY_BASIC_INFORMATION()
        if kernel32.VirtualQueryEx(h_proc, ctypes.c_void_p(target_addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            print(f"VirtualQuery: Base 0x{mbi.BaseAddress or 0:X}, AllocBase 0x{mbi.AllocationBase or 0:X}, State 0x{mbi.State:X}, Protect 0x{mbi.Protect:X}, Type 0x{mbi.Type:X}")

kernel32.CloseHandle(h_proc)
