#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genesis-Render procedural test-asset generator
==============================================

Generates a complete set of deterministic, dependency-free test assets for the
Genesis-Render PBR engine:

  assets/textures/
    checker_albedo.png              8x8 checkerboard, RGBA8, sRGB   (512x512)
    checker_metallic_roughness.png  aligned G=roughness B=metallic  (512x512)
    panel_normal.png                height-field derived tangents   (256x256)
    panel_height.png                the source height field         (256x256)
    tile_ao.png                     panel-aligned AO (R=G=B)        (256x256)
    grid_emissive.png               black + cyan thin grid          (512x512)
    uv_gradient.png                 R=u G=v B=0.5 A=255             (512x512)
    sky_equirect.hdr                Radiance RGBE equirect sky      (2048x1024)
    small_equirect.hdr              low-res sky for fast tests      (256x128)

  assets/models/
    genesis_sphere.gltf/.bin/.glb   UV sphere r=1, 48 seg x 32 rings
    genesis_torus.gltf/.bin         torus R=1.0 r=0.35, 64 x 32
    genesis_cube_textured.gltf/.bin 2x2x2 cube, textured material
    genesis_test_scene.gltf         3 nodes, references the 3 .bin files above

Everything is produced with the Python3 standard library only (zlib, struct,
math, random with fixed seeds). No PIL, no numpy.

Texture conventions (match the engine):
  * base_color / emissive : sRGB
  * metallic_roughness    : linear, G = roughness, B = metallic (glTF), R = 255
  * normal                : linear RGB, OpenGL style (+Y up / "green up"),
                            generated from a height field via central
                            differences (see height_to_normal()).
  * occlusion             : linear grayscale stored in R (G,B identical)
  * uv_gradient           : image row 0 = v = 0 = TOP of the image, i.e. the
                            G channel grows *downwards* through the rows.
                            Engine convention: row 0 == v == 0 == top.

