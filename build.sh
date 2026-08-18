#!/usr/bin/env bash
# Builds the native overlay library (the InGameOverlay "hello world" bridge).
#
# Outputs:
#   native/dist/linux-x86_64/libeveryone_overlay.so
#   native/dist/win-x64/everyone_overlay.dll
#   native/dist/win-x86/everyone_overlay.dll
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM_MINGW_ROOT="${LLVM_MINGW_ROOT:-$HOME/.local/llvm-mingw}"
DIST_DIR="$SCRIPT_DIR/dist"

# llvm-mingw ships lowercase header names (windows.h), but the vendored code
# includes <Windows.h> / <TlHelp32.h>. On a case-sensitive filesystem those
# includes fail, so create the exact-case symlinks once.
fix_mingw_case() {
    local sysroot_include="$1"
    [[ -d "$sysroot_include" ]] || { echo "missing mingw sysroot: $sysroot_include" >&2; exit 1; }

    declare -A CASEFIX=( [Windows.h]=windows.h [TlHelp32.h]=tlhelp32.h )
    for cap in "${!CASEFIX[@]}"; do
        local lower="${CASEFIX[$cap]}"
        if [[ -f "$sysroot_include/$lower" && ! -e "$sysroot_include/$cap" ]]; then
            ln -s "$lower" "$sysroot_include/$cap"
            echo "case-fix: linked $sysroot_include/$cap -> $lower"
        fi
    done
}

build_linux() {
    echo ">>> Building Linux (native)"
    cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build-linux" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$SCRIPT_DIR/build-linux" -j"$(nproc)"
    mkdir -p "$DIST_DIR/linux-x86_64"
    cp "$SCRIPT_DIR/build-linux/libeveryone_overlay.so" "$DIST_DIR/linux-x86_64/"
}

build_windows() {
    local arch="$1"
    local toolchain="$2"
    local sysroot="$3"

    echo ">>> Building Windows $arch (llvm-mingw)"
    fix_mingw_case "$sysroot/include"

    cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build-win-$arch" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/toolchains/$toolchain"
    cmake --build "$SCRIPT_DIR/build-win-$arch" -j"$(nproc)"
    mkdir -p "$DIST_DIR/win-$arch"
    cp "$SCRIPT_DIR/build-win-$arch/everyone_overlay.dll" "$DIST_DIR/win-$arch/"
}

case "${1:-all}" in
    linux)
        build_linux
        ;;
    windows)
        build_windows x64  llvm-mingw-x86_64.cmake "$LLVM_MINGW_ROOT/x86_64-w64-mingw32"
        build_windows x86  llvm-mingw-i686.cmake   "$LLVM_MINGW_ROOT/i686-w64-mingw32"
        ;;
    all)
        build_linux
        build_windows x64 llvm-mingw-x86_64.cmake "$LLVM_MINGW_ROOT/x86_64-w64-mingw32"
        build_windows x86 llvm-mingw-i686.cmake   "$LLVM_MINGW_ROOT/i686-w64-mingw32"
        ;;
    *)
        echo "Usage: $0 {linux|windows|all}"
        exit 1
        ;;
esac

echo ">>> Native overlay built into $DIST_DIR"
