#pragma once

#include <string>

// Optional debug logger.
//
// The destination is, in priority order:
//   1. a path set programmatically via EveryoneOverlaySetLogPath()
//   2. the EVERYONE_OVERLAY_LOG environment variable
// If neither is set, logging is a no-op, so it is inert in normal Unity use.
//
// These are defined in debug_log.cpp (not inline in the header) on purpose:
// the overlay is built with hidden inline visibility, which would give each
// translation unit its own copy of an inline function-local static.

void EveryoneOverlaySetLogPath(const std::string& path);
void DebugLog(const char* format, ...);
