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

#ifdef EVERYONE_OVERLAY_DEBUG_LOG
    // Last-resort log destination for the diagnostic build, so enabling the
    // build option is enough to get logs without any env var plumbing through
    // Steam/Rai Pal. Windows only; other platforms still need
    // EVERYONE_OVERLAY_LOG.
    std::string& DefaultLogPathStorage()
    {
        static std::string path;
#if defined(_WIN32)
        static bool resolved = false;
        if (!resolved)
        {
            resolved = true;
            HMODULE module = nullptr;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&DefaultLogPathStorage), &module))
            {
                char buffer[MAX_PATH];
                DWORD length = GetModuleFileNameA(module, buffer, static_cast<DWORD>(sizeof(buffer)));
                if (length > 0 && length < sizeof(buffer))
                {
                    path.assign(buffer, length);
                    size_t slash = path.find_last_of("\\/");
                    if (slash != std::string::npos)
                        path.resize(slash + 1);
                    else
                        path.clear();
                    path += "everyone-overlay.log";
                }
            }
        }
#endif
        return path;
    }
#endif
}

void EveryoneOverlaySetLogPath(const std::string& path)
{
    LogPathStorage() = path;
}

void DebugLog(const char* format, ...)
{
    const std::string& stored = LogPathStorage();
    const char* path = !stored.empty() ? stored.c_str() : EnvLogPath();
#ifdef EVERYONE_OVERLAY_DEBUG_LOG
    if (path == nullptr)
    {
        const std::string& fallback = DefaultLogPathStorage();
        if (!fallback.empty())
            path = fallback.c_str();
    }
#endif
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
