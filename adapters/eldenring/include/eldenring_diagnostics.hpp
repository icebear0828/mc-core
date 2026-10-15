#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <mutex>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace eldenring::diagnostics {

void Log(const char* fmt, ...);
void VLog(const char* fmt, va_list args);

#ifdef _WIN32
LONG WINAPI CrashVehHandler(PEXCEPTION_POINTERS ep);
#endif

void InitLogOnce();
FILE* GetLogFile();
const std::string& GetGameDir();
uint64_t GetStartTimeMs();

} // namespace eldenring::diagnostics
