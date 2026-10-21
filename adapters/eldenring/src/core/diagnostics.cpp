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
    // Intercept fatal faults only: Access Violation, illegal instruction, stack overflow, fastfail, abort.
    // Do NOT log 0xE06D7363 (C++ exception): the game uses handled C++ exceptions in normal control flow
    // (e.g. while unfocused/loading), and a VEH sees every one of them. Logging each one with a register +
    // stack dump floods mc_er.log (24k lines in 24 s in the 2026-10-21 run) and stalls the game.
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW || code == 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN / __fastfail */) {
        const uintptr_t rip = ep->ContextRecord != nullptr ? static_cast<uintptr_t>(ep->ContextRecord->Rip) : 0;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const unsigned long long rva = (addr >= base) ? static_cast<unsigned long long>(addr - base) : 0ULL;
        Log("CRASH-VEH: Exception 0x%08lX at addr %p (RVA 0x%llX, rip %p, tid %lu)",
            code, reinterpret_cast<void*>(addr), rva,
            reinterpret_cast<void*>(rip), GetCurrentThreadId());

        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR type = ep->ExceptionRecord->ExceptionInformation[0];
            const ULONG_PTR target = ep->ExceptionRecord->ExceptionInformation[1];
            Log("CRASH-VEH: Access Violation %s memory at %p",
                type == 0 ? "reading" : type == 1 ? "writing" : "executing",
                reinterpret_cast<void*>(target));
        }

        if (ep->ContextRecord != nullptr) {
            const PCONTEXT ctx = ep->ContextRecord;
            Log("CRASH-VEH: RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX",
                static_cast<unsigned long long>(ctx->Rax), static_cast<unsigned long long>(ctx->Rbx),
                static_cast<unsigned long long>(ctx->Rcx), static_cast<unsigned long long>(ctx->Rdx));
            Log("CRASH-VEH: RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX",
                static_cast<unsigned long long>(ctx->Rsi), static_cast<unsigned long long>(ctx->Rdi),
                static_cast<unsigned long long>(ctx->Rbp), static_cast<unsigned long long>(ctx->Rsp));
            Log("CRASH-VEH: R8 =%016llX R9 =%016llX R10=%016llX R11=%016llX",
                static_cast<unsigned long long>(ctx->R8), static_cast<unsigned long long>(ctx->R9),
                static_cast<unsigned long long>(ctx->R10), static_cast<unsigned long long>(ctx->R11));
            Log("CRASH-VEH: R12=%016llX R13=%016llX R14=%016llX R15=%016llX",
                static_cast<unsigned long long>(ctx->R12), static_cast<unsigned long long>(ctx->R13),
                static_cast<unsigned long long>(ctx->R14), static_cast<unsigned long long>(ctx->R15));

            auto read_safe_str = [](uintptr_t ptr, char* out, size_t max_len) -> bool {
                if (ptr < 0x10000 || ptr > 0x00007FFFFFFEFFFFULL) return false;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(ptr), out, max_len - 1, &got) || got == 0) {
                    return false;
                }
                out[got] = '\0';
                size_t len = 0;
                for (size_t i = 0; i < got; ++i) {
                    if (out[i] == '\0') { len = i; break; }
                    if (out[i] < 32 || out[i] > 126) {
                        if (out[i] != '\n' && out[i] != '\r' && out[i] != '\t') return false;
                    }
                }
                return len > 0;
            };

            // Inspect registers for panic or error strings
            char str_buf[256] = {};
            if (read_safe_str(static_cast<uintptr_t>(ctx->Rcx), str_buf, sizeof(str_buf))) {
                Log("CRASH-VEH: RCX string: \"%s\"", str_buf);
            }
            if (read_safe_str(static_cast<uintptr_t>(ctx->Rdx), str_buf, sizeof(str_buf))) {
                Log("CRASH-VEH: RDX string: \"%s\"", str_buf);
            }
            if (read_safe_str(static_cast<uintptr_t>(ctx->R8), str_buf, sizeof(str_buf))) {
                Log("CRASH-VEH: R8 string: \"%s\"", str_buf);
            }

            if (rva == 0x1EBB809ULL) {
                Log("CRASH-VEH: *** Dantelion2 Engine Panic (DL_PANIC) Triggered (0xDEADBA) ***");
            }

            // Dump callstack / return addresses from RSP
            if (ctx->Rsp != 0) {
                uintptr_t stack_vals[32] = {};
                SIZE_T read_bytes = 0;
                if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(ctx->Rsp), stack_vals, sizeof(stack_vals), &read_bytes)) {
                    const size_t count = read_bytes / sizeof(uintptr_t);
                    for (size_t i = 0; i < count; ++i) {
                        const uintptr_t val = stack_vals[i];
                        if (val >= base && val < base + 0x10000000) {
                            Log("CRASH-VEH: Stack[+0x%02zX] = %p (game RVA 0x%llX)", i * 8, reinterpret_cast<void*>(val),
                                static_cast<unsigned long long>(val - base));
                        }
                    }
                }
            }
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
