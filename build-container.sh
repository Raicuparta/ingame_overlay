#!/usr/bin/env bash
# Builds the Linux overlay .so inside a portable, old-glibc container and
# drops it in dist/linux-x86_64/.
#
# The container is only used for Linux; the Windows DLLs are cross-compiled
# separately with llvm-mingw (see build.sh).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_TAG="localhost/ingame-overlay-linux-builder:latest"

if ! command -v podman >/dev/null 2>&1; then
    echo "podman is required but was not found in PATH" >&2
    exit 1
fi

if ! podman image exists "$IMAGE_TAG"; then
    echo "Building container image $IMAGE_TAG"
    podman build -t "$IMAGE_TAG" -f "$SCRIPT_DIR/Containerfile" "$SCRIPT_DIR"
else
    echo "Container image $IMAGE_TAG already exists, skipping build"
fi

podman run --rm \
    -v "$SCRIPT_DIR:/work:Z" \
    -w /work \
    "$IMAGE_TAG" \
    sh /work/scripts/build_inside_container.sh

SO_PATH="$SCRIPT_DIR/dist/linux-x86_64/libeveryone_overlay.so"
if [[ ! -f "$SO_PATH" ]]; then
    echo "Could not locate built libeveryone_overlay.so at $SO_PATH" >&2
    exit 1
fi

echo "Built $SO_PATH"
