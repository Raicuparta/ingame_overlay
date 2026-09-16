#include "PresentPointerHook.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/uio.h>
#include <unistd.h>
#include <limits.h>

namespace InGameOverlay
{
    namespace
    {
        struct MapRegion
        {
            uintptr_t Start;
            uintptr_t End;
            std::string Path;
        };

        // Reads/writes via process_vm_* so a stale, unmapped or device mapping
        // reports a failure instead of faulting the process.
        bool ReadProcessMemory(uintptr_t address, void* buffer, size_t size, size_t& bytesRead)
        {
            iovec local{ buffer, size };
            iovec remote{ reinterpret_cast<void*>(address), size };
            const ssize_t result = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
            bytesRead = result > 0 ? static_cast<size_t>(result) : 0;
            return result > 0;
        }

        bool ReadUintptr(uintptr_t address, uintptr_t& value)
        {
            size_t bytesRead = 0;
            return ReadProcessMemory(address, &value, sizeof(value), bytesRead) && bytesRead == sizeof(value);
        }

        bool WriteUintptr(uintptr_t address, uintptr_t value)
        {
            iovec local{ &value, sizeof(value) };
            iovec remote{ reinterpret_cast<void*>(address), sizeof(value) };
            const ssize_t result = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
            return result == static_cast<ssize_t>(sizeof(value));
        }

        std::vector<MapRegion> ReadMapRegions()
        {
            std::vector<MapRegion> regions;

            std::ifstream maps("/proc/self/maps");
            std::string line;
            while (std::getline(maps, line))
            {
                std::istringstream stream(line);
                std::string range;
                std::string perms;
                std::string offset;
                std::string device;
                std::string inode;
                if (!(stream >> range >> perms >> offset >> device >> inode))
                    continue;

                std::string path;
                std::getline(stream, path);
                const size_t firstChar = path.find_first_not_of(' ');
                path = (firstChar == std::string::npos) ? std::string() : path.substr(firstChar);

                // Only readable + writable, non-executable mappings are candidates.
                if (perms.size() < 4 || perms[0] != 'r' || perms[1] != 'w' || perms[2] == 'x')
                    continue;

                const size_t dash = range.find('-');
                if (dash == std::string::npos)
                    continue;

                MapRegion region{};
                region.Start = std::stoull(range.substr(0, dash), nullptr, 16);
                region.End = std::stoull(range.substr(dash + 1), nullptr, 16);
                region.Path = path;
                if (region.End > region.Start)
                    regions.push_back(std::move(region));
            }

            return regions;
        }

        bool IsExecutableAddress(const std::vector<MapRegion>& executableRegions, uintptr_t address)
        {
            for (const MapRegion& region : executableRegions)
            {
                if (region.Start <= address && address < region.End)
                    return true;
            }
            return false;
        }

        // A Vulkan dispatch table (or a volk-style device table) is a run of
        // function pointers. Require most surrounding words to look like code
        // pointers, so a lone coincidental match is not patched.
        bool LooksLikePointerTable(const std::vector<MapRegion>& executableRegions, const MapRegion& region, uintptr_t address)
        {
            constexpr int radius = 4;
            int codeLikeEntries = 0;
            int checkedEntries = 0;

            for (int offset = -radius; offset <= radius; ++offset)
            {
                if (offset == 0)
                    continue;

                const uintptr_t entry = address + static_cast<uintptr_t>(offset) * sizeof(uintptr_t);
                if (entry < region.Start || entry + sizeof(uintptr_t) > region.End)
                    continue;

                uintptr_t value = 0;
                if (!ReadUintptr(entry, value))
                    continue;

                ++checkedEntries;
                if (value == 0 || IsExecutableAddress(executableRegions, value))
                    ++codeLikeEntries;
            }

            return checkedEntries >= 4 && codeLikeEntries * 2 >= checkedEntries + 2;
        }

