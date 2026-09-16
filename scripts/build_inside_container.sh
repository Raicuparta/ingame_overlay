#!/bin/sh
# Runs inside the manylinux2014 container (see ../Containerfile), with the
# repository bind-mounted at /work.
set -eu

cd /work

# Start from a clean build dir so the linker flags below are definitely
# picked up (CMake only reads LDFLAGS during the initial configure).
rm -rf /work/build-linux

# Statically link the C++ runtime so the .so doesn't need libstdc++.so.6 /
# libgcc_s.so.1 at load time. Only libGL, libX11 and libc remain, and those
# are already present in any process that has a running renderer.
export LDFLAGS="-static-libstdc++ -static-libgcc"

sh /work/build.sh linux

echo ">>> Portable Linux build complete"
