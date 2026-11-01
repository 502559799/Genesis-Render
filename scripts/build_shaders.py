#!/usr/bin/env python3
"""
Genesis Render - shader build script.

One GLSL source, two backends.

The engine keeps a single GLSL source per shader stage in
    src/genesis/renderer/shaders/**/*.{vert,frag,comp}

Sources use the dialect macros from
    src/genesis/renderer/shaders/common/genesis_shader.h

so that the same file can express Vulkan-style `layout(set=,binding=)`
qualifiers and plain OpenGL uniforms.  This script:

  1. inlines `#include "..."` directives (recursively),
  2. emits the OpenGL variant  (GLSL 410, `#define GENESIS_GLSL 1`)  as a C string,
  3. emits the Vulkan variant  (GLSL 450, `#define GENESIS_VK 1`)     and compiles it
     to SPIR-V with glslangValidator (or glslc) as a uint32_t array,
  4. writes a registry header + source pair into
        src/genesis/renderer/shaders/generated/
     which are committed to the repository so that the engine can be built on a
     machine that has neither Python nor the Vulkan SDK installed.

Usage:
    python3 scripts/build_shaders.py [--shader-dir DIR] [--out-dir DIR]
                                     [--glslang PATH] [--gl-only] [--check]
"""

import argparse
import os
import tempfile
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
DEFAULT_SHADER_DIR = os.path.join(REPO_ROOT, "src", "genesis", "renderer", "shaders")
DEFAULT_OUT_DIR = os.path.join(DEFAULT_SHADER_DIR, "generated")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')
STAGE_EXTENSIONS = (".vert", ".frag", ".comp", ".geom")


def fail(msg):
    sys.stderr.write("[build_shaders] ERROR: %s\n" % msg)
    sys.exit(1)


def read_file(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def inline_includes(path, shader_dir, seen=None, depth=0):
    """Recursively replace #include "..." with the file content."""
    if seen is None:
        seen = set()
    real = os.path.realpath(path)
    if real in seen:
        return ""  # include guard semantics
    seen.add(real)
    if depth > 16:
        fail("include depth exceeded while processing %s" % path)

    text = read_file(path)
    base_dir = os.path.dirname(path)
    out_lines = []
    for line in text.split("\n"):
        m = INCLUDE_RE.match(line)
        if m:
            inc = m.group(1)
            candidates = [os.path.join(base_dir, inc), os.path.join(shader_dir, inc)]
            target = None
            for c in candidates:
                if os.path.isfile(c):
                    target = c
                    break
            if target is None:
                fail("cannot resolve #include \"%s\" from %s" % (inc, path))
            out_lines.append("// ---- begin %s ----" % inc)
            out_lines.append(inline_includes(target, shader_dir, seen, depth + 1))
            out_lines.append("// ---- end %s ----" % line.strip())
        else:
            out_lines.append(line)
    return "\n".join(out_lines)


EXTRA_DEFINES = os.environ.get("GENESIS_SHADER_DEFINES", "")


def build_variant(source_path, shader_dir, dialect):
    """dialect: 'gl', 'vk' or 'msl' (the latter is GLSL 450 fed to SPIRV-Cross)"""
    body = inline_includes(source_path, shader_dir)
    if dialect == "vk":
        header = "#version 450\n#define GENESIS_VK 1\n"
    elif dialect == "msl":
        header = "#version 450\n#define GENESIS_MSL 1\n"
    elif source_path.endswith(".comp"):
        # Compute shaders need GL 4.3 (SSBO + glDispatchCompute).
        header = "#version 430\n#define GENESIS_GLSL 1\n"
    else:
        header = "#version 410\n#define GENESIS_GLSL 1\n"
    if EXTRA_DEFINES:
        header += "".join("#define %s 1\n" % d for d in EXTRA_DEFINES.split())
    return header + body


def c_string_literal(text):
    """Emit a C string literal, chunked so that no single literal is absurd."""
    chunks = []
    # Escape and split into ~120 char pieces for compiler friendliness.
    escaped = []
    for ch in text:
        if ch == "\\":
            escaped.append("\\\\")
        elif ch == '"':
            escaped.append('\\"')
        elif ch == "\n":
            escaped.append("\\n\"\n    \"")
        elif ch == "\t":
            escaped.append("\\t")
        elif ord(ch) < 32:
            escaped.append("\\x%02x" % ord(ch))
        else:
            escaped.append(ch)
    chunks.append('"' + "".join(escaped) + '"')
    return "\n    ".join(chunks)


def c_byte_array(words):
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join("0x%08xu" % w for w in words[i:i + 8]) + ",")
    return "\n".join(lines)


