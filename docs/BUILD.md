# 构建指南（Linux / macOS）

## 1. 依赖

| 依赖 | 必需 | Linux (Ubuntu 22.04) | macOS |
|------|------|----------------------|-------|
| C++17 编译器 | ✅ | `build-essential` (gcc ≥ 9) | Xcode CLT (clang) |
| CMake ≥ 3.16 | ✅ | `cmake` | `brew install cmake` |
| SDL2 | ✅ | `libsdl2-dev` | `brew install sdl2` |
| zlib | ✅ | `zlib1g-dev` | 系统自带 |
| Vulkan SDK | 可选（Vulkan 后端） | `libvulkan-dev` | LunarG Vulkan SDK |
| glslangValidator | 可选（重新生成着色器） | `glslang-tools` | Vulkan SDK 自带 |
| 软件光栅器（headless 验证） | 可选 | `mesa-vulkan-drivers`(lavapipe) + `xvfb` + Mesa llvmpipe | — |

已随仓库内置、无需安装：imgui 1.86（含 SDL/OpenGL3/Vulkan 后端）、Lua 5.4.4、
cgltf、stb_image、VulkanMemoryAllocator、tlsf、cglm、meshoptimizer、enkiTS、tracy。

缺失 Vulkan 时 CMake 会打印 `Vulkan SDK not found - disabling the Vulkan backend`
并自动退化为仅 OpenGL 构建，不会失败。

## 2. 构建

```bash
./build.sh                 # Release，双后端，demo + tests
./build.sh --debug         # Debug（build-debug/）
./build.sh --gl-only       # 仅 OpenGL
./build.sh --vk-only       # 仅 Vulkan
./build.sh --shaders       # 先重新生成着色器（需 python3 + glslangValidator）
./build.sh --test          # 构建后运行测试
./build.sh --demo          # 构建后运行 demo
./build.sh --clean -j8     # 清理重建，指定并行度
```

等价的手动命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DGENESIS_BACKEND_OPENGL=ON -DGENESIS_BACKEND_VULKAN=ON
cmake --build build -j
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `GENESIS_BACKEND_OPENGL` | ON | 编译 `src/genesis/rhi/opengl` |
| `GENESIS_BACKEND_VULKAN` | ON | 编译 `src/genesis/rhi/vulkan`（找不到 Vulkan 时自动关闭） |
| `GENESIS_BUILD_DEMO` | ON | `demo/*.cpp` → `bin/genesis_demo` |
| `GENESIS_BUILD_TESTS` | ON | `tests/*.cpp` → `bin/genesis_tests` |
| `GENESIS_REBUILD_SHADERS` | AUTO | `AUTO`=有 python3+glslang 时在 configure 阶段重新生成；`ON`=强制（缺失则报错）；`OFF`=只用入库产物 |
| `GENESIS_LEGACY_DEMO1` | OFF | v1 的 macOS 摄像头 Demo（需要 AVFoundation，仅 APPLE） |
| `GENESIS_VULKAN_VALIDATION` | OFF | 传给 demo/tests 的默认 validation 开关 |

产物：`build/lib/libGenesisEngine.a`、`build/bin/genesis_demo`、`build/bin/genesis_tests`。

## 3. 着色器构建

单一 GLSL 源文件（`src/genesis/renderer/shaders/**`）→ 两个变体：

```
scripts/build_shaders.py
  ├─ 内联 #include（common/genesis_shader.h, common/ubo_layout.h, common/pbr.h）
  ├─ GL 变体：#version 410 + #define GENESIS_GLSL 1  → glslangValidator 语法校验
  │            → 嵌入为 C 字符串
  └─ VK 变体：#version 450 + #define GENESIS_VK 1     → glslangValidator -V
               --target-env vulkan1.1 → SPIR-V → 嵌入为 uint32_t 数组
产物：src/genesis/renderer/shaders/generated/genesis_shaders_generated.{h,cpp}（入库）
```

产物入库的意义：**没有 python3 / 没有 Vulkan SDK 的机器也能直接编译引擎**，
运行时零文件 IO（着色器全部在二进制里）。运行期通过
`Genesis::Shaders::findShader("scene/pbr_mesh.frag")` 取得两个变体，
RHI 按后端选择 `glsl` 或 `spirv`。

手动重新生成：

```bash
python3 scripts/build_shaders.py                 # 双方言
python3 scripts/build_shaders.py --gl-only       # 仅 GL（无 glslang 时）
python3 scripts/build_shaders.py --check         # 校验产物是否过期（CI 用）
```

## 4. 资产生成

```bash
python3 scripts/gen_assets.py            # 生成 assets/textures + assets/models
python3 scripts/gen_assets.py --verify   # 生成后回读校验（PNG/HDR/glTF/GLB）
python3 scripts/gen_fbx_test_asset.py    # 生成 FBX 二进制测试资产
```

`gen_assets.py` 只用 Python 标准库（zlib/struct/math），自带 PNG / TGA /
Radiance HDR(RGBE) / glTF / GLB 编码器，所有随机性使用固定 seed，
重复运行字节级一致，可用作回归基线。

## 5. Headless 验证环境（CI）

容器/CI 里没有显卡时，用软件光栅器 + 虚拟显示即可跑完整的像素级验证：

```bash
sudo apt-get install -y mesa-vulkan-drivers xvfb
Xvfb :99 -screen 0 1920x1080x24 &
export DISPLAY=:99
export XDG_RUNTIME_DIR=/tmp/xdg && mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json   # lavapipe

./build/bin/genesis_tests --backend gl        # Mesa llvmpipe, OpenGL 4.5 Core
./build/bin/genesis_tests --backend vulkan    # lavapipe, Vulkan 1.3
./build/bin/genesis_demo --backend gl --headless --screenshot shot.png
```

macOS 上无需 Xvfb；OpenGL 走系统 4.1 Core，Vulkan 需安装 MoltenVK/Vulkan SDK。
`GL 4.1` 是 macOS 的上限，这也是整个引擎（含 imgui 的 `#version 410`、
IBL 用 raster pass 而非 compute）坚持 4.1 特性的原因。
