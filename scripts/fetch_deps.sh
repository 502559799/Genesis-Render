#!/usr/bin/env bash
# ============================================================================
#  Genesis Render - fetch the optional build dependencies into deps/prefix
#
#  Linux only (the CI container has the runtime libraries but not the
#  development headers).  Downloads:
#    - SDL2 headers            (system libSDL2 is linked)
#    - Vulkan headers          (system loader is linked)
#    - V8 via libnode 18.x     (headers + libnode.so + ICU/c-ares/libuv)
#
#  Usage:  scripts/fetch_deps.sh [--force]
#
#  The script is resumable: Debian mirrors are slow in some environments, so
#  every download retries with HTTP range continuation until the archive
#  validates.
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/../deps"
PREFIX="$DEPS/prefix"
FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ "$(uname)" != "Linux" ]]; then
    echo "This helper targets Linux. On macOS install cmake/sdl2 with Homebrew and"
    echo "point GENESIS_V8_ROOT at a libnode / V8 prefix (or use GENESIS_ENABLE_JS=OFF)."
    exit 0
fi

mkdir -p "$DEPS" "$PREFIX/include" "$PREFIX/lib"

need() {
    local marker="$1"
    [[ "$FORCE" == "1" ]] && return 0
    [[ -e "$marker" ]] && return 1
    return 0
}

# download URL OUT VALIDATE_CMD...
download() {
    local url="$1" out="$2"
    shift 2
    if [[ -s "$out" ]] && "$@" >/dev/null 2>&1; then
        return 0
    fi
    for _ in 1 2 3 4 5 6; do
        curl -L --retry 2 -C - -o "$out" "$url" -sS || true
        if "$@" >/dev/null 2>&1; then
            echo "  ok $(basename "$out") ($(stat -c %s "$out") bytes)"
            return 0
        fi
    done
    echo "  FAILED $(basename "$out")" >&2
    return 1
}

echo "==> SDL2 headers"
if need "$PREFIX/include/SDL2/SDL.h"; then
    URL="https://www.libsdl.org/release/SDL2-2.0.20.tar.gz"
    download "$URL" "$DEPS/SDL2.tar.gz" tar tzf
    tar xzf "$DEPS/SDL2.tar.gz" -C "$DEPS"
    rm -rf "$PREFIX/include/SDL2"
    cp -a "$DEPS/SDL2-2.0.20/include" "$PREFIX/include/SDL2"
fi

echo "==> Vulkan headers"
if need "$PREFIX/include/vulkan/vulkan.h"; then
    URL="https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.204.tar.gz"
    download "$URL" "$DEPS/vkheaders.tar.gz" tar tzf
    rm -rf "$DEPS/Vulkan-Headers-1.3.204"
    tar xzf "$DEPS/vkheaders.tar.gz" -C "$DEPS"
    rm -rf "$PREFIX/include/vulkan"
    cp -a "$DEPS/Vulkan-Headers-1.3.204/include/vulkan" "$PREFIX/include/"
fi

echo "==> V8 (libnode 18.20.4 + ICU 72 + c-ares + libuv)"
if need "$PREFIX/include/node/v8.h"; then
    DEB="$DEPS/libnode-dev.deb"
    download "http://deb.debian.org/debian/pool/main/n/nodejs/libnode-dev_18.20.4+dfsg-1~deb12u2_amd64.deb" \
             "$DEB" dpkg-deb -I
    rm -rf "$DEPS/libnode-dev"
    dpkg-deb -x "$DEB" "$DEPS/libnode-dev"
    rm -rf "$PREFIX/include/node"
    cp -a "$DEPS/libnode-dev/usr/include/node" "$PREFIX/include/node"
fi
if need "$PREFIX/lib/libnode.so.108"; then
    DEB="$DEPS/libnode108.deb"
    download "http://deb.debian.org/debian/pool/main/n/nodejs/libnode108_18.20.4+dfsg-1~deb12u2_amd64.deb" \
             "$DEB" dpkg-deb -I
    rm -rf "$DEPS/libnode-runtime"
    dpkg-deb -x "$DEB" "$DEPS/libnode-runtime"
    cp -a "$DEPS/libnode-runtime/usr/lib/x86_64-linux-gnu/libnode.so.108" "$PREFIX/lib/"
    ln -sf libnode.so.108 "$PREFIX/lib/libnode.so"