def find_glslang(explicit):
    if explicit:
        if os.path.isfile(explicit) and os.access(explicit, os.X_OK):
            return explicit, None
        fail("glslang compiler not found at %s" % explicit)
    for name in ("glslangValidator", "glslang", "glslc"):
        for directory in os.environ.get("PATH", "").split(os.pathsep):
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate, ("glslc" if name == "glslc" else None)
    # Common Vulkan SDK locations
    for candidate in (
        os.path.expandvars("$VULKAN_SDK/bin/glslangValidator"),
        "/usr/local/bin/glslangValidator",
        "/usr/bin/glslangValidator",
    ):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate, None
    return None, None


GLSLANG_STAGE = {"vert": "vert", "frag": "frag", "comp": "comp", "geom": "geom"}


def validate_glsl(glslang, source_text, stage):
    """Syntax/semantic check of the OpenGL (4.10) variant."""
    if "glslc" in os.path.basename(glslang):
        return True, ""
    tmp = os.path.join(tempfile.gettempdir(), "genesis_gl_check.%s" % GLSLANG_STAGE.get(stage, "vert"))
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(source_text)
    try:
        # Plain invocation: compile + link check only, no SPIR-V output.
        # (Using -G/--target-env opengl would additionally demand explicit
        # layout(binding=) qualifiers, which the GL dialect intentionally omits.)
        proc = subprocess.run([glslang, tmp], capture_output=True, text=True)
        return proc.returncode == 0, (proc.stdout + proc.stderr)
    finally:
        if os.path.isfile(tmp):
            os.remove(tmp)
        for junk in (tmp + ".spv", "glslangValidator.out.spv"):
            if os.path.isfile(junk):
                os.remove(junk)


def compile_spirv(glslang, mode, source_text, out_spv, stage):
    # glslangValidator infers the stage from the file extension, so the temp
    # file must carry the real stage suffix (.vert/.frag/.comp).
    suffix = GLSLANG_STAGE.get(stage, "vert")
    tmp = "%s.%s" % (out_spv, suffix)
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(source_text)
    try:
        if mode == "glslc":
            cmd = [glslang, "-mfmt=num", "-o", out_spv, tmp]
        else:
            cmd = [glslang, "-V", "--target-env", "vulkan1.1", "-S", suffix, "-o", out_spv, tmp]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            fail("SPIR-V compilation failed for %s" % out_spv)
        if mode == "glslc" and os.path.isfile(out_spv + ".num"):
            os.replace(out_spv + ".num", out_spv)
    finally:
        if os.path.isfile(tmp):
            os.remove(tmp)


def find_spirv_cross(explicit):
    if explicit:
        if os.path.isfile(explicit) and os.access(explicit, os.X_OK):
            return explicit
        fail("spirv-cross not found at %s" % explicit)
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(directory, "spirv-cross")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    for candidate in (os.path.expandvars("$VULKAN_SDK/bin/spirv-cross"), "/usr/local/bin/spirv-cross"):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def compile_msl(glslang, mode, spirv_cross, source_text, stage, scratch_base):
    """GLSL (MSL dialect) -> SPIR-V -> Metal Shading Language 2.1.

    --msl-decoration-binding turns the flattened `binding = set*8+binding`
    decorations into the [[buffer]]/[[texture]]/[[sampler]] slots and
    --flip-vert-y makes Metal rasterise NDC y=-1 into row 0, which is the
    orientation the OpenGL and Vulkan backends use natively.
    """
    spv = scratch_base + ".msl.spv"
    compile_spirv(glslang, mode, source_text, spv, stage)
    out = scratch_base + ".metal"
    cmd = [spirv_cross, spv, "--msl", "--msl-version", "20100", "--msl-decoration-binding", "--output", out]
    if stage == "vert":
        cmd.insert(3, "--flip-vert-y")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if os.path.isfile(spv):
        os.remove(spv)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        fail("SPIRV-Cross MSL translation failed for %s" % scratch_base)
    text = read_file(out)
    return text, out


def spv_words(path):
    import struct
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % 4 != 0:
        fail("SPIR-V file %s is not a multiple of 4 bytes" % path)
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def identifier_for(rel_path):
    name = rel_path.replace(os.sep, "_").replace("/", "_")
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if name and name[0].isdigit():
        name = "_" + name
    return name


