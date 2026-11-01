#!/usr/bin/env bash
# ============================================================================
#  Genesis Render - optional software Vulkan driver (Mesa lavapipe)
#
#  Containers often ship libvulkan but no ICD.  This helper downloads Debian's
#  mesa-vulkan-drivers package (lavapipe, built against the LLVM 15 that the
#  base image already provides), extracts the ICD into ../deps/vulkan_icd and
#  prints the environment needed to run the Vulkan backend.
#
#  Usage: scripts/fetch_lavapipe.sh [--force]
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/../deps"
OUT="$DEPS/vulkan_icd"
FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ "$(uname)" != "Linux" ]]; then
    echo "lavapipe helper is Linux-only; macOS uses MoltenVK from the Vulkan SDK."
    exit 0
fi
if [[ "$FORCE" == "0" && -f "$OUT/lvp_icd.json" && -f "$OUT/libvulkan_lvp.so" ]]; then
    echo "lavapipe already present in $OUT"
else
    mkdir -p "$DEPS" "$OUT"
    DEB="$DEPS/mesa-vulkan-drivers.deb"
    URL="http://deb.debian.org/debian/pool/main/m/mesa/mesa-vulkan-drivers_22.3.6-1+deb12u2_amd64.deb"
    for _ in 1 2 3 4 5 6; do
        curl -L --retry 2 -C - -o "$DEB" "$URL" -sS || true
        if dpkg-deb -I "$DEB" >/dev/null 2>&1; then break; fi
    done
    dpkg-deb -I "$DEB" >/dev/null 2>&1 || { echo "download failed" >&2; exit 1; }
    rm -rf "$DEPS/mesa-vulkan-extract"
    dpkg-deb -x "$DEB" "$DEPS/mesa-vulkan-extract"
    cp "$DEPS/mesa-vulkan-extract/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so" "$OUT/"
    cat > "$OUT/lvp_icd.json" <<EOF
{"ICD":{"api_version":"1.1.230","library_path":"$OUT/libvulkan_lvp.so"},"file_format_version":"1.0.0"}
EOF
    echo "lavapipe installed in $OUT"
fi

if ldd "$OUT/libvulkan_lvp.so" | grep -q "not found"; then
    echo "!! missing runtime dependencies:" >&2
    ldd "$OUT/libvulkan_lvp.so" | grep "not found" >&2
    exit 1
fi

cat <<EOF
Use it with:
  export VK_ICD_FILENAMES=$OUT/lvp_icd.json
  export VK_DRIVER_FILES=$OUT/lvp_icd.json
  ./build/bin/genesis_tests                 # OpenGL + Vulkan + cross-backend
EOF