fi
if need "$PREFIX/lib/libicuuc.so.72"; then
    DEB="$DEPS/icu72.deb"
    download "http://deb.debian.org/debian/pool/main/i/icu/libicu72_72.1-3+deb12u1_amd64.deb" \
             "$DEB" dpkg-deb -I
    rm -rf "$DEPS/icu72"
    dpkg-deb -x "$DEB" "$DEPS/icu72"
    cp -a "$DEPS"/icu72/usr/lib/x86_64-linux-gnu/libicu*.so.72* "$PREFIX/lib/"
fi
if need "$PREFIX/lib/libcares.so.2"; then
    DEB="$DEPS/cares.deb"
    download "http://deb.debian.org/debian/pool/main/c/c-ares/libc-ares2_1.18.1-3_amd64.deb" \
             "$DEB" dpkg-deb -I
    rm -rf "$DEPS/cares"
    dpkg-deb -x "$DEB" "$DEPS/cares"
    cp -a "$DEPS"/cares/usr/lib/x86_64-linux-gnu/libcares.so.2* "$PREFIX/lib/"
fi
if need "$PREFIX/lib/libuv.so.1"; then
    DEB="$DEPS/libuv.deb"
    download "http://deb.debian.org/debian/pool/main/libu/libuv1/libuv1_1.44.2-1+deb12u1_amd64.deb" \
             "$DEB" dpkg-deb -I
    rm -rf "$DEPS/libuv"
    dpkg-deb -x "$DEB" "$DEPS/libuv"
    cp -a "$DEPS"/libuv/usr/lib/x86_64-linux-gnu/libuv.so.1* "$PREFIX/lib/"
fi

# Linker names for the runtime libraries provided by the base image.
[[ -e "$PREFIX/lib/libSDL2.so" ]] || ln -sf /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 "$PREFIX/lib/libSDL2.so"
[[ -e "$PREFIX/lib/libvulkan.so" ]] || ln -sf /lib/x86_64-linux-gnu/libvulkan.so.1 "$PREFIX/lib/libvulkan.so"

echo "==> FFmpeg development files (libavcodec/libavformat/libswscale/libswresample)"
if need "$PREFIX/include/libavcodec/avcodec.h"; then
    FFMPEG_ROOT="$DEPS/ffmpeg-dev"
    mkdir -p "$FFMPEG_ROOT"
    for pkg in libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev; do
        DEB="$DEPS/${pkg}.deb"
        download "http://archive.ubuntu.com/ubuntu/pool/universe/f/ffmpeg/${pkg}_4.4.2-0ubuntu0.22.04.1_amd64.deb" \
                 "$DEB" dpkg-deb -I
        dpkg-deb -x "$DEB" "$FFMPEG_ROOT"
    done
    cp -a "$FFMPEG_ROOT"/usr/include/x86_64-linux-gnu/libavcodec \
          "$FFMPEG_ROOT"/usr/include/x86_64-linux-gnu/libavformat \
          "$FFMPEG_ROOT"/usr/include/x86_64-linux-gnu/libavutil \
          "$FFMPEG_ROOT"/usr/include/x86_64-linux-gnu/libswscale \
          "$FFMPEG_ROOT"/usr/include/x86_64-linux-gnu/libswresample "$PREFIX/include/"
    for lib in avcodec avformat avutil swscale swresample; do
        cp -a "$FFMPEG_ROOT/usr/lib/x86_64-linux-gnu/lib${lib}.so" "$PREFIX/lib/" 2>/dev/null || true
        # The dev symlink points at a fully versioned name; point that name at
        # the runtime library shipped by the base image.
        target="$(readlink "$PREFIX/lib/lib${lib}.so" || true)"
        if [[ -n "$target" && ! -e "$PREFIX/lib/$target" ]]; then
            system_lib="$(ls /lib/x86_64-linux-gnu/lib${lib}.so.* 2>/dev/null | head -1)"
            [[ -n "$system_lib" ]] && ln -sf "$system_lib" "$PREFIX/lib/$target"
        fi
    done
fi

echo
echo "dependencies ready in $PREFIX"
echo "configure with: cmake -S . -B build -DCMAKE_PREFIX_PATH=$PREFIX -DGENESIS_V8_ROOT=$PREFIX"
echo "or simply run ./build.sh (it picks the prefix up automatically)."