def main():
    parser = argparse.ArgumentParser(description="Genesis shader build script")
    parser.add_argument("--shader-dir", default=DEFAULT_SHADER_DIR)
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    parser.add_argument("--glslang", default=None, help="path to glslangValidator / glslc")
    parser.add_argument("--spirv-cross", default=None, help="path to spirv-cross (MSL generation)")
    parser.add_argument("--no-msl", action="store_true", help="skip Metal Shading Language generation")
    parser.add_argument("--gl-only", action="store_true", help="skip SPIR-V generation")
    parser.add_argument("--no-validate", action="store_true", help="skip GLSL 410 validation")
    parser.add_argument("--check", action="store_true", help="verify output is up to date, do not write")
    args = parser.parse_args()

    shader_dir = os.path.abspath(args.shader_dir)
    out_dir = os.path.abspath(args.out_dir)
    if not os.path.isdir(shader_dir):
        fail("shader dir not found: %s" % shader_dir)

    sources = []
    for root, dirs, files in os.walk(shader_dir):
        dirs[:] = [d for d in dirs if d not in ("generated",)]
        for f in sorted(files):
            if f.endswith(STAGE_EXTENSIONS):
                full = os.path.join(root, f)
                rel = os.path.relpath(full, shader_dir)
                sources.append((rel, full))
    sources.sort()

    glslang, glslc_mode = (None, None)
    if not args.gl_only:
        glslang, glslc_mode = find_glslang(args.glslang)
        if not glslang:
            sys.stderr.write("[build_shaders] WARNING: no glslangValidator/glslc found - generating GLSL only\n")
    spirv_cross = None
    if glslang and not args.no_msl:
        spirv_cross = find_spirv_cross(args.spirv_cross)
        if not spirv_cross:
            sys.stderr.write("[build_shaders] WARNING: no spirv-cross found - the Metal variant is skipped\n")

    os.makedirs(out_dir, exist_ok=True)

    header_lines = []
    source_lines = []
    registry_lines = []
    count_vk = 0
    count_gl = 0
    count_msl = 0

    header_lines.append("// Generated by scripts/build_shaders.py - DO NOT EDIT")
    header_lines.append("#ifndef __GENESIS_SHADERS_GENERATED_H__")
    header_lines.append("#define __GENESIS_SHADERS_GENERATED_H__")
    header_lines.append("")
    header_lines.append("#include <cstddef>")
    header_lines.append("#include <cstdint>")
    header_lines.append("")
    header_lines.append("namespace Genesis {")
    header_lines.append("namespace Shaders {")
    header_lines.append("")
    header_lines.append("struct ShaderBinary")
    header_lines.append("{")
    header_lines.append("    const char *name;          // e.g. \"pbr/mesh.vert\"")
    header_lines.append("    const char *glsl;          // OpenGL variant source (NUL terminated)")
    header_lines.append("    size_t glsl_size;          // bytes, excluding NUL")
    header_lines.append("    const uint32_t *spirv;     // Vulkan variant, nullptr when unavailable")
    header_lines.append("    size_t spirv_words;        // number of 32-bit words")
    header_lines.append("    const char *msl;           // Metal variant (MSL 2.1, entry main0), nullptr when unavailable")
    header_lines.append("    size_t msl_size;           // bytes, excluding NUL")
    header_lines.append("};")
    header_lines.append("")
    header_lines.append("// Number of shaders baked into the binary.")
    header_lines.append("size_t shaderCount();")
    header_lines.append("// Lookup by relative source path, e.g. \"pbr/mesh.vert\". Returns nullptr when missing.")
    header_lines.append("const ShaderBinary *findShader(const char *name);")
    header_lines.append("")
    header_lines.append("} // namespace Shaders")
    header_lines.append("} // namespace Genesis")
    header_lines.append("")
    header_lines.append("#endif // __GENESIS_SHADERS_GENERATED_H__")

    source_lines.append("// Generated by scripts/build_shaders.py - DO NOT EDIT")
    source_lines.append('#include "genesis_shaders_generated.h"')
    source_lines.append("")
    source_lines.append("#include <cstring>")
    source_lines.append("")
    source_lines.append("namespace Genesis {")
    source_lines.append("namespace Shaders {")
    source_lines.append("namespace {")

    for rel, full in sources:
        ident = identifier_for(rel)
        stage = rel.rsplit(".", 1)[-1]

        gl_text = build_variant(full, shader_dir, "gl")
        gl_ident = "%s_glsl" % ident
        if glslang and not args.no_validate:
            ok, log = validate_glsl(glslang, gl_text, stage)
            if not ok:
                sys.stderr.write("[build_shaders] OpenGL(4.10) variant of %s failed:\n%s\n" % (rel, log))
                fail("GLSL 410 validation failed for %s" % rel)
        source_lines.append("// ---- %s (OpenGL 4.10 variant) ----" % rel)
        source_lines.append("static const char %s[] =" % gl_ident)
        source_lines.append("    " + c_string_literal(gl_text) + ";")
        source_lines.append("static const size_t %s_size = %d;" % (gl_ident, len(gl_text.encode("utf-8"))))
        source_lines.append("")
        count_gl += 1

        vk_ident = "%s_spirv" % ident
        vk_size_ident = "%s_spirv_words" % ident
        if glslang:
            spv_path = os.path.join(out_dir, ident + ".spv")
            vk_text = build_variant(full, shader_dir, "vk")
            compile_spirv(glslang, glslc_mode, vk_text, spv_path, stage)
            words = spv_words(spv_path)
            source_lines.append("// ---- %s (SPIR-V, Vulkan 1.1) ----" % rel)
            source_lines.append("static const uint32_t %s[] = {" % vk_ident)
            source_lines.append(c_byte_array(words))
            source_lines.append("};")
            source_lines.append("static const size_t %s = %d;" % (vk_size_ident, len(words)))
            source_lines.append("")
            count_vk += 1
            msl_ref = "nullptr, 0"
            if spirv_cross:
                msl_text, _ = compile_msl(glslang, glslc_mode, spirv_cross, build_variant(full, shader_dir, "msl"),
                                          stage, os.path.join(out_dir, ident))
                msl_ident = "%s_msl" % ident
                source_lines.append("// ---- %s (Metal Shading Language 2.1, via SPIRV-Cross) ----" % rel)
                source_lines.append("static const char %s[] =" % msl_ident)
                source_lines.append("    " + c_string_literal(msl_text) + ";")
                source_lines.append("static const size_t %s_size = %d;" % (msl_ident, len(msl_text.encode("utf-8"))))
                source_lines.append("")
                msl_ref = "%s, %s_size" % (msl_ident, msl_ident)
                count_msl += 1
            registry_lines.append('    ShaderBinary{"%s", %s, %s_size, %s, %s, %s},'
                                  % (rel.replace(os.sep, "/"), gl_ident, gl_ident, vk_ident, vk_size_ident, msl_ref))
        else:
            registry_lines.append('    ShaderBinary{"%s", %s, %s_size, nullptr, 0, nullptr, 0},'
                                  % (rel.replace(os.sep, "/"), gl_ident, gl_ident))

    source_lines.append("static const ShaderBinary k_shaders[] = {")
    if registry_lines:
        source_lines.append("\n".join(registry_lines))
    source_lines.append("};")
    source_lines.append("")
    source_lines.append("} // namespace")
    source_lines.append("")
    source_lines.append("size_t shaderCount()")
    source_lines.append("{")
    source_lines.append("    return sizeof(k_shaders) / sizeof(k_shaders[0]);")
    source_lines.append("}")
    source_lines.append("")
    source_lines.append("const ShaderBinary *findShader(const char *name)")
    source_lines.append("{")
    source_lines.append("    if (!name) return nullptr;")
    source_lines.append("    for (const ShaderBinary &s : k_shaders)")
    source_lines.append("        if (std::strcmp(s.name, name) == 0) return &s;")
    source_lines.append("    return nullptr;")
    source_lines.append("}")
    source_lines.append("")
    source_lines.append("} // namespace Shaders")
    source_lines.append("} // namespace Genesis")

    header_text = "\n".join(header_lines) + "\n"
    source_text = "\n".join(source_lines) + "\n"

    header_path = os.path.join(out_dir, "genesis_shaders_generated.h")
    source_path = os.path.join(out_dir, "genesis_shaders_generated.cpp")

    if args.check:
        ok = True
        for path, text in ((header_path, header_text), (source_path, source_text)):
            if not os.path.isfile(path) or read_file(path) != text:
                sys.stderr.write("[build_shaders] out of date: %s\n" % path)
                ok = False
        sys.exit(0 if ok else 1)

    # Drop per-shader artefacts (.spv / .metal) of shaders that no longer exist.
    expected = set()
    for rel, _ in sources:
        ident = identifier_for(rel)
        expected.update({ident + ".spv", ident + ".metal"})
    for f in os.listdir(out_dir):
        if f.endswith((".spv", ".metal")) and f not in expected:
            os.remove(os.path.join(out_dir, f))

    with open(header_path, "w", encoding="utf-8") as f:
        f.write(header_text)
    with open(source_path, "w", encoding="utf-8") as f:
        f.write(source_text)

    print("[build_shaders] %d shaders -> GLSL:%d SPIR-V:%d MSL:%d" % (len(sources), count_gl, count_vk, count_msl))
    print("[build_shaders] wrote %s" % header_path)
    print("[build_shaders] wrote %s" % source_path)


if __name__ == "__main__":
    main()