Equirectangular HDR convention (matches the engine's sampleEquirect):
  For a pixel (x, y) of an W x H (2:1) equirect image:

      u     = (x + 0.5) / W                       // [0, 1]
      v     = (y + 0.5) / H                       // [0, 1], row 0 = top
      phi   = (u - 0.5) * 2*pi                    // == atan2(dir.z, dir.x)
      theta = v * pi                              // == acos(dir.y)
      dir   = (cos(phi)*sin(theta), cos(theta), sin(phi)*sin(theta))

  Inverse mapping used by the engine GLSL helper:

      vec3 sampleEquirect(sampler2D env, vec3 d) {
          vec2 uv = vec2(atan(d.z, d.x) * (0.5 / PI) + 0.5,
                         acos(clamp(d.y, -1.0, 1.0)) / PI);
          return texture(env, uv).rgb;
      }

  => Row 0 of the image is +Y (zenith), the last row is -Y (nadir).
  => +X points at u = 0 (left seam of the image), +Z at u = 0.25.
  The HDR is uploaded without vertical flip (stb_image returns row 0 first),
  so v = 0 samples row 0 == zenith, exactly as the mapping above assumes.

Usage:
    python3 scripts/gen_assets.py                # generate everything
    python3 scripts/gen_assets.py --verify       # re-read & assert everything
    python3 scripts/gen_assets.py --out-dir DIR  # override output root
"""

import argparse
import json
import math
import os
import random
import struct
import sys
import tempfile
import time
import zlib
from array import array

# ----------------------------------------------------------------------------
# Small math helpers
# ----------------------------------------------------------------------------

def clamp(x, lo, hi):
    return lo if x < lo else (hi if x > hi else x)


def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


# ============================================================================
# A. Minimal PNG encoder / decoder
# ============================================================================

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

_PNG_COLOR_TYPE = {1: 0, 2: 4, 3: 2, 4: 6}  # channels -> PNG color type


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    """One PNG chunk: length + tag + data + CRC32(tag||data)."""
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, width, height, pixels, channels=4):
    """
    Write an 8-bit non-interlaced PNG.

    pixels  : bytes/bytearray, row-major, `channels` bytes per pixel
              (channels=3 -> RGB, channels=4 -> RGBA)
    All rows use filter type 0 (None); IDAT is zlib.compress() of the
    filtered scanlines. CRCs use zlib.crc32 (per PNG spec, big-endian).
    """
    if channels not in (3, 4):
        raise ValueError("channels must be 3 (RGB) or 4 (RGBA)")
    expected = width * height * channels
    if len(pixels) != expected:
        raise ValueError("pixel buffer size %d != expected %d"
                         % (len(pixels), expected))
    color_type = _PNG_COLOR_TYPE[channels]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    stride = width * channels
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0
        off = y * stride
        raw += pixels[off:off + stride]
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(PNG_SIGNATURE)
        f.write(_png_chunk(b"IHDR", ihdr))
        f.write(_png_chunk(b"IDAT", idat))
        f.write(_png_chunk(b"IEND", b""))


def _paeth_predictor(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    """
    Decode an 8-bit non-interlaced PNG (color types 0/2/4/6, i.e. gray,
    RGB, gray+alpha, RGBA). Implements un-filtering for filter types 0..4
    (None, Sub, Up, Average, Paeth). Validates every chunk CRC.

    Returns (width, height, channels, bytearray_of_pixels).
    """
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError("%s: bad PNG signature" % path)
    pos = 8
    width = height = bitdepth = color_type = interlace = None
    idat = bytearray()
    while pos + 12 <= len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        (crc,) = struct.unpack(">I", data[pos + 8 + ln:pos + 12 + ln])
        if crc != (zlib.crc32(tag + body) & 0xFFFFFFFF):
            raise ValueError("%s: CRC mismatch in %s chunk" % (path, tag))
        if tag == b"IHDR":
            (width, height, bitdepth, color_type,
             _cm, _fm, interlace) = struct.unpack(">IIBBBBB", body)
            if bitdepth != 8:
                raise ValueError("%s: only 8-bit supported (got %d)"
                                 % (path, bitdepth))
            if interlace != 0:
                raise ValueError("%s: interlaced PNG not supported" % path)
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        pos += 12 + ln
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ValueError("%s: unsupported color type %d (palette etc.)"
                         % (path, color_type))
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        ftype = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if ftype == 0:
            pass
        elif ftype == 1:  # Sub
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif ftype == 2:  # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:  # Average
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                line[x] = (line[x] + _paeth_predictor(a, b, c)) & 0xFF
        else:
            raise ValueError("%s: bad filter type %d" % (path, ftype))
        o = y * stride
        out[o:o + stride] = line
        prev = line
    return width, height, channels, out


def png_pixel(px, w, ch, x, y):
    """Fetch pixel (x,y) from a decoded PNG pixel buffer as a tuple."""
    o = (y * w + x) * ch
    return tuple(px[o:o + ch])


# ============================================================================
# Minimal TGA encoder (uncompressed truecolor, type 2) + tiny reader
# ============================================================================

def write_tga(path, w, h, rgba):
    """
    Write an uncompressed truecolor TGA (image type 2), 32 bpp BGRA.

    TGA default origin is BOTTOM-left, so rows are written flipped
    vertically (file row 0 == image bottom row). The image descriptor is
    0x08 (8 alpha bits, origin = bottom-left). Pixels are stored BGRA.
    """
    if len(rgba) != w * h * 4:
        raise ValueError("rgba buffer size mismatch")
    header = bytearray(18)
    header[2] = 2                       # uncompressed truecolor
    struct.pack_into("<HH", header, 12, w, h)   # width/height at byte 12/14
    header[16] = 32                     # bits per pixel
    header[17] = 0x08                   # 8 alpha bits, origin bottom-left
    out = bytearray()
    out += header
    for y in range(h - 1, -1, -1):      # Y flip: bottom row first
        row = rgba[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            r = row[x * 4]
            g = row[x * 4 + 1]
            b = row[x * 4 + 2]
            a = row[x * 4 + 3]
            out += bytes((b, g, r, a))  # BGRA order
    with open(path, "wb") as f:
        f.write(bytes(out))


def read_tga(path):
    """Minimal reader for uncompressed 32bpp type-2 TGAs written above."""
    with open(path, "rb") as f:
        data = f.read()
    if data[2] != 2:
        raise ValueError("not an uncompressed truecolor TGA")
    w, h = struct.unpack_from("<HH", data, 12)
    bpp = data[16]
    descriptor = data[17]
    origin_top = bool(descriptor & 0x20)
    if bpp != 32:
        raise ValueError("only 32bpp supported")
    idlen = data[0]
    off = 18 + idlen
    rgba = bytearray(w * h * 4)
    for fy in range(h):                 # fy = row order in file (bottom-up)
        y = fy if origin_top else h - 1 - fy
        for x in range(w):
            b, g, r, a = data[off:off + 4]
            off += 4
            o = (y * w + x) * 4
            rgba[o] = r
            rgba[o + 1] = g
            rgba[o + 2] = b
            rgba[o + 3] = a
    return w, h, rgba


# ============================================================================
# Radiance HDR (RGBE) encoder / decoder
# ============================================================================

def write_hdr(path, w, h, rgb_floats):
    """
    Write a Radiance .hdr (RGBE) file, equirectangular-friendly.

    rgb_floats : flat sequence of 3*w*h linear floats (R,G,B per pixel,
                 row-major, row 0 first).

    Header: "#?RADIANCE", FORMAT=32-bit_rle_rgbe, then the standard
    resolution line "-Y <h> +X <w>". Scanlines are written UNCOMPRESSED
    (old-style, no per-scanline RLE). stb_image (and Radiance tools) detect
    the missing RLE marker and fall back to the old uncompressed path, so
    this is fully compatible; it also keeps the encoder trivially correct.
    """
    if len(rgb_floats) != 3 * w * h:
        raise ValueError("rgb_floats must hold 3*w*h values")
    rgbe = bytearray(4 * w * h)
    o = 0
    for i in range(0, 3 * w * h, 3):
        r = rgb_floats[i]
        g = rgb_floats[i + 1]
        b = rgb_floats[i + 2]
        v = r if r > g else g
        if b > v:
            v = b
        if v < 1e-32:
            rgbe[o] = rgbe[o + 1] = rgbe[o + 2] = rgbe[o + 3] = 0
        else:
            m, e = math.frexp(v)
            scale = m * 256.0 / v       # == 2**(8 - e)
            ri = int(r * scale)
            gi = int(g * scale)
            bi = int(b * scale)
            rgbe[o] = 255 if ri > 255 else ri
            rgbe[o + 1] = 255 if gi > 255 else gi
            rgbe[o + 2] = 255 if bi > 255 else bi
            rgbe[o + 3] = e + 128
        o += 4
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\n")
        f.write(b"# Genesis-Render procedural environment map (gen_assets.py)\n")
        f.write(b"FORMAT=32-bit_rle_rgbe\n")
        f.write(b"\n")
        f.write(("-Y %d +X %d\n" % (h, w)).encode("ascii"))
        f.write(bytes(rgbe))


def read_hdr(path):
    """
    Read a Radiance RGBE file. Supports both the old uncompressed scanline
    format (what write_hdr produces) and the new RLE scanline format.

    Returns (w, h, array('f') of 3*w*h floats). Decode matches stb_image:
    value = (component + 0.5) * 2**(exponent - 128 - 8).
    """
    with open(path, "rb") as f:
        data = f.read()
    if not (data.startswith(b"#?RADIANCE") or data.startswith(b"#?RGBE")):
        raise ValueError("%s: not a Radiance HDR file" % path)
    pos = data.index(b"\n\n") + 2       # end of header text lines
    # resolution line: "-Y <h> +X <w>"
    nl = data.index(b"\n", pos)
    res = data[pos:nl].split()
    pos = nl + 1
    if res[0] != b"-Y" or res[2] != b"+X":
        raise ValueError("%s: unsupported resolution line %r" % (path, res))
    h = int(res[1])
    w = int(res[3])
    out = array("f", bytes(4 * 3 * w * h))

    def decode_rgbe(quad, dst, o):
        e = quad[3]
        if e == 0:
            dst[o] = dst[o + 1] = dst[o + 2] = 0.0
        else:
            f = 2.0 ** (e - (128 + 8))
            dst[o] = (quad[0] + 0.5) * f
            dst[o + 1] = (quad[1] + 0.5) * f
            dst[o + 2] = (quad[2] + 0.5) * f

    first = data[pos:pos + 4]
    rle = (len(first) == 4 and first[0] == 2 and first[1] == 2
           and not (first[2] & 0x80) and ((first[2] << 8) | first[3]) == w)
    if not rle:
        # old-style uncompressed: plain w*h RGBE quads
        need = w * h * 4
        buf = data[pos:pos + need]
        if len(buf) != need:
            raise ValueError("%s: truncated HDR data" % path)
        o = 0
        for i in range(0, need, 4):
            decode_rgbe(buf[i:i + 4], out, o)
            o += 3
    else:
        # new-style RLE: each scanline preceded by its own res line + marker
        scan = bytearray(4 * w)
        for y in range(h):
            if y > 0:
                nl = data.index(b"\n", pos)      # per-scanline "-Y 1 +X w"
                pos = nl + 1
            if data[pos] != 2 or data[pos + 1] != 2:
                raise ValueError("%s: bad RLE scanline marker" % path)
            slen = (data[pos + 2] << 8) | data[pos + 3]
            if slen != w:
                raise ValueError("%s: RLE scanline width mismatch" % path)
            pos += 4
            for c in range(4):                   # per-channel run-length
                ptr = c
                while ptr < 4 * w:
                    cnt = data[pos]
                    pos += 1
                    if cnt > 128:                # run
                        val = data[pos]
                        pos += 1
                        for _ in range(cnt - 128):
                            scan[ptr] = val
                            ptr += 4
                    else:                        # literal
                        scan[ptr:ptr + 4 * cnt:4] = data[pos:pos + cnt]
                        pos += cnt
                        ptr += 4 * cnt
            o = y * w * 3
            for i in range(0, 4 * w, 4):
                decode_rgbe(scan[i:i + 4], out, o)
                o += 3
    return w, h, out


# ============================================================================
# Deterministic 3D value noise (for clouds) - fixed seed, no global random
# ============================================================================

_NOISE_P = 32
_NOISE_GRID = None


def init_noise(seed):
    global _NOISE_GRID
    rnd = random.Random(seed)
    _NOISE_GRID = [rnd.random() for _ in range(_NOISE_P ** 3)]


def value_noise3(x, y, z):
    """Trilinear-interpolated 3D value noise on a wrapping 32^3 lattice."""
    g = _NOISE_GRID
    xi = int(math.floor(x)); yi = int(math.floor(y)); zi = int(math.floor(z))
    xf = x - xi; yf = y - yi; zf = z - zi
    u = xf * xf * (3.0 - 2.0 * xf)
    v = yf * yf * (3.0 - 2.0 * yf)
    w = zf * zf * (3.0 - 2.0 * zf)
    x0 = (xi & 31) << 10; x1 = ((xi + 1) & 31) << 10
    y0 = (yi & 31) << 5;  y1 = ((yi + 1) & 31) << 5
    z0 = zi & 31;         z1 = (zi + 1) & 31
    c000 = g[x0 | y0 | z0]; c100 = g[x1 | y0 | z0]
    c010 = g[x0 | y1 | z0]; c110 = g[x1 | y1 | z0]
    c001 = g[x0 | y0 | z1]; c101 = g[x1 | y0 | z1]
    c011 = g[x0 | y1 | z1]; c111 = g[x1 | y1 | z1]
    x00 = c000 + (c100 - c000) * u
    x10 = c010 + (c110 - c010) * u
    x01 = c001 + (c101 - c001) * u
    x11 = c011 + (c111 - c011) * u
    a = x00 + (x10 - x00) * v
    b = x01 + (x11 - x01) * v
    return a + (b - a) * w


def fbm3(x, y, z, octaves):
    total = 0.0
    amp = 0.55
    norm = 0.0
    freq = 1.0
    for _ in range(octaves):
        total += amp * value_noise3(x * freq, y * freq, z * freq)
        norm += amp
        amp *= 0.5
        freq *= 2.13
    return total / norm


# ============================================================================
# B. Texture generators (all tileable / seamless)
# ============================================================================

def gen_checker_albedo(size=512, cells=8):
    """8x8 checkerboard: white(255) & mid-gray(128). Cell (0,0) is WHITE."""
    px = bytearray(size * size * 4)
    cell = size // cells
    o = 0
    for y in range(size):
        cy = y // cell
        for x in range(size):
            v = 255 if ((x // cell + cy) % 2 == 0) else 128
            px[o] = v; px[o + 1] = v; px[o + 2] = v; px[o + 3] = 255
            o += 4
    return px


def gen_checker_metallic_roughness(size=512, cells=8):
    """
    Aligned with gen_checker_albedo. glTF channel packing:
      G = roughness, B = metallic, R = 255 (free slot, AO lives elsewhere).
    White albedo cells  -> dielectric, rough : (255, 230, 0)
    Gray  albedo cells  -> metal, smooth     : (255, 50, 255)
    """
    px = bytearray(size * size * 4)
    cell = size // cells
    o = 0
    for y in range(size):
        cy = y // cell
        for x in range(size):
            white = ((x // cell + cy) % 2 == 0)
            px[o] = 255
            px[o + 1] = 230 if white else 50   # roughness
            px[o + 2] = 0 if white else 255    # metallic
            px[o + 3] = 255
            o += 4
    return px


def gen_panel_height(size=256, panels=4, groove_width=5.0):
    """
    Tileable panel/brick height field: flat plateaus (1.0) with smooth
    grooves (0.0) along the panel grid lines. Groove profile is a
    smoothstep in the distance to the nearest grid line, so the surface is
    C1 and the derived normals are smooth. Returns list of rows of floats.
    """
    cell = float(size) / panels
    rows = []
    for y in range(size):
        ry = y % cell
        dy = min(ry, cell - ry)               # distance to nearest h-line
        gy = smoothstep(0.0, groove_width, dy)
        row = []
        for x in range(size):
            rx = x % cell
            dx = min(rx, cell - rx)
            gx = smoothstep(0.0, groove_width, dx)
            row.append(gx * gy)
        rows.append(row)
    return rows


def height_to_normal(hmap, w, h, strength=3.0):
    """
    Convert a height field to a tangent-space normal map (RGB bytes).

    Uses central differences with WRAP-AROUND sampling so the result is
    tileable, and n = normalize(-strength * dh/dx, -strength * dh/dy, 1)
    in image space (x right, y down). Because this engine's convention is
    "image row 0 = v = 0 = top", +v grows with the row index, so
    ny = -dh/d(row) makes the upper lip of a groove green (>128):
    the standard OpenGL "+Y up / green up" normal-map look.

    Flat areas encode to exactly (128, 128, 255).
    """
    out = bytearray(w * h * 3)
    o = 0
    for y in range(h):
        ym = hmap[(y - 1) % h]
        y0 = hmap[y]
        yp = hmap[(y + 1) % h]
        for x in range(w):
            xm = (x - 1) % w
            xp = (x + 1) % w
            dhdx = (y0[xp] - y0[xm]) * 0.5
            dhdy = (yp[x] - ym[x]) * 0.5
            nx = -dhdx * strength
            ny = -dhdy * strength
            nz = 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            nx *= inv; ny *= inv; nz *= inv
            out[o] = int(clamp((nx * 0.5 + 0.5) * 255.0 + 0.5, 0, 255))
            out[o + 1] = int(clamp((ny * 0.5 + 0.5) * 255.0 + 0.5, 0, 255))
            out[o + 2] = int(clamp((nz * 0.5 + 0.5) * 255.0 + 0.5, 0, 255))
            o += 3
    return out


def gen_tile_ao(size=256, panels=4, fade=12.0):
    """
    AO aligned with the panel grid: 255 at cell centers, ~90 in the seams,
    smooth transition over `fade` pixels. Grayscale stored as RGB (R=G=B).
    """
    cell = float(size) / panels
    px = bytearray(size * size * 3)
    o = 0
    for y in range(size):
        ry = y % cell
        dy = min(ry, cell - ry)
        for x in range(size):
            rx = x % cell
            dx = min(rx, cell - rx)
            s = smoothstep(0.0, fade, min(dx, dy))
            v = int(90.0 + (255.0 - 90.0) * s + 0.5)
            px[o] = v; px[o + 1] = v; px[o + 2] = v
            o += 3
    return px


def gen_grid_emissive(size=512, cell=64, line_w=2):
    """Black base + thin bright-cyan (0,255,220) grid lines. sRGB, RGB8."""
    px = bytearray(size * size * 3)
    o = 0
    for y in range(size):
        on_h = (y % cell) < line_w
        for x in range(size):
            if on_h or (x % cell) < line_w:
                px[o] = 0; px[o + 1] = 255; px[o + 2] = 220
            else:
                px[o] = 0; px[o + 1] = 0; px[o + 2] = 0
            o += 3
    return px


def gen_uv_gradient(size=512):
    """
    UV debug chart: R = u, G = v, B = 0.5*255 = 128, A = 255.

    ENGINE CONVENTION: image row 0 == v == 0 == TOP. So G grows downwards:
    pixel (0,0) = (0,0,128,255) and pixel (511,511) = (255,255,128,255).
    R = round(255 * x / (w-1)), G = round(255 * y / (h-1)) so the corner
    assertions are exact.
    """
    px = bytearray(size * size * 4)
    o = 0
    for y in range(size):
        g = int(round(255.0 * y / (size - 1)))
        for x in range(size):
            r = int(round(255.0 * x / (size - 1)))
            px[o] = r; px[o + 1] = g; px[o + 2] = 128; px[o + 3] = 255
            o += 4
    return px


# ----------------------------------------------------------------------------
# Procedural equirectangular HDR sky
# ----------------------------------------------------------------------------

SKY_ZENITH = (0.05, 0.12, 0.35)     # deep blue straight up
SKY_HORIZON = (1.0, 0.75, 0.5)      # warm white at the horizon
SKY_GROUND = (0.08, 0.07, 0.06)     # dark gray below
SUN_PEAK = 300.0                    # sun disc peak radiance
SUN_RADIUS = math.radians(2.0)      # ~solar angular radius
SUN_TINT = (1.0, 0.95, 0.85)
GLOW_AMPLITUDE = 22.0
GLOW_FALLOFF = 520.0                # exp falloff in (1 - cos(angle))


def gen_equirect_sky(w, h, seed=1337, sun_azimuth_deg=45.0,
                     sun_elevation_deg=35.0, num_clouds=4, octaves=3,
                     cloud_freq=2.7, simplified=False):
    """
    Procedural sky as an equirectangular RGB float array (linear radiance).

    Mapping (see module docstring):
        u = (x+0.5)/w ; phi = (u-0.5)*2pi ; theta = ((y+0.5)/h)*pi
        dir = (cos(phi) sin(theta), cos(theta), sin(phi) sin(theta))
        row 0 = +Y zenith, last row = -Y nadir.

    Content: Preetham-style 3-stop vertical gradient, a ~300-radiance sun
    disc with exp halo, and `num_clouds` deterministic fbm clouds (fixed
    seed; random.Random(seed) only).
    """
    rnd = random.Random(seed)
    init_noise(seed ^ 0x5EED)

    az = math.radians(sun_azimuth_deg)
    el = math.radians(sun_elevation_deg)
    sx = math.cos(el) * math.cos(az)
    sy = math.sin(el)
    sz = math.cos(el) * math.sin(az)
    cos_r = math.cos(SUN_RADIUS)
    inv_span = 1.0 / (1.0 - cos_r)

    # Cloud "blobs": each is a spherical cap that masks an fbm detail field.
    blobs = []
    for _ in range(num_clouds):
        baz = rnd.uniform(0.0, 2.0 * math.pi)
        bel = rnd.uniform(0.20, 0.85)
        bx = math.cos(bel) * math.cos(baz)
        by = math.sin(bel)
        bz = math.cos(bel) * math.sin(baz)
        r_out = rnd.uniform(0.38, 0.62)
        r_in = r_out * rnd.uniform(0.30, 0.55)
        blobs.append((bx, by, bz, math.cos(r_out), math.cos(r_in)))

    cph = [0.0] * w
    sph = [0.0] * w
    axsun = [0.0] * w
    axb = [[0.0] * w for _ in blobs]
    for x in range(w):
        phi = (((x + 0.5) / w) - 0.5) * 2.0 * math.pi
        c = math.cos(phi)
        s = math.sin(phi)
        cph[x] = c
        sph[x] = s
        axsun[x] = c * sx + s * sz
        for k, (bx, by, bz, _e0, _e1) in enumerate(blobs):
            axb[k][x] = c * bx + s * bz

    rgb = array("f")
    exp = math.exp
    rowbuf = [0.0] * (3 * w)
    for y in range(h):
        theta = ((y + 0.5) / h) * math.pi
        st = math.sin(theta)
        ct = math.cos(theta)
        # vertical gradient (constant along the row)
        if ct >= 0.0:
            t = ct ** 0.55
            sr = lerp(SKY_HORIZON[0], SKY_ZENITH[0], t)
            sg = lerp(SKY_HORIZON[1], SKY_ZENITH[1], t)
            sb = lerp(SKY_HORIZON[2], SKY_ZENITH[2], t)
        else:
            t = (-ct) ** 0.5
            sr = lerp(SKY_HORIZON[0], SKY_GROUND[0], t)
            sg = lerp(SKY_HORIZON[1], SKY_GROUND[1], t)
            sb = lerp(SKY_HORIZON[2], SKY_GROUND[2], t)
        cysun = ct * sy
        cyb = [ct * b[1] for b in blobs]
        o = 0
        for x in range(w):
            dirx = st * cph[x]
            dirz = st * sph[x]
            r = sr; g = sg; b = sb
            dsun = st * axsun[x] + cysun
            # --- clouds (mask = union of spherical caps * fbm detail) ---
            m = 0.0
            for k in range(len(blobs)):
                e0 = blobs[k][3]
                d = st * axb[k][x] + cyb[k]
                if d > e0:
                    s = smoothstep(e0, blobs[k][4], d)
                    if s > m:
                        m = s
            if m > 0.015:
                f = fbm3(dirx * cloud_freq, ct * cloud_freq,
                         dirz * cloud_freq, 2 if simplified else octaves)
                density = m * smoothstep(0.34, 0.62, f)
                if density > 0.004:
                    ds = dsun if dsun > 0.0 else 0.0
                    lit = 1.15 + 3.0 * (ds ** 4)
                    cr = lit * 1.00; cg = lit * 0.98; cb = lit * 0.94
                    r += (cr - r) * density
                    g += (cg - g) * density
                    b += (cb - b) * density
            # --- sun disc + halo (drawn in front of clouds) ---
            if dsun > 0.98:
                d1 = 1.0 - dsun
                add = GLOW_AMPLITUDE * exp(-GLOW_FALLOFF * d1)
                if dsun > cos_r:
                    t = (dsun - cos_r) * inv_span
                    add += SUN_PEAK * smoothstep(0.0, 0.30, t)
                r += add * SUN_TINT[0]
                g += add * SUN_TINT[1]
                b += add * SUN_TINT[2]
            rowbuf[o] = r
            rowbuf[o + 1] = g
            rowbuf[o + 2] = b
            o += 3
        rgb.extend(rowbuf)
    return rgb


# ============================================================================
# C. Hand-written glTF 2.0 geometry
# ============================================================================

COMP_FLOAT = 5126
COMP_USHORT = 5123
COMP_UINT = 5125
TARGET_ARRAY_BUFFER = 34962
TARGET_ELEMENT_ARRAY_BUFFER = 34963
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
COMP_SIZE = {COMP_FLOAT: 4, COMP_USHORT: 2, COMP_UINT: 4}

ASSET_INFO = {"version": "2.0", "generator": "Genesis gen_assets.py"}


def gen_sphere_geometry(radius=1.0, segments=48, rings=32):
    """
    UV sphere, poles on +Y/-Y. v = 0 at the +Y pole, u wraps the equator.
    Tangent = d(pos)/d(u) normalized = (-sin(phi), 0, cos(phi)), w = +1
    (bitangent = cross(normal, tangent.xyz) * w points along +v).
    Pole-degenerate triangles are skipped.
    """
    pos = []; nrm = []; tan = []; uv = []; idx = []
    for i in range(rings + 1):
        v = i / rings
        theta = math.pi * v
        st = math.sin(theta); ct = math.cos(theta)
        for j in range(segments + 1):
            u = j / segments
            phi = 2.0 * math.pi * u
            sp = math.sin(phi); cp = math.cos(phi)
            nx = st * cp; ny = ct; nz = st * sp
            pos += (radius * nx, radius * ny, radius * nz)
            nrm += (nx, ny, nz)
            if st > 1e-8:
                tan += (-sp, 0.0, cp, 1.0)
            else:
                tan += (1.0, 0.0, 0.0, 1.0)
            uv += (u, v)
    s1 = segments + 1
    for i in range(rings):
        for j in range(segments):
            a = i * s1 + j
            b = a + s1          # (i+1, j)
            c = b + 1           # (i+1, j+1)
            d = a + 1           # (i, j+1)
            if i > 0:           # (a,d,c): skip degenerate at +Y pole
                idx += (a, d, c)
            if i < rings - 1:   # (a,c,b): skip degenerate at -Y pole
                idx += (a, c, b)
    return pos, nrm, tan, uv, idx


def gen_torus_geometry(major=1.0, minor=0.35, seg_u=64, seg_v=32):
    """Torus around the +Y axis. u wraps the major ring, v the tube."""
    pos = []; nrm = []; tan = []; uv = []; idx = []
    for i in range(seg_u + 1):
        u = i / seg_u
        phi = 2.0 * math.pi * u
        cp = math.cos(phi); sp = math.sin(phi)
        for j in range(seg_v + 1):
            v = j / seg_v
            psi = 2.0 * math.pi * v
            cq = math.cos(psi); sq = math.sin(psi)
            rr = major + minor * cq
            pos += (rr * cp, minor * sq, rr * sp)
            nrm += (cq * cp, sq, cq * sp)
            tan += (-sp, 0.0, cp, 1.0)    # d(pos)/d(u), w = +1
            uv += (u, v)
    v1 = seg_v + 1
    for i in range(seg_u):
        for j in range(seg_v):
            a = i * v1 + j
            b = a + v1
            c = b + 1
            d = a + 1
            idx += (a, d, c, a, c, b)
    return pos, nrm, tan, uv, idx


def gen_cube_geometry(half=1.0):
    """
    2*half cube centered at the origin, 24 verts, every face maps UV 0..1.
    Per face: tangent T, bitangent B = cross(N, T) so that glTF tangent
    w = +1 (bitangent = cross(N, T) * w) is consistent, and triangle
    winding (0,1,2),(0,2,3) is CCW seen from outside.
    """
    faces = [
        ((1, 0, 0), (0, 0, -1)),   # +X
        ((-1, 0, 0), (0, 0, 1)),   # -X
        ((0, 1, 0), (1, 0, 0)),    # +Y
        ((0, -1, 0), (1, 0, 0)),   # -Y
        ((0, 0, 1), (1, 0, 0)),    # +Z
        ((0, 0, -1), (-1, 0, 0)),  # -Z
    ]
    corner_uv = ((0, 0), (1, 0), (1, 1), (0, 1))
    pos = []; nrm = []; tan = []; uv = []; idx = []
    for n, t in faces:
        bx = n[1] * t[2] - n[2] * t[1]
        by = n[2] * t[0] - n[0] * t[2]
        bz = n[0] * t[1] - n[1] * t[0]      # B = N x T
        base = len(pos) // 3
        for (cu, cv) in corner_uv:
            su = (2.0 * cu - 1.0) * half
            sv = (2.0 * cv - 1.0) * half
            pos += (n[0] * half + t[0] * su + bx * sv,
                    n[1] * half + t[1] * su + by * sv,
                    n[2] * half + t[2] * su + bz * sv)
            nrm += (float(n[0]), float(n[1]), float(n[2]))
            tan += (float(t[0]), float(t[1]), float(t[2]), 1.0)
            uv += (cu, cv)
        idx += (base, base + 1, base + 2, base, base + 2, base + 3)
    return pos, nrm, tan, uv, idx


def _minmax(vals, ncomp):
    n = len(vals) // ncomp
    mins = [vals[c] for c in range(ncomp)]
    maxs = [vals[c] for c in range(ncomp)]
    for i in range(ncomp, len(vals)):
        c = i % ncomp
        v = vals[i]
        if v < mins[c]:
            mins[c] = v
        elif v > maxs[c]:
            maxs[c] = v
    return mins, maxs


def build_mesh_buffers(pos, nrm, tan, uv, idx, buffer_index=0):
    """
    Pack one primitive's attribute/index arrays into a little-endian binary
    blob plus glTF bufferViews + accessors (4-byte aligned, POSITION gets
    mandatory min/max; other float attributes get min/max too).
    Index component type is UNSIGNED_SHORT unless > 65535 vertices.

    Returns (bin_bytes, bufferViews, accessors).
    """
    nverts = len(pos) // 3
    use_u32 = nverts > 65535
    binb = bytearray()
    views = []
    accessors = []

    def add(data, target, count, ctype, vtype, src=None):
        pad = (-len(binb)) % 4
        if pad:
            binb.extend(b"\x00" * pad)
        off = len(binb)
        binb.extend(data)
        views.append({"buffer": buffer_index, "byteOffset": off,
                      "byteLength": len(data), "target": target})
        acc = {"bufferView": len(views) - 1, "componentType": ctype,
               "count": count, "type": vtype}
        if src is not None:
            mn, mx = _minmax(src, NCOMP[vtype])
            acc["min"] = mn
            acc["max"] = mx
        accessors.append(acc)

    add(struct.pack("<%df" % len(pos), *pos), TARGET_ARRAY_BUFFER,
        nverts, COMP_FLOAT, "VEC3", src=pos)
    add(struct.pack("<%df" % len(nrm), *nrm), TARGET_ARRAY_BUFFER,
        nverts, COMP_FLOAT, "VEC3", src=nrm)
    add(struct.pack("<%df" % len(tan), *tan), TARGET_ARRAY_BUFFER,
        nverts, COMP_FLOAT, "VEC4", src=tan)
    add(struct.pack("<%df" % len(uv), *uv), TARGET_ARRAY_BUFFER,
        nverts, COMP_FLOAT, "VEC2", src=uv)
    if use_u32:
        fmt, ct = "<%dI" % len(idx), COMP_UINT
    else:
        fmt, ct = "<%dH" % len(idx), COMP_USHORT
    add(struct.pack(fmt, *idx), TARGET_ELEMENT_ARRAY_BUFFER,
        len(idx), ct, "SCALAR")
    return bytes(binb), views, accessors


def primitive_attrs(base):
    return ({"attributes": {"POSITION": base, "NORMAL": base + 1,
                            "TANGENT": base + 2, "TEXCOORD_0": base + 3},
             "indices": base + 4, "mode": 4})


MAT_SPHERE = {
    "name": "GenesisSphereMat",
    "pbrMetallicRoughness": {
        "baseColorFactor": [0.9, 0.75, 0.4, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.35,
    },
}

MAT_TORUS = {
    "name": "GenesisTorusMat",
    "pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 0.85, 0.5, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 0.15,
    },
}

MAT_CUBE = {
    "name": "GenesisCubeTexturedMat",
    "pbrMetallicRoughness": {
        "baseColorTexture": {"index": 0},
        "metallicRoughnessTexture": {"index": 1},
        # factors = 1.0 so the textures fully drive metallic/roughness/base
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
    },
    "normalTexture": {"index": 2, "scale": 1.0},
}

SAMPLER_REPEAT_LINEAR_MIP = {
    "magFilter": 9729,        # LINEAR
    "minFilter": 9987,        # LINEAR_MIPMAP_LINEAR
    "wrapS": 10497,           # REPEAT
    "wrapT": 10497,           # REPEAT
}

CUBE_IMAGES = [
    {"uri": "../textures/checker_albedo.png"},
    {"uri": "../textures/checker_metallic_roughness.png"},
    {"uri": "../textures/panel_normal.png"},
]
CUBE_TEXTURES = [
    {"sampler": 0, "source": 0},
    {"sampler": 0, "source": 1},
    {"sampler": 0, "source": 2},
]


def build_single_model_gltf(mesh_name, geo, material, bin_uri, textured=False):
    """Assemble a complete one-mesh glTF document + its .bin payload."""
    binb, views, accessors = build_mesh_buffers(*geo, buffer_index=0)
    prim = primitive_attrs(0)
    prim["material"] = 0
    doc = {
        "asset": dict(ASSET_INFO),
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [0]}],
        "nodes": [{"name": mesh_name, "mesh": 0}],
        "meshes": [{"name": mesh_name, "primitives": [prim]}],
        "materials": [material],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": bin_uri, "byteLength": len(binb)}],
    }
    if textured:
        doc["samplers"] = [dict(SAMPLER_REPEAT_LINEAR_MIP)]
        doc["images"] = [dict(i) for i in CUBE_IMAGES]
        doc["textures"] = [dict(t) for t in CUBE_TEXTURES]
    return doc, binb


def build_test_scene_gltf(geo_sphere, geo_torus, geo_cube, bin_names,
                          models_dir):
    """
    Multi-mesh scene: 3 nodes (sphere / torus / cube), each with
    translation + rotation + scale. Uses THREE buffers whose uris point at
    the already-generated per-model .bin files (glTF allows >1 buffer;
    cgltf handles it).
    """
    views = []
    accessors = []
    meshes = []
    buffers = []
    geos = [(geo_sphere, MAT_SPHERE, "GenesisSphere"),
            (geo_torus, MAT_TORUS, "GenesisTorus"),
            (geo_cube, MAT_CUBE, "GenesisCubeTextured")]
    for bi, (geo, _mat, name) in enumerate(geos):
        base = len(accessors)
        prim = primitive_attrs(base)
        prim["material"] = bi
        meshes.append({"name": name, "primitives": [prim]})
        buffers.append({"uri": bin_names[bi],
                        "byteLength": os.path.getsize(
                            os.path.join(models_dir, bin_names[bi]))})
        _b, v, a = build_mesh_buffers(*geo, buffer_index=bi)
        # accessor.bufferView indices are local to each geometry (0..4);
        # remap them into the concatenated bufferView list.
        for acc in a:
            acc["bufferView"] += base
        views.extend(v)
        accessors.extend(a)
    q = lambda ang_deg, axis: [  # quaternion [x,y,z,w]
        axis[0] * math.sin(math.radians(ang_deg) / 2),
        axis[1] * math.sin(math.radians(ang_deg) / 2),
        axis[2] * math.sin(math.radians(ang_deg) / 2),
        math.cos(math.radians(ang_deg) / 2)]
    nodes = [
        {"name": "Sphere", "mesh": 0,
         "translation": [-2.6, 0.2, 0.0],
         "rotation": q(30.0, (0, 1, 0)),
         "scale": [1.0, 1.0, 1.0]},
        {"name": "Torus", "mesh": 1,
         "translation": [0.0, 0.0, 0.0],
         "rotation": q(45.0, (1, 0, 0)),
         "scale": [1.0, 1.0, 1.0]},
        {"name": "CubeTextured", "mesh": 2,
         "translation": [2.6, 0.0, 0.0],
         "rotation": q(20.0, (0, 1, 0)),
         "scale": [0.9, 0.9, 0.9]},
    ]
    return {
        "asset": dict(ASSET_INFO),
        "scene": 0,
        "scenes": [{"name": "GenesisTestScene", "nodes": [0, 1, 2]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": [dict(m) for m in (MAT_SPHERE, MAT_TORUS, MAT_CUBE)],
        "samplers": [dict(SAMPLER_REPEAT_LINEAR_MIP)],
        "images": [dict(i) for i in CUBE_IMAGES],
        "textures": [dict(t) for t in CUBE_TEXTURES],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": buffers,
    }


def write_gltf(path, doc):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def write_glb(path, doc, bin_data):
    """
    Pack a glTF doc (whose buffers[0] has no uri) + binary into a spec-legal
    GLB: 12-byte header ('glTF', version 2, total length), chunk 0 = JSON
    (0x4E4F534A) padded with 0x20 to a 4-byte boundary, chunk 1 = BIN
    (0x004E4942) padded with 0x00 to a 4-byte boundary.
    """
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js_pad = (-len(js)) % 4
    js += b"\x20" * js_pad
    bin_pad = (-len(bin_data)) % 4
    binb = bin_data + b"\x00" * bin_pad
    total = 12 + 8 + len(js) + 8 + len(binb)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))    # 'JSON'
        f.write(js)
        f.write(struct.pack("<II", len(binb), 0x004E4942))  # 'BIN\0'
        f.write(binb)
    return len(js), len(binb)


# ============================================================================
# Generation driver
# ============================================================================

MANIFEST = []


def note(relpath, dims, info):
    full = os.path.join(OUT_DIR, relpath)
    MANIFEST.append((relpath, os.path.getsize(full), dims, info))


def generate_textures(tex_dir):
    print("== textures ==")

    px = gen_checker_albedo(512, 8)
    write_png(os.path.join(tex_dir, "checker_albedo.png"), 512, 512, px, 4)
    note("textures/checker_albedo.png", "512x512", "RGBA8 sRGB, 8x8 cells, "
         "cell(0,0)=white")

    px = gen_checker_metallic_roughness(512, 8)
    write_png(os.path.join(tex_dir, "checker_metallic_roughness.png"),
              512, 512, px, 4)
    note("textures/checker_metallic_roughness.png", "512x512",
         "white=(255,230,0) gray=(255,50,255) G=rough B=metal")

    hmap = gen_panel_height(256, 4, groove_width=5.0)
    nrm = height_to_normal(hmap, 256, 256, strength=3.0)
    write_png(os.path.join(tex_dir, "panel_normal.png"), 256, 256, nrm, 3)
    note("textures/panel_normal.png", "256x256",
         "RGB, central-difference from height, OpenGL +Y up, flat=(128,128,255)")

    hp = bytearray()
    for row in hmap:
        for v in row:
            b = int(clamp(v * 255.0 + 0.5, 0, 255))
            hp += bytes((b, b, b, 255))
    write_png(os.path.join(tex_dir, "panel_height.png"), 256, 256, hp, 4)
    note("textures/panel_height.png", "256x256",
         "RGBA gray, 4x4 panels, groove_width=5px")

    px = gen_tile_ao(256, 4, fade=12.0)
    write_png(os.path.join(tex_dir, "tile_ao.png"), 256, 256, px, 3)
    note("textures/tile_ao.png", "256x256",
         "RGB gray: 255 center -> 90 seam, fade=12px")

    px = gen_grid_emissive(512, 64, 2)
    write_png(os.path.join(tex_dir, "grid_emissive.png"), 512, 512, px, 3)
    note("textures/grid_emissive.png", "512x512",
         "sRGB, black + (0,255,220) 2px lines every 64px")

    px = gen_uv_gradient(512)
    write_png(os.path.join(tex_dir, "uv_gradient.png"), 512, 512, px, 4)
    note("textures/uv_gradient.png", "512x512",
         "R=u G=v B=128 A=255; row0=v=0=top")

    t0 = time.time()
    rgb = gen_equirect_sky(2048, 1024, seed=1337, sun_azimuth_deg=45.0,
                           sun_elevation_deg=35.0, num_clouds=4, octaves=3)
    write_hdr(os.path.join(tex_dir, "sky_equirect.hdr"), 2048, 1024, rgb)
    dt = time.time() - t0
    note("textures/sky_equirect.hdr", "2048x1024",
         "RGBE equirect, sun @ az45 el35 peak~%.0f, 4 fbm clouds, %.1fs"
         % (SUN_PEAK, dt))
    print("   sky_equirect.hdr generated in %.1f s" % dt, flush=True)

    t0 = time.time()
    rgb = gen_equirect_sky(256, 128, seed=1337, sun_azimuth_deg=45.0,
                           sun_elevation_deg=35.0, num_clouds=3, octaves=3,
                           simplified=True)
    write_hdr(os.path.join(tex_dir, "small_equirect.hdr"), 256, 128, rgb)
    dt = time.time() - t0
    note("textures/small_equirect.hdr", "256x128",
         "RGBE equirect, simplified (3 clouds, 2 octaves), %.1fs" % dt)


def generate_models(models_dir):
    print("== models ==")

    # --- sphere (gltf + bin + glb) ---
    geo_sphere = gen_sphere_geometry(1.0, 48, 32)
    nverts = len(geo_sphere[0]) // 3
    nidx = len(geo_sphere[4])
    doc, binb = build_single_model_gltf("GenesisSphere", geo_sphere,
                                        MAT_SPHERE, "genesis_sphere.bin")
    with open(os.path.join(models_dir, "genesis_sphere.bin"), "wb") as f:
        f.write(binb)
    write_gltf(os.path.join(models_dir, "genesis_sphere.gltf"), doc)
    note("models/genesis_sphere.gltf", "%d v / %d i" % (nverts, nidx),
         "r=1.0, 48x32, UNSIGNED_SHORT indices, min/max=%s/%s"
         % ([round(v, 3) for v in doc["accessors"][0]["min"]],
            [round(v, 3) for v in doc["accessors"][0]["max"]]))
    note("models/genesis_sphere.bin", "%d v / %d i" % (nverts, nidx),
         "%d bytes, 5 bufferViews (P/N/T/UV/IDX)" % len(binb))

    glb_doc = json.loads(json.dumps(doc))
    glb_doc["buffers"] = [{"byteLength": len(binb) + (-len(binb)) % 4}]
    jlen, blen = write_glb(os.path.join(models_dir, "genesis_sphere.glb"),
                           glb_doc, binb)
    note("models/genesis_sphere.glb", "%d v / %d i" % (nverts, nidx),
         "JSON chunk %dB, BIN chunk %dB" % (jlen, blen))

    # --- torus ---
    geo_torus = gen_torus_geometry(1.0, 0.35, 64, 32)
    nverts = len(geo_torus[0]) // 3
    nidx = len(geo_torus[4])
    doc, binb = build_single_model_gltf("GenesisTorus", geo_torus,
                                        MAT_TORUS, "genesis_torus.bin")
    with open(os.path.join(models_dir, "genesis_torus.bin"), "wb") as f:
        f.write(binb)
    write_gltf(os.path.join(models_dir, "genesis_torus.gltf"), doc)
    note("models/genesis_torus.gltf", "%d v / %d i" % (nverts, nidx),
         "R=1.0 r=0.35, 64x32, metal=1.0 rough=0.15")
    note("models/genesis_torus.bin", "%d v / %d i" % (nverts, nidx),
         "%d bytes" % len(binb))

    # --- textured cube ---
    geo_cube = gen_cube_geometry(1.0)
    nverts = len(geo_cube[0]) // 3
    nidx = len(geo_cube[4])
    doc, binb = build_single_model_gltf("GenesisCubeTextured", geo_cube,
                                        MAT_CUBE, "genesis_cube_textured.bin",
                                        textured=True)
    with open(os.path.join(models_dir, "genesis_cube_textured.bin"), "wb") as f:
        f.write(binb)
    write_gltf(os.path.join(models_dir, "genesis_cube_textured.gltf"), doc)
    note("models/genesis_cube_textured.gltf", "%d v / %d i" % (nverts, nidx),
         "2x2x2, 3 images (../textures/*.png), metal/rough factors=1.0")
    note("models/genesis_cube_textured.bin", "%d v / %d i" % (nverts, nidx),
         "%d bytes" % len(binb))

    # --- multi-node test scene (reuses the 3 external .bin files) ---
    scene = build_test_scene_gltf(geo_sphere, geo_torus, geo_cube,
                                  ["genesis_sphere.bin", "genesis_torus.bin",
                                   "genesis_cube_textured.bin"], models_dir)
    write_gltf(os.path.join(models_dir, "genesis_test_scene.gltf"), scene)
    note("models/genesis_test_scene.gltf", "3 nodes / 3 buffers",
         "sphere+torus+cube with TRS, buffers[].uri -> per-model .bin")

    return geo_sphere, geo_torus, geo_cube


def print_manifest():
    print()
    print("%-44s %10s  %-16s  %s"
          % ("file", "size", "dims / verts", "check"))
    print("-" * 130)
    for rel, size, dims, info in MANIFEST:
        print("%-44s %10d  %-16s  %s" % (rel, size, dims, info))
    print("-" * 130)
    print("%d files, %d bytes total"
          % (len(MANIFEST), sum(m[1] for m in MANIFEST)))


OUT_DIR = None


def generate_all(outdir):
    global OUT_DIR
    OUT_DIR = outdir
    tex_dir = os.path.join(outdir, "textures")
    models_dir = os.path.join(outdir, "models")
    os.makedirs(tex_dir, exist_ok=True)
    os.makedirs(models_dir, exist_ok=True)
    generate_textures(tex_dir)
    generate_models(models_dir)
    print_manifest()


# ============================================================================
# D. Verification (--verify)
# ============================================================================

def validate_gltf_doc(doc, label, buffer_bytes, base_dir, errs):
    """Strict structural validator for a glTF 2.0 document (cgltf-safe)."""
    def err(msg):
        errs.append("%s: %s" % (label, msg))

    if doc.get("asset", {}).get("version") != "2.0":
        err("asset.version != 2.0")
    if "scene" in doc:
        if not (0 <= doc["scene"] < len(doc.get("scenes", []))):
            err("scene index out of range")
    scenes = doc.get("scenes", [])
    nodes = doc.get("nodes", [])
    meshes = doc.get("meshes", [])
    accessors = doc.get("accessors", [])
    views = doc.get("bufferViews", [])
    buffers = doc.get("buffers", [])
    materials = doc.get("materials", [])
    images = doc.get("images", [])
    textures = doc.get("textures", [])

    for si, sc in enumerate(scenes):
        for ni in sc.get("nodes", []):
            if not (0 <= ni < len(nodes)):
                err("scene %d references bad node %d" % (si, ni))
    for ni, nd in enumerate(nodes):
        if "mesh" in nd and not (0 <= nd["mesh"] < len(meshes)):
            err("node %d bad mesh index" % ni)
        if "rotation" in nd:
            qv = nd["rotation"]
            if len(qv) != 4 or abs(sum(c * c for c in qv) - 1.0) > 1e-3:
                err("node %d rotation not a unit quaternion" % ni)
    if len(buffers) != len(buffer_bytes):
        err("expected %d buffers, got %d" % (len(buffer_bytes), len(buffers)))
        return
    for bi, buf in enumerate(buffers):
        if buf.get("byteLength") != len(buffer_bytes[bi]):
            err("buffer %d byteLength %s != actual %d"
                % (bi, buf.get("byteLength"), len(buffer_bytes[bi])))
        if "uri" in buf and not buf["uri"].startswith("data:"):
            p = os.path.normpath(os.path.join(base_dir, buf["uri"]))
            if not os.path.isfile(p):
                err("buffer %d uri %s missing" % (bi, buf["uri"]))
    for vi, v in enumerate(views):
        bi = v.get("buffer", 0)
        if not (0 <= bi < len(buffers)):
            err("bufferView %d bad buffer" % vi)
            continue
        if v.get("byteOffset", 0) % 4 != 0:
            err("bufferView %d byteOffset not 4-aligned" % vi)
        end = v.get("byteOffset", 0) + v["byteLength"]
        if end > len(buffer_bytes[bi]):
            err("bufferView %d exceeds buffer %d" % (vi, bi))
    for ai, a in enumerate(accessors):
        vi = a["bufferView"]
        if not (0 <= vi < len(views)):
            err("accessor %d bad bufferView" % ai)
            continue
        v = views[vi]
        ncomp = NCOMP.get(a["type"])
        csz = COMP_SIZE.get(a["componentType"])
        if ncomp is None or csz is None:
            err("accessor %d bad type/componentType" % ai)
            continue
        need = a["count"] * ncomp * csz
        off = a.get("byteOffset", 0)
        if off + need > v["byteLength"]:
            err("accessor %d does not fit its bufferView" % ai)
    for mi, m in enumerate(meshes):
        for pi, p in enumerate(m.get("primitives", [])):
            attrs = p.get("attributes", {})
            if "POSITION" not in attrs:
                err("mesh %d prim %d has no POSITION" % (mi, pi))
                continue
            pa = accessors[attrs["POSITION"]]
            if pa["type"] != "VEC3" or pa["componentType"] != COMP_FLOAT:
                err("mesh %d prim %d POSITION not float VEC3" % (mi, pi))
            if "min" not in pa or "max" not in pa \
                    or len(pa["min"]) != 3 or len(pa["max"]) != 3:
                err("mesh %d prim %d POSITION accessor lacks min/max "
                    "(required by spec)" % (mi, pi))
            if "indices" in p:
                ia = accessors[p["indices"]]
                if ia["type"] != "SCALAR" or ia["componentType"] not in \
                        (5121, COMP_USHORT, COMP_UINT):
                    err("mesh %d prim %d bad indices accessor" % (mi, pi))
                elif ia["count"] % 3 != 0:
                    err("mesh %d prim %d index count %% 3 != 0" % (mi, pi))
            if "material" in p and not (0 <= p["material"] < len(materials)):
                err("mesh %d prim %d bad material index" % (mi, pi))
    for mat in materials:
        pbr = mat.get("pbrMetallicRoughness", {})
        for key in ("baseColorTexture", "metallicRoughnessTexture"):
            if key in pbr:
                ti = pbr[key]["index"]
                if not (0 <= ti < len(textures)):
                    err("material texture index out of range (%s)" % key)
        if "normalTexture" in mat:
            ti = mat["normalTexture"]["index"]
            if not (0 <= ti < len(textures)):
                err("normalTexture index out of range")
    for ti, t in enumerate(textures):
        if "source" in t and not (0 <= t["source"] < len(images)):
            err("texture %d bad source" % ti)
    for ii, im in enumerate(images):
        if "uri" in im and not im["uri"].startswith("data:"):
            p = os.path.normpath(os.path.join(base_dir, im["uri"]))
            if not os.path.isfile(p):
                err("image %d uri %s missing" % (ii, im["uri"]))


def check_geometry_from_bin(label, doc, binb, errs, checks):
    """Re-read POSITION/indices from the binary and cross-check the JSON."""
    def err(msg):
        errs.append("%s: %s" % (label, msg))

    prim = doc["meshes"][0]["primitives"][0] if len(doc["meshes"]) == 1 \
        else doc["meshes"][0]["primitives"][0]
    pa = doc["accessors"][prim["attributes"]["POSITION"]]
    ia = doc["accessors"][prim["indices"]]
    pv = doc["bufferViews"][pa["bufferView"]]
    iv = doc["bufferViews"][ia["bufferView"]]
    nverts = pa["count"]
    floats = struct.unpack_from("<%df" % (nverts * 3), binb,
                                pv["byteOffset"] + pa.get("byteOffset", 0))
    mn = [min(floats[c::3]) for c in range(3)]
    mx = [max(floats[c::3]) for c in range(3)]
    for c in range(3):
        if abs(mn[c] - pa["min"][c]) > 1e-4 or abs(mx[c] - pa["max"][c]) > 1e-4:
            err("POSITION min/max mismatch (recomputed %s/%s vs %s/%s)"
                % (mn, mx, pa["min"], pa["max"]))
    fmt = {COMP_USHORT: "H", COMP_UINT: "I"}[ia["componentType"]]
    idxs = struct.unpack_from("<%d%s" % (ia["count"], fmt), binb,
                              iv["byteOffset"] + ia.get("byteOffset", 0))
    if max(idxs) >= nverts:
        err("index %d out of range (nverts=%d)" % (max(idxs), nverts))
    if len(set(idxs)) < 3:
        err("degenerate index buffer")
    # normals unit length (sample every 37th)
    na = doc["accessors"][prim["attributes"]["NORMAL"]]
    nv = doc["bufferViews"][na["bufferView"]]
    nrm = struct.unpack_from("<%df" % (nverts * 3), binb,
                             nv["byteOffset"] + na.get("byteOffset", 0))
    bad = 0
    for i in range(0, nverts * 3, 3 * 37):
        l = math.sqrt(nrm[i] ** 2 + nrm[i + 1] ** 2 + nrm[i + 2] ** 2)
        if abs(l - 1.0) > 1e-3:
            bad += 1
    if bad:
        err("%d sampled normals not unit length" % bad)
    checks.append("%s bin cross-check: %d verts, %d indices, idxmax=%d, "
                  "min/max recomputed OK, normals unit OK"
                  % (label, nverts, ia["count"], max(idxs)))


def verify_all(outdir):
    tex = os.path.join(outdir, "textures")
    models = os.path.join(outdir, "models")
    passed = []
    failed = []
    infos = []

    def check(name, cond, detail=""):
        (passed if cond else failed).append(name)
        print("[%s] %s%s" % ("PASS" if cond else "FAIL", name,
                             ("  (%s)" % detail) if detail else ""))

    print("== verify: textures ==")

    # --- checker_albedo ---
    p = os.path.join(tex, "checker_albedo.png")
    w, h, ch, px = read_png(p)
    check("checker_albedo.png IHDR reparse 512x512 RGBA",
          (w, h, ch) == (512, 512, 4))
    check("checker_albedo (0,0) == white (255,255,255,255)",
          png_pixel(px, w, ch, 0, 0) == (255, 255, 255, 255),
          "got %s" % (png_pixel(px, w, ch, 0, 0),))
    # NOTE on the spec's "(32,32) gray": with an 8x8 checker at 512 the cell
    # size is 64 px, so (32,32) is the CENTER OF THE SAME white cell as
    # (0,0) and cannot be gray in any aligned checkerboard. We assert the
    # geometric truth instead: (32,32) == white cell interior, and the gray
    # cell centre at (96,32) (= cell (1,0)).
    check("checker_albedo (32,32) == white (center of cell(0,0), same as "
          "(0,0)) [see NOTE]",
          png_pixel(px, w, ch, 32, 32) == (255, 255, 255, 255),
          "got %s" % (png_pixel(px, w, ch, 32, 32),))
    check("checker_albedo (96,32) == gray (128,128,128,255) "
          "[cell(1,0) center]",
          png_pixel(px, w, ch, 96, 32) == (128, 128, 128, 255),
          "got %s" % (png_pixel(px, w, ch, 96, 32),))
    check("checker_albedo (64,0) == gray (first pixel of cell(1,0))",
          png_pixel(px, w, ch, 64, 0) == (128, 128, 128, 255))
    # seamlessness: cell count is even, so the pattern continues across the
    # wrap: x=511 is the last pixel of gray cell 7, adjacent to white cell 0
    # of the next tile.
    check("checker_albedo tileable: (511,0) is gray cell 7 (pattern wraps)",
          png_pixel(px, w, ch, 511, 0) == (128, 128, 128, 255),
          "got %s" % (png_pixel(px, w, ch, 511, 0),))

    # --- checker_metallic_roughness ---
    w2, h2, ch2, px2 = read_png(
        os.path.join(tex, "checker_metallic_roughness.png"))
    check("checker_metallic_roughness white cell (0,0) == (255,230,0,255) "
          "[G=rough 230, B=metal 0]",
          png_pixel(px2, w2, ch2, 0, 0) == (255, 230, 0, 255),
          "got %s" % (png_pixel(px2, w2, ch2, 0, 0),))
    check("checker_metallic_roughness gray cell (96,32) == (255,50,255,255) "
          "[G=rough 50, B=metal 255]",
          png_pixel(px2, w2, ch2, 96, 32) == (255, 50, 255, 255),
          "got %s" % (png_pixel(px2, w2, ch2, 96, 32),))

    # --- uv_gradient ---
    w3, h3, ch3, px3 = read_png(os.path.join(tex, "uv_gradient.png"))
    p00 = png_pixel(px3, w3, ch3, 0, 0)
    pff = png_pixel(px3, w3, ch3, 511, 511)
    check("uv_gradient (0,0) R~=0 G~=0 (v=0 at row 0 = top)",
          abs(p00[0]) <= 2 and abs(p00[1]) <= 2, "got %s" % (p00,))
    check("uv_gradient (511,511) R~=255 G~=255",
          abs(pff[0] - 255) <= 2 and abs(pff[1] - 255) <= 2,
          "got %s" % (pff,))
    check("uv_gradient B == 128 (0.5) and A == 255 everywhere sampled",
          p00[2] == 128 and p00[3] == 255 and pff[2] == 128 and pff[3] == 255)

    # --- panel_normal / panel_height structural checks ---
    w4, h4, ch4, px4 = read_png(os.path.join(tex, "panel_normal.png"))
    flat = png_pixel(px4, w4, ch4, 32, 32)   # cell center -> flat
    check("panel_normal flat area == (128,128,255)",
          flat == (128, 128, 255), "got %s" % (flat,))
    left = png_pixel(px4, w4, ch4, 255, 32)  # left lip of seam at x=0/256
    right = png_pixel(px4, w4, ch4, 1, 32)   # right lip
    check("panel_normal seam side walls tilt +/-X (R>140 left, R<116 right)",
          left[0] > 140 and right[0] < 116,
          "left=%s right=%s" % (left, right))
    up = png_pixel(px4, w4, ch4, 32, 255)    # upper lip of seam at y=0/256
    down = png_pixel(px4, w4, ch4, 32, 1)    # lower lip
    check("panel_normal GREEN UP: upper seam lip G>140, lower lip G<116 "
          "(OpenGL +Y convention)",
          up[1] > 140 and down[1] < 116, "up=%s down=%s" % (up, down))
    w5, h5, ch5, px5 = read_png(os.path.join(tex, "panel_height.png"))
    check("panel_height seam==0 plateau==255 (aligned with normal map)",
          png_pixel(px5, w5, ch5, 0, 32)[0] == 0
          and png_pixel(px5, w5, ch5, 32, 32)[0] == 255)

    # --- tile_ao ---
    w6, h6, ch6, px6 = read_png(os.path.join(tex, "tile_ao.png"))
    c = png_pixel(px6, w6, ch6, 32, 32)
    s = png_pixel(px6, w6, ch6, 0, 32)
    check("tile_ao center==255, seam==90, R==G==B",
          c[0] == 255 and s[0] == 90 and c[0] == c[1] == c[2]
          and s[0] == s[1] == s[2], "center=%s seam=%s" % (c, s))

    # --- grid_emissive ---
    w7, h7, ch7, px7 = read_png(os.path.join(tex, "grid_emissive.png"))
    check("grid_emissive line==(0,255,220), base==(0,0,0)",
          png_pixel(px7, w7, ch7, 0, 5) == (0, 255, 220)
          and png_pixel(px7, w7, ch7, 5, 0) == (0, 255, 220)
          and png_pixel(px7, w7, ch7, 32, 32) == (0, 0, 0))

    # --- sky_equirect.hdr ---
    print("== verify: HDR ==")
    sw, sh, srgb = read_hdr(os.path.join(tex, "sky_equirect.hdr"))
    check("sky_equirect.hdr resolution 2048x1024 (2:1)",
          (sw, sh) == (2048, 1024) and sw == 2 * sh)
    mx = max(srgb)
    mean = sum(srgb) / len(srgb)
    lum = (0.2126 * sum(srgb[0::3]) + 0.7152 * sum(srgb[1::3])
           + 0.0722 * sum(srgb[2::3])) / (sw * sh)
    check("sky_equirect.hdr max > 50 (sun disc)", mx > 50.0,
          "max=%.1f" % mx)
    check("sky_equirect.hdr mean in [0.1, 10]", 0.1 <= mean <= 10.0,
          "mean(rgb)=%.3f, mean luminance=%.3f" % (mean, lum))
    zen = (srgb[0], srgb[1], srgb[2])
    check("sky_equirect.hdr row0 = zenith ~ (0.05,0.12,0.35) (+Y at top)",
          abs(zen[0] - 0.05) < 0.02 and abs(zen[1] - 0.12) < 0.02
          and abs(zen[2] - 0.35) < 0.03, "got (%.3f,%.3f,%.3f)" % zen)
    hw2, hh2, hrgb2 = read_hdr(os.path.join(tex, "small_equirect.hdr"))
    check("small_equirect.hdr 256x128 and max > 50",
          (hw2, hh2) == (256, 128) and max(hrgb2) > 50.0,
          "max=%.1f" % max(hrgb2))

    # --- TGA encoder round-trip self-test (temp file, no asset pollution) ---
    with tempfile.TemporaryDirectory() as td:
        tp = os.path.join(td, "t.tga")
        src = bytearray()
        for y in range(4):
            for x in range(4):
                src += bytes((x * 60, y * 60, (x + y) * 30, 255))
        write_tga(tp, 4, 4, src)
        tw, th, back = read_tga(tp)
        check("TGA round-trip (y-flip, BGRA) exact",
              (tw, th) == (4, 4) and bytes(back) == bytes(src))

    # --- HDR encoder round-trip self-test ---
    with tempfile.TemporaryDirectory() as td:
        tp = os.path.join(td, "t.hdr")
        vals = array("f", [0.0, 0.0, 0.0, 1e-4, 0.5, 2.0,
                           300.0, 12.5, 0.75, 1.0, 1.0, 1.0])
        write_hdr(tp, 4, 1, vals)
        rw, rh, rback = read_hdr(tp)
        # RGBE quantization: all 3 channels share ONE exponent per pixel, so
        # the absolute error of a channel is bounded by ~0.5 LSB of the
        # pixel's MAX component (<= max/256), plus the +0.5 decode bias.
        rel = 0.0
        for p in range(4):
            pmax = max(vals[3 * p], vals[3 * p + 1], vals[3 * p + 2])
            tol = max(pmax, 1e-6)
            for c in range(3):
                e = abs(rback[3 * p + c] - vals[3 * p + c]) / tol
                rel = max(rel, e)
        check("HDR RGBE round-trip within 0.8% of per-pixel max component",
              (rw, rh) == (4, 1) and rel < 0.008, "max rel err=%.4f" % rel)

    # --- glTF / GLB ---
    print("== verify: glTF / GLB ==")
    errs = []

    def load_bin(name):
        with open(os.path.join(models, name), "rb") as f:
            return f.read()

    # sphere .gltf + .bin
    with open(os.path.join(models, "genesis_sphere.gltf"), "r",
              encoding="utf-8") as f:
        sdoc = json.load(f)
    sbin = load_bin("genesis_sphere.bin")
    validate_gltf_doc(sdoc, "genesis_sphere.gltf", [sbin], models, errs)
    check("genesis_sphere.gltf JSON parses + strict validation",
          not [e for e in errs if e.startswith("genesis_sphere.gltf")])
    check("genesis_sphere.gltf buffers[0].byteLength == bin file size (%d)"
          % len(sbin),
          sdoc["buffers"][0]["byteLength"] == len(sbin))
    exp_verts = (32 + 1) * (48 + 1)
    check("genesis_sphere POSITION accessor count == %d" % exp_verts,
          sdoc["accessors"][0]["count"] == exp_verts)
    check_geometry_from_bin("genesis_sphere", sdoc, sbin, errs, infos)

    # torus
    with open(os.path.join(models, "genesis_torus.gltf"), "r",
              encoding="utf-8") as f:
        tdoc = json.load(f)
    tbin = load_bin("genesis_torus.bin")
    validate_gltf_doc(tdoc, "genesis_torus.gltf", [tbin], models, errs)
    check("genesis_torus.gltf JSON parses + strict validation (%d verts)"
          % ((64 + 1) * (32 + 1)),
          not [e for e in errs if e.startswith("genesis_torus.gltf")]
          and tdoc["accessors"][0]["count"] == 65 * 33)
    check_geometry_from_bin("genesis_torus", tdoc, tbin, errs, infos)

    # cube textured
    with open(os.path.join(models, "genesis_cube_textured.gltf"), "r",
              encoding="utf-8") as f:
        cdoc = json.load(f)
    cbin = load_bin("genesis_cube_textured.bin")
    validate_gltf_doc(cdoc, "genesis_cube_textured.gltf", [cbin], models, errs)
    check("genesis_cube_textured.gltf valid, 24 verts / 36 indices, "
          "3 image URIs resolve on disk",
          not [e for e in errs if e.startswith("genesis_cube_textured")]
          and cdoc["accessors"][0]["count"] == 24
          and cdoc["accessors"][4]["count"] == 36
          and len(cdoc["images"]) == 3)
    mat = cdoc["materials"][0]["pbrMetallicRoughness"]
    check("cube material factors == 1.0 (textures drive PBR) + normalTexture",
          mat["metallicFactor"] == 1.0 and mat["roughnessFactor"] == 1.0
          and "normalTexture" in cdoc["materials"][0])
    check_geometry_from_bin("genesis_cube_textured", cdoc, cbin, errs, infos)

    # test scene
    with open(os.path.join(models, "genesis_test_scene.gltf"), "r",
              encoding="utf-8") as f:
        xdoc = json.load(f)
    xbins = [load_bin(b["uri"]) for b in xdoc["buffers"]]
    validate_gltf_doc(xdoc, "genesis_test_scene.gltf", xbins, models, errs)
    check("genesis_test_scene.gltf: 3 nodes w/ TRS, 3 buffers[].uri resolve, "
          "byteLengths match",
          not [e for e in errs if e.startswith("genesis_test_scene")]
          and len(xdoc["nodes"]) == 3
          and all("translation" in n and "rotation" in n and "scale" in n
                  for n in xdoc["nodes"])
          and len(xdoc["buffers"]) == 3)

    # sphere GLB
    with open(os.path.join(models, "genesis_sphere.glb"), "rb") as f:
        glb = f.read()
    ok = True
    detail = ""
    try:
        magic, version, total = struct.unpack_from("<4sII", glb, 0)
        ok &= magic == b"glTF" and version == 2 and total == len(glb)
        jlen, jtype = struct.unpack_from("<II", glb, 12)
        blen, btype = struct.unpack_from("<II", glb, 20 + jlen)
        ok &= jtype == 0x4E4F534A and btype == 0x004E4942
        ok &= jlen % 4 == 0 and blen % 4 == 0
        ok &= 12 + 8 + jlen + 8 + blen == len(glb)
        gdoc = json.loads(glb[20:20 + jlen].decode("utf-8"))
        gbin = glb[28 + jlen:28 + jlen + blen]
        ok &= "uri" not in gdoc["buffers"][0]
        ok &= gdoc["buffers"][0]["byteLength"] <= blen
        pa = gdoc["accessors"][gdoc["meshes"][0]["primitives"][0]
                               ["attributes"]["POSITION"]]
        ok &= pa["count"] == exp_verts
        ok &= all(abs(pa["min"][c] + 1.0) < 1e-5
                  and abs(pa["max"][c] - 1.0) < 1e-5 for c in range(3))
        detail = ("magic=%s ver=%d total=%d JSON=%dB(0x%X) BIN=%dB(0x%X) "
                  "pos.count=%d min=%s max=%s"
                  % (magic, version, total, jlen, jtype, blen, btype,
                     pa["count"], [round(v, 2) for v in pa["min"]],
                     [round(v, 2) for v in pa["max"]]))
    except Exception as ex:  # noqa: BLE001
        ok = False
        detail = "exception: %s" % ex
    check("genesis_sphere.glb: header/chunks/JSON/POSITION all spec-legal",
          ok, detail)
    for line in infos:
        print("      info: %s" % line)

    # pre-existing sample models untouched
    for name in ("DamagedHelmet.glb", "Duck.glb"):
        p = os.path.join(models, name)
        check("pre-existing %s still present" % name, os.path.isfile(p),
              "%d bytes" % os.path.getsize(p) if os.path.isfile(p) else "")

    print()
    if errs:
        print("VALIDATION ERRORS:")
        for e in errs:
            print("  - %s" % e)
    print("verify summary: %d passed, %d failed" % (len(passed), len(failed)))
    return not failed and not errs


# ============================================================================
# main
# ============================================================================

def main():
    ap = argparse.ArgumentParser(
        description="Genesis-Render procedural test asset generator "
                    "(stdlib only, deterministic)")
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    ap.add_argument("--out-dir", default=None,
                    help="output root (default: <repo>/assets)")
    ap.add_argument("--verify", action="store_true",
                    help="re-read generated assets and run assertions "
                         "instead of generating")
    args = ap.parse_args()
    outdir = args.out_dir or os.path.join(repo_root, "assets")
    if args.verify:
        ok = verify_all(outdir)
        print("ALL CHECKS PASSED" if ok else "SOME CHECKS FAILED")
        sys.exit(0 if ok else 1)
    t0 = time.time()
    generate_all(outdir)
    print("done in %.1f s -> %s" % (time.time() - t0, outdir))


if __name__ == "__main__":
    main()
