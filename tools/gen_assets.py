#!/usr/bin/env python3
"""
Genesis demo 资产生成器:
  - assets/env/sky.hdr            程序化 equirect HDR 环境图 (Radiance RGBE)
  - assets/textures/*.png         程序化 PBR 贴图 (albedo/normal/mr/ao)
  - assets/models/sphere_pbr.gltf 带 PBR 材质的 UV 球 (glTF 2.0)
  - assets/scripts/*.lua          demo 脚本
之后可用 assimp CLI 将 gltf 转为 fbx:
  assimp export sphere_pbr.gltf sphere_pbr.fbx
"""
import json
import math
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("PIL required")
    sys.exit(1)

ROOT = os.path.dirname(os.path.abspath(__file__)) + "/.."
ASSETS = os.path.join(ROOT, "assets")


# ------------------------------------------------------------------
# Radiance HDR (RGBE) 写出
# ------------------------------------------------------------------
def write_hdr(path, width, height, pixels):
    """pixels: list of (r,g,b) float, row0 = 天顶"""
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\n")
        f.write(b"FORMAT=32-bit_rle_rgbe\n\n")
        f.write(f"-Y {height} +X {width}\n".encode())
        # 非压缩 RGBE 逐像素
        for (r, g, b) in pixels:
            m = max(r, g, b)
            if m < 1e-32:
                f.write(bytes([0, 0, 0, 0]))
                continue
            e = math.floor(math.log(m, 2)) + 1
            scale = 256.0 / (2.0 ** e)
            f.write(bytes([
                min(255, int(r * scale)),
                min(255, int(g * scale)),
                min(255, int(b * scale)),
                e + 128,
            ]))
    print(f"[assets] wrote {path}")


def gen_sky_hdr(path, w=512, h=256):
    """天空渐变 + 太阳 + 地面"""
    pixels = []
    sun_dir = (0.4, 0.55, 0.6)
    sun_len = math.sqrt(sum(c * c for c in sun_dir))
    sun_dir = tuple(c / sun_len for c in sun_dir)
    for y in range(h):
        v = y / (h - 1)          # 0=天顶 1=地底
        theta = v * math.pi      # 0..pi
        dir_y = math.cos(theta)  # 1=天顶
        for x in range(w):
            u = x / (w - 1)
            phi = u * 2 * math.pi
            dir_x = math.sin(theta) * math.cos(phi)
            dir_z = math.sin(theta) * math.sin(phi)
            # 天空渐变
            if dir_y >= 0:
                sky = (0.25 + 0.55 * (1 - dir_y) ** 1.5,
                       0.42 + 0.45 * (1 - dir_y) ** 1.8,
                       0.85 - 0.25 * (1 - dir_y) ** 1.2)
            else:
                # 地面: 暖棕色
                g = min(1.0, -dir_y * 3.0)
                sky = (0.32 - 0.1 * g, 0.24 - 0.08 * g, 0.18 - 0.06 * g)
            # 太阳
            dot = max(0.0, dir_x * sun_dir[0] + dir_y * sun_dir[1] + dir_z * sun_dir[2])
            sun = dot ** 800 * 30.0 + dot ** 64 * 1.2
            pixels.append((sky[0] + sun, sky[1] + sun * 0.9, sky[2] + sun * 0.7))
    write_hdr(path, w, h, pixels)


