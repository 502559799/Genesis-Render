#!/usr/bin/env bash
# ============================================================================
#  Genesis Render - fetch the shader toolchain (glslangValidator + spirv-cross)
#
#  Only needed when regenerating shaders (./build.sh --shaders).  The committed
#  GLSL / SPIR-V / MSL artefacts are enough for normal builds.
#
#  Usage: scripts/fetch_shader_tools.sh [--force]
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/../deps"
OUT="$DEPS/tools"
FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ "$(uname)" != "Linux" ]]; then
    echo "On macOS install the Vulkan SDK (glslangValidator + spirv-cross) or brew them."
    exit 0
fi
mkdir -p "$OUT"

fetch_deb() {
    local url="$1" deb="$2" extract="$3" bin="$4"
    if [[ "$FORCE" == "0" && -x "$OUT/$bin" ]]; then
        echo "$bin already present"
        return
    fi
    for _ in 1 2 3 4 5 6; do
        curl -L --retry 2 -C - -o "$deb" "$url" -sS || true
        dpkg-deb -I "$deb" >/dev/null 2>&1 && break
    done
    dpkg-deb -I "$deb" >/dev/null 2>&1 || { echo "download failed: $url" >&2; exit 1; }
    rm -rf "$extract"
    dpkg-deb -x "$deb" "$extract"
    cp "$extract/usr/bin/$bin" "$OUT/$bin"
    chmod +x "$OUT/$bin"
    echo "installed $OUT/$bin"
}

fetch_deb "http://deb.debian.org/debian/pool/main/g/glslang/glslang-tools_12.0.0-2_amd64.deb" \
          "$DEPS/glslang-tools.deb" "$DEPS/glslang-tools-extract" glslangValidator
fetch_deb "http://deb.debian.org/debian/pool/main/s/spirv-cross/spirv-cross_2021.01.15+1.3.239.0-1_amd64.deb" \
          "$DEPS/spirv-cross.deb" "$DEPS/spirv-cross-extract" spirv-cross

"$OUT/glslangValidator" --version | head -1
"$OUT/spirv-cross" --help 2>&1 | head -1
echo
echo "Regenerate shaders with:"
echo "  PATH=$OUT:\$PATH ./build.sh --shaders"
