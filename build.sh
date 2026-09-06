#!/usr/bin/env bash
# ============================================================================
#  Genesis Render 2.0 - one command cross platform build (Linux / macOS)
#
#  ./build.sh                     Release build, both backends, demo + tests
#  ./build.sh --debug             Debug build (symbols, no optimisation)
#  ./build.sh --gl-only           OpenGL backend only
#  ./build.sh --vk-only           Vulkan backend only
#  ./build.sh --shaders           regenerate the shader headers first
#  ./build.sh --test              build, then run the whole test suite
#  ./build.sh --demo              build, then run the demo (OpenGL)
#  ./build.sh --clean             remove the build directory first
#
#  Dependencies
#    Linux : build-essential cmake(>=3.16) libsdl2-dev zlib1g-dev
#            optional (Vulkan backend + shader regeneration):
#            libvulkan-dev glslang-tools spirv-tools mesa-vulkan-drivers xvfb
#    macOS : xcode-select --install; brew install cmake sdl2
#            optional: the LunarG Vulkan SDK (vulkan + glslangValidator)
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE=Release
BACKEND_GL=ON
BACKEND_VK=ON
REBUILD_SHADERS=OFF
RUN_TESTS=OFF
RUN_DEMO=OFF
CLEAN=OFF
JOBS=$( (nproc 2>/dev/null) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)    BUILD_TYPE=Debug; shift ;;
        --release)  BUILD_TYPE=Release; shift ;;
        --gl-only)  BACKEND_VK=OFF; shift ;;
        --vk-only)  BACKEND_GL=OFF; shift ;;
        --shaders)  REBUILD_SHADERS=ON; shift ;;
        --test)     RUN_TESTS=ON; shift ;;
        --demo)     RUN_DEMO=ON; shift ;;
        --clean)    CLEAN=ON; shift ;;
        -j*)        JOBS="${1#-j}"; shift ;;
        -h|--help)  sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BUILD_DIR="build"
[[ "$BUILD_TYPE" == "Debug" ]] && BUILD_DIR="build-debug"
[[ "$CLEAN" == "ON" ]] && rm -rf "$BUILD_DIR"

echo "==> Genesis Render: ${BUILD_TYPE} build (OpenGL=${BACKEND_GL} Vulkan=${BACKEND_VK}) in ${BUILD_DIR}"

# --- optional shader regeneration -------------------------------------------
if [[ "$REBUILD_SHADERS" == "ON" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        echo "==> regenerating shader headers"
        python3 scripts/build_shaders.py
    else
        echo "!! python3 not found - keeping the committed shader headers" >&2
    fi
fi

# --- configure ---------------------------------------------------------------
CMAKE_ARGS=(
    -S . -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DGENESIS_BACKEND_OPENGL="$BACKEND_GL"
    -DGENESIS_BACKEND_VULKAN="$BACKEND_VK"
    -DGENESIS_REBUILD_SHADERS=OFF
)
if command -v ninja >/dev/null 2>&1 && [[ -z "${GENESIS_NO_NINJA:-}" ]]; then
    CMAKE_ARGS+=(-G Ninja)
fi
cmake "${CMAKE_ARGS[@]}"

# --- build -------------------------------------------------------------------
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "==> build complete:"
ls -1 "$BUILD_DIR/bin" 2>/dev/null || true

# --- optional run ------------------------------------------------------------
if [[ "$RUN_TESTS" == "ON" ]]; then
    echo "==> running the test suite"
    # Headless environments need a display for the GL backend and an ICD for Vulkan.
    if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a "$BUILD_DIR/bin/genesis_tests"
    else
        "$BUILD_DIR/bin/genesis_tests"
    fi
fi

if [[ "$RUN_DEMO" == "ON" ]]; then
    echo "==> launching the demo (OpenGL)"
    if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a "$BUILD_DIR/bin/genesis_demo" --backend gl
    else
        "$BUILD_DIR/bin/genesis_demo" --backend gl
    fi
fi
