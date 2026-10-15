#include "eldenring_diagnostics.hpp"

#include <share.h>
#include <cstdio>
#include <cstring>

namespace eldenring::diagnostics {

namespace {
std::mutex g_log_mutex;
FILE* g_log = nullptr;
std::string g_game_dir;
uint64_t g_start_ms = 0;
} // namespace

void VLog(const char* fmt, va_list args) {
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    std::lock_guard<std::mutex> g(g_log_mutex);
    if (!g_log) return;
#ifdef _WIN32
    const double elapsed = static_cast<double>(GetTickCount64() - g_start_ms) / 1000.0;
#else
    const double elapsed = 0.0;
#endif
    fprintf(g_log, "[%8.3f] %s\n", elapsed, msg);
    fflush(g_log);
}

void Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VLog(fmt, args);
    va_end(args);
}

#ifdef _WIN32
LONG WINAPI CrashVehHandler(PEXCEPTION_POINTERS ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Intercept fatal faults: Access Violation, illegal instruction, stack overflow, fastfail, abort
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW || code == 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN / __fastfail */ ||
        code == 0xE06D7363 /* C++ exception */) {
        const uintptr_t rip = ep->ContextRecord != nullptr ? static_cast<uintptr_t>(ep->ContextRecord->Rip) : 0;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        Log("CRASH-VEH: Exception 0x%08lX at addr %p (RVA 0x%llX, rip %p, tid %lu)",
            code, reinterpret_cast<void*>(addr),
            (addr >= base) ? static_cast<unsigned long long>(addr - base) : 0ULL,
            reinterpret_cast<void*>(rip), GetCurrentThreadId());
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR type = ep->ExceptionRecord->ExceptionInformation[0];
            const ULONG_PTR target = ep->ExceptionRecord->ExceptionInformation[1];
            Log("CRASH-VEH: Access Violation %s memory at %p",
                type == 0 ? "reading" : type == 1 ? "writing" : "executing",
                reinterpret_cast<void*>(target));
        }
        std::lock_guard<std::mutex> g(g_log_mutex);
        if (g_log) fflush(g_log);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void InitLogOnce() {
    static std::mutex m;
    static bool done = false;
    std::lock_guard<std::mutex> g(m);
    if (done) return;
    done = true;
#ifdef _WIN32
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of("\\/") + 1);
    g_start_ms = GetTickCount64();
    // _fsopen with _SH_DENYNO: other processes (the probes, `type`) can read the log while the game runs.
    g_log = _fsopen((g_game_dir + "mc_er.log").c_str(), "a", _SH_DENYNO);
    AddVectoredExceptionHandler(1, CrashVehHandler);
    Log("==== eldenring adapter loaded (pid %lu, exe %s) ====", GetCurrentProcessId(), exe);
    Log("build: OUR adapter (shadow, bolt F3, bow, crash VEH), compiled %s %s", __DATE__, __TIME__);
#endif
}

FILE* GetLogFile() {
    std::lock_guard<std::mutex> g(g_log_mutex);
    return g_log;
}

const std::string& GetGameDir() {
    return g_game_dir;
}

uint64_t GetStartTimeMs() {
    return g_start_ms;
}

} // namespace eldenring::diagnostics
