#pragma once

#include <cstdint>
#include <vector>

namespace InGameOverlay
{
    // Replaces occurrences of the function pointer `original` with
    // `replacement` in this process's writable memory, recording every patched
    // address in `patchedAddresses`.
    //
    // This is used instead of an inline detour for vkQueuePresentKHR: on the
    // NVIDIA Linux driver, relocating the driver prologue to build a callable
    // trampoline produces a corrupted one that traps (ud2). Patching the stored
    // pointer instead leaves the driver code untouched, so the saved original
    // can simply be called.
    //
    // Phase 1 only looks at the main executable's own writable segments (where
    // loaders such as volk keep resolved function pointers). If that finds
    // nothing, phase 2 looks at anonymous/heap memory and only accepts a match
    // surrounded by other code pointers (a dispatch table) to avoid patching an
    // unrelated value.
    //
    // `addressToSkip` (typically the caller's own saved copy) is never patched.
    size_t PatchFunctionPointer(
        void* original,
        void* replacement,
        std::vector<uintptr_t>& patchedAddresses,
        const void* addressToSkip = nullptr);

    // Writes `original` back to every address previously recorded.
    void RestoreFunctionPointers(const std::vector<uintptr_t>& patchedAddresses, void* original);
}
