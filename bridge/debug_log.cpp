#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
    std::string& LogPathStorage()
    {
        static std::string path;
        return path;
    }

    const char* EnvLogPath()
    {
#if defined(_WIN32)
        static char buffer[1024];
        DWORD length = GetEnvironmentVariableA("EVERYONE_OVERLAY_LOG", buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return nullptr;
        return buffer;
#else
        return std::getenv("EVERYONE_OVERLAY_LOG");
#endif
    }
}

void EveryoneOverlaySetLogPath(const std::string& path)
{
    LogPathStorage() = path;
}

void DebugLog(const char* format, ...)
{
    const std::string& stored = LogPathStorage();
    const char* path = !stored.empty() ? stored.c_str() : EnvLogPath();
    if (path == nullptr)
        return;

    std::FILE* file = std::fopen(path, "a");
    if (file == nullptr)
        return;

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}
