#!/usr/bin/env python3
"""
Genesis Render - procedural 3D Gaussian Splat sample generator.

Produces a binary little-endian PLY with the standard 3DGS attributes:
    x y z nx ny nz f_dc_0 f_dc_1 f_dc_2 opacity scale_0 scale_1 scale_2 rot_0..3

The sample is a double helix + torus knot + star field, which makes the
anisotropic scale and depth sorting easy to judge in the viewer.

    python3 tools/gen_gsplat.py assets/splats/genesis_gaussians.ply --count 16000
"""
from __future__ import annotations

import argparse
import math
import random
import struct
from pathlib import Path

SH_C0 = 0.28209479177387814


def logit(p: float) -> float:
    p = max(1e-4, min(1.0 - 1e-4, p))
    return math.log(p / (1.0 - p))


def encode_color(r: float, g: float, b: float) -> tuple[float, float, float]:
    return ((r - 0.5) / SH_C0, (g - 0.5) / SH_C0, (b - 0.5) / SH_C0)


def generate(count: int, seed: int) -> list[tuple[float, ...]]:
    rng = random.Random(seed)
    splats: list[tuple[float, ...]] = []

    helix_count = int(count * 0.62)
    for i in range(helix_count):
        t = i / max(1, helix_count - 1)
        y = -1.8 + 3.6 * t
        angle = t * math.pi * 8.0
        strand = i % 2
        radius = 0.95
        x = math.cos(angle + strand * math.pi) * radius
        z = math.sin(angle + strand * math.pi) * radius
        jitter = 0.035
        x += rng.gauss(0.0, jitter)
        y += rng.gauss(0.0, jitter)
        z += rng.gauss(0.0, jitter)
        if strand == 0:
            color = (0.15 + 0.35 * t, 0.85, 1.0 - 0.2 * t)
        else:
            color = (1.0 - 0.15 * t, 0.25 + 0.25 * t, 0.9)
        scale = math.exp(rng.uniform(-5.8, -4.8))
        splats.append((x, y + 2.1, z, 0, 0, 1, *encode_color(*color),
                       logit(rng.uniform(0.30, 0.70)), scale, scale, scale * 0.7, 1.0, 0.0, 0.0, 0.0))

    knot_count = int(count * 0.28)
    for i in range(knot_count):
        t = i / max(1, knot_count - 1) * math.tau
        p = 0.55
        q = 1.7
        r = 1.35 + 0.45 * math.cos(q * t)
        x = r * math.cos(p * t)
        y = 1.8 + 0.5 * math.sin(q * t)
        z = r * math.sin(p * t)
        x += rng.gauss(0.0, 0.03)
        y += rng.gauss(0.0, 0.03)
        z += rng.gauss(0.0, 0.03)
        color = (1.0, 0.72 + 0.25 * math.sin(t), 0.18 + 0.2 * math.cos(t * 2.0))
        scale = math.exp(rng.uniform(-5.7, -4.7))
        splats.append((x, y, z, 0, 0, 1, *encode_color(*color),
                       logit(rng.uniform(0.28, 0.65)), scale, scale, scale, 1.0, 0.0, 0.0, 0.0))

    while len(splats) < count:
        # star field / ambient cloud
        theta = rng.uniform(0.0, math.tau)
        phi = math.acos(rng.uniform(-1.0, 1.0))
        radius = rng.uniform(5.2, 7.0)
        x = radius * math.sin(phi) * math.cos(theta)
        y = 2.0 + radius * math.cos(phi) * 0.7
        z = radius * math.sin(phi) * math.sin(theta)
        brightness = rng.uniform(0.4, 1.0)
        color = (brightness, brightness * rng.uniform(0.85, 1.0), 1.0)
        scale = math.exp(rng.uniform(-6.2, -5.2))
        splats.append((x, y, z, 0, 0, 1, *encode_color(*color),
                       logit(rng.uniform(0.20, 0.5)), scale, scale, scale, 1.0, 0.0, 0.0, 0.0))
    return splats


def write_ply(path: Path, splats: list[tuple[float, ...]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (
        "ply\n"
        "format binary_little_endian 1.0\n"
        "comment Genesis Render procedural 3D Gaussian splat sample\n"
        f"element vertex {len(splats)}\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property float nx\nproperty float ny\nproperty float nz\n"
        "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
        "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n"
        "end_header\n"
    )
    with path.open("wb") as handle:
        handle.write(header.encode("ascii"))
        for splat in splats:
            handle.write(struct.pack("<17f", *splat))
    print(f"wrote {path} ({path.stat().st_size} bytes, {len(splats)} splats)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--count", type=int, default=12000)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()
    write_ply(args.output, generate(max(100, args.count), args.seed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
