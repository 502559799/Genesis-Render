#!/usr/bin/env bash
# Genesis Render - macOS build.
#
# OpenGL 4.1 backend always builds. Install LunarG Vulkan SDK to also get
# the Vulkan backend (MoltenVK via VK_KHR_portability_enumeration).
#
# Homebrew dependencies (match your Mac architecture):
#   brew install sdl2 assimp
#   # Vulkan: https://vulkan.lunarq.com/sdk  (or: brew install molten-vk vulkan-headers)
#
# By default the build targets the *host* architecture. This matters on Apple
# Silicon: Homebrew under /opt/homebrew ships arm64-only dylibs, so building
# "x86_64;arm64" fails to link the x86_64 slice. For a universal binary you
# must install universal (or separate x86_64 under Rosetta in /usr/local)
# copies of SDL2/assimp, then configure with -DGENESIS_MAC_UNIVERSAL=ON.
set -euo pipefail
cd "$(dirname "$0")"
BUILD=${1:-build}

UNIVERSAL=""
if [ "${GENESIS_MAC_UNIVERSAL:-0}" = "1" ]; then
    UNIVERSAL="-DGENESIS_MAC_UNIVERSAL=ON"
fi

cmake -S . -B "$BUILD" -G Xcode -DCMAKE_BUILD_TYPE=Release $UNIVERSAL
cmake --build "$BUILD" --config Release -j"$(sysctl -n hw.ncpu)"
echo
echo "Binary: $BUILD/bin/Release/GenesisPBR (or $BUILD/bin/GenesisPBR)"
echo "Run with --backend opengl (--backend vulkan when the SDK is installed)."
