#!/usr/bin/env python3
"""
Genesis 着色器构建脚本:
  1. 递归展开 #include, 生成 OpenGL 用的扁平 GLSL (<out>/<name>.<stage>.glsl)
  2. 注入 #version 450 + GENESIS_VULKAN 宏, 调 glslangValidator 生成 SPIR-V (<out>/<name>.<stage>.spv)
用法: build_shader.py <shader_src> <out_dir> <glslangValidator>
"""
import os
import re
import subprocess
import sys


def flatten(path: str, seen: set) -> str:
    """递归展开 #include "xxx" (相对当前文件目录)"""
    out_lines = []
    base = os.path.dirname(path)
    with open(path, "r") as f:
        for line in f:
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                inc = os.path.normpath(os.path.join(base, m.group(1)))
                if inc in seen:
                    continue  # include guard 已由文件自身处理, 这里防重复展开
                seen.add(inc)
                out_lines.append(f'// begin include: {m.group(1)}\n')
                out_lines.append(flatten(inc, seen))
                out_lines.append(f'// end include: {m.group(1)}\n')
            elif re.match(r"\s*#version", line):
                continue  # #version 由目标后端统一注入
            else:
                out_lines.append(line)
    return "".join(out_lines)


def main():
    if len(sys.argv) < 4:
        print("usage: build_shader.py <shader_src> <out_dir> <glslangValidator>")
        return 1

    src = sys.argv[1]
    out_dir = sys.argv[2]
    glslang = sys.argv[3]

    name = os.path.basename(src)  # e.g. pbr.vert
    os.makedirs(out_dir, exist_ok=True)

    flattened = flatten(src, set())

    # 去掉 common.glsl 的 include guard 宏 (展开后 #ifndef 仍保留无妨)
    glsl_path = os.path.join(out_dir, name + ".glsl")
    with open(glsl_path, "w") as f:
        f.write(flattened)

    # SPIR-V: 注入版本与宏
    stage = os.path.splitext(name)[1]  # .vert / .frag
    tmp_path = os.path.join(out_dir, name + ".vk.tmp" + stage)
    with open(tmp_path, "w") as f:
        f.write("#version 450\n#define GENESIS_VULKAN 1\n")
        f.write(flattened)

    spv_path = os.path.join(out_dir, name + ".spv")
    result = subprocess.run([glslang, "-V", tmp_path, "-o", spv_path],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[shader] glslangValidator failed for {name}:")
        print(result.stdout)
        print(result.stderr)
        return 1

    os.remove(tmp_path)
    print(f"[shader] built {name} -> .glsl + .spv")
    return 0


if __name__ == "__main__":
    sys.exit(main())
