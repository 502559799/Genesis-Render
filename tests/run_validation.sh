#!/usr/bin/env bash
# Builds the engine and runs the full headless validation for both RHI
# backends. On a machine with a display server DISPLAY is used directly;
# under Xvfb set DISPLAY / XDG_RUNTIME_DIR in the environment beforehand.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILDDIR=${1:-build}
WIDTH=${WIDTH:-640}
HEIGHT=${HEIGHT:-400}

cmake -S . -B "$BUILDDIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILDDIR" -j"$(nproc 2>/dev/null || echo 4)"

run_backend() {
    local backend=$1
    local extra=()
    if [ "$backend" = "vulkan" ] && [ -n "${VK_ICD:-}" ]; then
        extra+=(VK_ICD_FILENAMES="$VK_ICD")
    fi
    echo "===== $backend validation ====="
    ( cd "$BUILDDIR" && env "${extra[@]}" SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-x11} \
        ./bin/GenesisPBR --backend "$backend" --selftest --width "$WIDTH" --height "$HEIGHT" )
}

run_backend opengl
run_backend vulkan
echo "All backend validations succeeded."