        size_t ScanRegion(
            const MapRegion& region,
            const std::vector<MapRegion>& executableRegions,
            uintptr_t original,
            uintptr_t replacement,
            bool requirePointerTable,
            uintptr_t addressToSkip,
            std::vector<uintptr_t>& patchedAddresses)
        {
            constexpr size_t chunkBytes = 64 * 1024;
            std::vector<uintptr_t> buffer(chunkBytes / sizeof(uintptr_t));

            size_t hits = 0;
            uintptr_t address = region.Start;
            while (address + sizeof(uintptr_t) <= region.End)
            {
                size_t wanted = static_cast<size_t>(std::min<uintptr_t>(chunkBytes, region.End - address));
                wanted &= ~(sizeof(uintptr_t) - 1);
                if (wanted == 0)
                    break;

                size_t bytesRead = 0;
                if (!ReadProcessMemory(address, buffer.data(), wanted, bytesRead) || bytesRead < sizeof(uintptr_t))
                {
                    // Unreadable: skip a page and retry.
                    address += 4096;
                    continue;
                }

                const size_t words = bytesRead / sizeof(uintptr_t);
                for (size_t i = 0; i < words; ++i)
                {
                    if (buffer[i] != original)
                        continue;

                    const uintptr_t hitAddress = address + i * sizeof(uintptr_t);
                    if (hitAddress == addressToSkip)
                        continue;

                    if (requirePointerTable && !LooksLikePointerTable(executableRegions, region, hitAddress))
                        continue;

                    if (!WriteUintptr(hitAddress, replacement))
                        continue;

                    patchedAddresses.push_back(hitAddress);
                    ++hits;
                }

                address += bytesRead;
            }
            return hits;
        }
    }

    size_t PatchFunctionPointer(void* original, void* replacement, std::vector<uintptr_t>& patchedAddresses, const void* addressToSkip)
    {
        if (original == nullptr || replacement == nullptr)
            return 0;

        const uintptr_t originalValue = reinterpret_cast<uintptr_t>(original);
        const uintptr_t replacementValue = reinterpret_cast<uintptr_t>(replacement);
        const uintptr_t skipValue = reinterpret_cast<uintptr_t>(addressToSkip);

        const std::vector<MapRegion> regions = ReadMapRegions();

        // Executable ranges, used to validate dispatch-table neighbours. (The
        // permission filter above dropped non-writable regions, so collect the
        // executable view separately from /proc/self/maps.)
        std::vector<MapRegion> executableRegions;
        {
            std::ifstream maps("/proc/self/maps");
            std::string line;
            while (std::getline(maps, line))
            {
                std::istringstream stream(line);
                std::string range;
                std::string perms;
                std::string offset;
                std::string device;
                std::string inode;
                if (!(stream >> range >> perms >> offset >> device >> inode))
                    continue;
                if (perms.size() < 4 || perms[2] != 'x')
                    continue;

                const size_t dash = range.find('-');
                if (dash == std::string::npos)
                    continue;

                MapRegion region{};
                region.Start = std::stoull(range.substr(0, dash), nullptr, 16);
                region.End = std::stoull(range.substr(dash + 1), nullptr, 16);
                if (region.End > region.Start)
                    executableRegions.push_back(std::move(region));
            }
        }

        char executablePath[PATH_MAX] = {};
        const ssize_t executablePathLength = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
        const std::string mainExecutable = executablePathLength > 0
            ? std::string(executablePath, static_cast<size_t>(executablePathLength))
            : std::string();

        size_t phase1Hits = 0;

        // Phase 1: the main executable's own writable segments.
        if (!mainExecutable.empty())
        {
            for (const MapRegion& region : regions)
            {
                if (region.Path != mainExecutable)
                    continue;
                phase1Hits += ScanRegion(region, executableRegions, originalValue, replacementValue, false, skipValue, patchedAddresses);
            }
        }

        if (phase1Hits > 0)
            return phase1Hits;

        // Phase 2: anonymous/heap segments, only where the neighbours look like
        // a dispatch table.
        size_t phase2Hits = 0;
        for (const MapRegion& region : regions)
        {
            if (!region.Path.empty() && region.Path != "[heap]")
                continue;
            phase2Hits += ScanRegion(region, executableRegions, originalValue, replacementValue, true, skipValue, patchedAddresses);
        }

        return phase2Hits;
    }

    void RestoreFunctionPointers(const std::vector<uintptr_t>& patchedAddresses, void* original)
    {
        const uintptr_t originalValue = reinterpret_cast<uintptr_t>(original);
        for (uintptr_t address : patchedAddresses)
        {
            WriteUintptr(address, originalValue);
        }
    }
}
