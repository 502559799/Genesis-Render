#!/usr/bin/env bash
# Genesis Render - Linux build (OpenGL + Vulkan backends when SDK present).
set -euo pipefail
cd "$(dirname "$0")"
BUILD=${1:-build}
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
echo
echo "Binary: $BUILD/bin/GenesisPBR"
echo "Run:    cd $BUILD && ./bin/GenesisPBR --backend vulkan|opengl"
