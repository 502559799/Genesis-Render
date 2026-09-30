#!/usr/bin/env bash
# ============================================================================
#  Genesis Render - one command cross platform build (Linux / macOS)
#
#  ./build.sh                     Release build, every backend available on
#                                 this platform (OpenGL + Vulkan, + Metal on
#                                 macOS), demo + tests, into build/
#  ./build.sh --debug             Debug build (symbols, no optimisation)
#  ./build.sh --no-gl             skip the OpenGL backend
#  ./build.sh --no-vulkan         skip the Vulkan backend
#  ./build.sh --no-metal          skip the Metal backend (macOS)
#  ./build.sh --shaders           regenerate GLSL / SPIR-V / MSL first
#                                 (needs glslangValidator and spirv-cross)
#  ./build.sh --test              build, then run the whole test suite on
#                                 every backend + cross backend comparison
#  ./build.sh --validation        with --test / --demo: enable API validation
#                                 (Vulkan layers, GL error checks, Metal API)
#  ./build.sh --demo [BACKEND]    build, then run the demo (gl | vulkan | metal)
#  ./build.sh --clean             remove the build directory first
#
#  Dependencies
#    Linux : build-essential cmake(>=3.16) libsdl2-dev zlib1g-dev
#            optional: libvulkan-dev mesa-vulkan-drivers xvfb
#            shader regeneration: glslang-tools spirv-cross
#    macOS : xcode-select --install; brew install cmake sdl2
#            optional: LunarG Vulkan SDK (MoltenVK + validation layers)
#            shader regeneration: glslang spirv-cross (SDK or brew)
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE=Release
BACKEND_GL=ON
BACKEND_VK=ON
BACKEND_MTL=ON
REBUILD_SHADERS=OFF
RUN_TESTS=OFF
RUN_DEMO=OFF
DEMO_BACKEND=gl
VALIDATION=OFF
CLEAN=OFF
JOBS=$( (nproc 2>/dev/null) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)      BUILD_TYPE=Debug; shift ;;
        --release)    BUILD_TYPE=Release; shift ;;
        --no-gl)      BACKEND_GL=OFF; shift ;;
        --no-vulkan)  BACKEND_VK=OFF; shift ;;
        --no-metal)   BACKEND_MTL=OFF; shift ;;
        --shaders)    REBUILD_SHADERS=ON; shift ;;
        --test)       RUN_TESTS=ON; shift ;;
        --validation) VALIDATION=ON; shift ;;
        --demo)       RUN_DEMO=ON; shift
                      if [[ $# -gt 0 && "$1" != --* ]]; then DEMO_BACKEND="$1"; shift; fi ;;
        --clean)      CLEAN=ON; shift ;;
        -j*)          JOBS="${1#-j}"; shift ;;
        -h|--help)    sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ "$(uname)" != "Darwin" ]] && BACKEND_MTL=OFF

BUILD_DIR="build"
[[ "$BUILD_TYPE" == "Debug" ]] && BUILD_DIR="build-debug"
[[ "$CLEAN" == "ON" ]] && rm -rf "$BUILD_DIR"

echo "==> Genesis Render: ${BUILD_TYPE} build (OpenGL=${BACKEND_GL} Vulkan=${BACKEND_VK} Metal=${BACKEND_MTL}) in ${BUILD_DIR}"

# --- optional shader regeneration -------------------------------------------
if [[ "$REBUILD_SHADERS" == "ON" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        echo "==> regenerating shaders (GLSL 410 / SPIR-V / MSL)"
        python3 scripts/build_shaders.py
    else
        echo "!! python3 not found - keeping the committed shader sources" >&2
    fi
fi

# --- configure ---------------------------------------------------------------
CMAKE_ARGS=(
    -S . -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DGENESIS_BACKEND_OPENGL="$BACKEND_GL"
    -DGENESIS_BACKEND_VULKAN="$BACKEND_VK"
    -DGENESIS_BACKEND_METAL="$BACKEND_MTL"
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

# --- validation environment ----------------------------------------------------
if [[ "$VALIDATION" == "ON" ]]; then
    export GENESIS_VALIDATION=1 GENESIS_GL_VALIDATE=1
    # LunarG SDK layers are not on the loader path by default on macOS.
    if [[ -z "${VK_ADD_LAYER_PATH:-}" ]]; then
        for d in "$HOME"/VulkanSDK/*/macOS/share/vulkan/explicit_layer.d /usr/share/vulkan/explicit_layer.d; do
            [[ -d "$d" ]] && export VK_ADD_LAYER_PATH="$d" && break
        done
    fi
fi

run_headless() {
    if [[ -z "${DISPLAY:-}" && "$(uname)" != "Darwin" ]] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a "$@"
    else
        "$@"
    fi
}

# --- optional run ------------------------------------------------------------
if [[ "$RUN_TESTS" == "ON" ]]; then
    echo "==> running the test suite"
    if [[ "$VALIDATION" == "ON" && "$BACKEND_MTL" == "ON" ]]; then
        # Metal API validation must not be applied to the GL driver (it runs on
        # Metal and falls back to software), so validate Metal on its own.
        run_headless "$BUILD_DIR/bin/genesis_tests" --backend gl
        [[ "$BACKEND_VK" == "ON" ]] && run_headless "$BUILD_DIR/bin/genesis_tests" --backend vulkan
        MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 run_headless "$BUILD_DIR/bin/genesis_tests" --backend metal
    else
        run_headless "$BUILD_DIR/bin/genesis_tests"
    fi
fi

if [[ "$RUN_DEMO" == "ON" ]]; then
    echo "==> launching the demo (${DEMO_BACKEND})"
    [[ "$VALIDATION" == "ON" && "$DEMO_BACKEND" == "metal" ]] && export MTL_DEBUG_LAYER=1
    run_headless "$BUILD_DIR/bin/genesis_demo" --backend "$DEMO_BACKEND"
fi
