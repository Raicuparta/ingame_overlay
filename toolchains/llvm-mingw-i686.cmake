# Toolchain file to cross-compile the native overlay for 32-bit Windows
# using the llvm-mingw toolchain installed at ~/.local/llvm-mingw.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(LLVM_MINGW_ROOT "$ENV{HOME}/.local/llvm-mingw")

set(CMAKE_C_COMPILER    "${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER  "${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER   "${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-windres")

set(CMAKE_C_COMPILER_TARGET   i686-w64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET i686-w64-windows-gnu)

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/i686-w64-mingw32")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