# ------------------------------------------------------------------
# 程序化 PBR 贴图
# ------------------------------------------------------------------
def gen_textures(tex_dir):
    os.makedirs(tex_dir, exist_ok=True)
    size = 256

    # albedo: 棋盘格 + 边缘渐变 (金色金属板风格)
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            checker = ((x // 32) + (y // 32)) % 2
            base = (205, 150, 60) if checker else (160, 110, 45)
            shade = 1.0 - 0.25 * (x / size)
            px[x, y] = tuple(int(c * shade) for c in base)
    img.save(os.path.join(tex_dir, "gold_albedo.png"))

    # normal: 规律凹凸圆点
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            nx = math.sin(x / size * math.pi * 8) * 0.35
            ny = math.sin(y / size * math.pi * 8) * 0.35
            nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
            px[x, y] = (int((nx * 0.5 + 0.5) * 255),
                        int((ny * 0.5 + 0.5) * 255),
                        int((nz * 0.5 + 0.5) * 255))
    img.save(os.path.join(tex_dir, "dots_normal.png"))

    # metallic-roughness (glTF 约定 G=roughness B=metallic): 条纹变化
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            metallic = 255 if x < size // 2 else 40
            roughness = int(80 + 140 * (y / size))
            px[x, y] = (0, roughness, metallic)
    img.save(os.path.join(tex_dir, "stripes_mr.png"))

    # ao: 全白
    Image.new("RGB", (8, 8), (255, 255, 255)).save(os.path.join(tex_dir, "white_ao.png"))

    # 第二套: 粗糙塑料 (金属度 0)
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            checker = ((x // 64) + (y // 64)) % 2
            px[x, y] = (190, 60, 60) if checker else (140, 40, 40)
    img.save(os.path.join(tex_dir, "red_albedo.png"))

    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = (0, int(120 + 100 * (x / size)), 0)  # 粗糙度渐变, 金属度 0
    img.save(os.path.join(tex_dir, "plastic_mr.png"))

    print(f"[assets] textures written to {tex_dir}")


# ------------------------------------------------------------------
# UV 球 glTF 生成
# ------------------------------------------------------------------
def gen_uv_sphere(stacks=32, slices=64, radius=1.0):
    positions, normals, uvs, tangents, indices = [], [], [], [], []
    for i in range(stacks + 1):
        v = i / stacks
        theta = v * math.pi  # 0..pi (顶->底)
        for j in range(slices + 1):
            u = j / slices
            phi = u * 2 * math.pi
            x = math.sin(theta) * math.cos(phi)
            y = math.cos(theta)
            z = math.sin(theta) * math.sin(phi)
            positions.append((radius * x, radius * y, radius * z))
            normals.append((x, y, z))
            uvs.append((u, v))
            # 切线: d/dphi
            tangents.append((-math.sin(phi), 0.0, math.cos(phi), 1.0))
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + slices + 1
            indices += [a, b, a + 1, b, b + 1, a + 1]
    return positions, normals, uvs, tangents, indices


def write_gltf_sphere(path, tex_prefix, albedo, normal, mr, ao, base_color, metallic, roughness):
    positions, normals, uvs, tangents, indices = gen_uv_sphere()

    bin_data = bytearray()
    views = []
    accessors = []

    def add_accessor(data_bytes, comp_type, count, acc_type, target):
        offset = len(bin_data)
        bin_data.extend(data_bytes)
        while len(bin_data) % 4:
            bin_data.append(0)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data_bytes), "target": target})
        accessors.append({"bufferView": len(views) - 1, "componentType": comp_type,
                          "count": count, "type": acc_type})

    def pack_floats(values):
        return b"".join(struct.pack("<f", c) for v in values for c in (v if isinstance(v, tuple) else (v,)))

    add_accessor(pack_floats(positions), 5126, len(positions), "VEC3", 34962)
    add_accessor(pack_floats(normals), 5126, len(normals), "VEC3", 34962)
    add_accessor(pack_floats(uvs), 5126, len(uvs), "VEC2", 34962)
    add_accessor(pack_floats(tangents), 5126, len(tangents), "VEC4", 34962)
    add_accessor(b"".join(struct.pack("<I", i) for i in indices), 5125, len(indices), "SCALAR", 34963)

    textures = []
    images = []
    samplers = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]

    def add_tex(filename):
        images.append({"uri": tex_prefix + filename})
        textures.append({"source": len(images) - 1, "sampler": 0})
        return len(textures) - 1

    material = {
        "pbrMetallicRoughness": {
            "baseColorFactor": base_color,
            "metallicFactor": metallic,
            "roughnessFactor": roughness,
        }
    }
    if albedo:
        material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": add_tex(albedo)}
    if mr:
        material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": add_tex(mr)}
    if normal:
        material["normalTexture"] = {"index": add_tex(normal)}
    if ao:
        material["occlusionTexture"] = {"index": add_tex(ao)}

    gltf = {
        "asset": {"version": "2.0", "generator": "genesis-asset-gen"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "sphere"}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3},
                "indices": 4,
                "material": 0,
            }]
        }],
        "materials": [material],
        "buffers": [{"byteLength": len(bin_data), "uri": os.path.basename(path).replace(".gltf", ".bin")}],
        "bufferViews": views,
        "accessors": accessors,
        "textures": textures,
        "images": images,
        "samplers": samplers,
    }

    with open(path, "w") as f:
        json.dump(gltf, f, indent=1)
    with open(path.replace(".gltf", ".bin"), "wb") as f:
        f.write(bin_data)
    print(f"[assets] wrote {path}")


# ------------------------------------------------------------------
# Lua 脚本
# ------------------------------------------------------------------
ORBIT_LUA = """-- 自转 + 公转演示脚本: 验证脚本系统驱动 ECS transform
local angle = 0.0

function on_create()
    log("orbit script attached to: " .. entity.get_name())
end

function on_update(dt)
    angle = angle + dt * 0.8
    -- 绕 Y 轴公转 (半径 2.2), 同时自转
    local x = math.cos(angle) * 2.2
    local z = math.sin(angle) * 2.2
    entity.set_position(x, 0.0, z)
    entity.set_rotation_euler(0.0, math.deg(angle * 2.0), 0.0)
end

function on_destroy()
    log("orbit script destroyed")
end
"""

PULSE_LIGHT_LUA = """-- 呼吸灯脚本: 通过键盘控制亮度方向的示例 (需引擎侧扩展光照绑定, 这里演示 input API)
function on_create()
    log("pulse script attached to: " .. entity.get_name())
end

function on_update(dt)
    local s = 0.6 + 0.4 * math.sin(time.elapsed * 3.0)
    entity.set_scale(s, s, s)
    if input.is_key_down("Space") then
        entity.set_scale(1.5, 1.5, 1.5)
    end
end
"""


def main():
    os.makedirs(os.path.join(ASSETS, "env"), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, "models"), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, "scripts"), exist_ok=True)

    gen_sky_hdr(os.path.join(ASSETS, "env", "sky.hdr"))

    tex_dir = os.path.join(ASSETS, "textures")
    gen_textures(tex_dir)

    # 金球 (带全部 PBR 贴图)
    write_gltf_sphere(os.path.join(ASSETS, "models", "sphere_gold.gltf"),
                      "../textures/", "gold_albedo.png", "dots_normal.png",
                      "stripes_mr.png", "white_ao.png",
                      [1.0, 1.0, 1.0, 1.0], 1.0, 1.0)

    # 红塑料球 (无 metallic)
    write_gltf_sphere(os.path.join(ASSETS, "models", "sphere_plastic.gltf"),
                      "../textures/", "red_albedo.png", None,
                      "plastic_mr.png", None,
                      [1.0, 1.0, 1.0, 1.0], 0.0, 1.0)

    with open(os.path.join(ASSETS, "scripts", "orbit.lua"), "w") as f:
        f.write(ORBIT_LUA)
    with open(os.path.join(ASSETS, "scripts", "pulse.lua"), "w") as f:
        f.write(PULSE_LIGHT_LUA)

    print("[assets] all done")


if __name__ == "__main__":
    main()
