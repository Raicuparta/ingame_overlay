// Case-fix shim: mingw-w64 ships "windows.h" (lowercase), but the vendored
// Windows sources include it with the MSVC casing. This directory is added to
// the include path only for Windows cross builds (see CMakeLists.txt).
#pragma once
#include <windows.h>
