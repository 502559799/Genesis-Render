# 构建指南（macOS / Linux）

## 1. 依赖

| 依赖 | 必需 | macOS | Linux (Ubuntu 22.04) |
|---|---|---|---|
| C++17 编译器 | ✅ | Xcode CLT（AppleClang；Metal 后端以 Objective-C++ + ARC 编译） | `build-essential`（gcc ≥ 9） |
| CMake ≥ 3.16 | ✅ | `brew install cmake` | `cmake` |
| SDL2 | ✅ | `brew install sdl2` | `libsdl2-dev` |
| zlib | ✅ | 系统自带 | `zlib1g-dev` |
| Metal | macOS 自带 | Metal.framework / QuartzCore | — |
| Vulkan | 可选 | LunarG Vulkan SDK（MoltenVK + validation layers） | `libvulkan-dev` + 驱动 |
| glslangValidator + spirv-cross | 仅重新生成着色器 | Vulkan SDK 或 `brew install glslang spirv-cross` | `glslang-tools spirv-cross` |

内置、无需安装：imgui 1.86、Lua 5.4.4、cgltf、stb、VulkanMemoryAllocator 等（`src/external/`）。
找不到 Vulkan 时 CMake 自动关闭 Vulkan 后端；非 Apple 平台自动关闭 Metal 后端。

## 2. 构建

```bash
./build.sh                       # Release → build/bin/{genesis_demo,genesis_tests}
./build.sh --debug               # Debug（build-debug/）
./build.sh --no-gl | --no-vulkan | --no-metal
./build.sh --shaders             # 先重新生成 GLSL410 / SPIR-V / MSL
./build.sh --test                # 构建后运行全部测试（三后端 + 跨后端比对）
./build.sh --test --validation   # 打开 Vulkan layer / GL 错误检查 / Metal API + shader 校验
./build.sh --demo [gl|vulkan|metal]
./build.sh --clean -j8
```

等价的手动命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGENESIS_REBUILD_SHADERS=OFF
cmake --build build -j
```

### CMake 选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `GENESIS_BACKEND_OPENGL` | ON | `src/genesis/rhi/opengl` |
| `GENESIS_BACKEND_VULKAN` | ON | `src/genesis/rhi/vulkan`（找不到 Vulkan 时自动关闭） |
| `GENESIS_BACKEND_METAL` | Apple 上 ON | `src/genesis/rhi/metal/metal_backend.mm`，链接 Metal / QuartzCore / Cocoa |
| `GENESIS_BUILD_DEMO` / `GENESIS_BUILD_TESTS` | ON | `demo/*.cpp`、`tests/*.cpp` |
| `GENESIS_REBUILD_SHADERS` | AUTO | AUTO：工具齐全时 configure 阶段重新生成；ON：强制；OFF：只用入库产物 |
| `GENESIS_VULKAN_VALIDATION` | OFF | demo/tests 默认打开 validation |
| `GENESIS_LEGACY_DEMO1` | OFF | v1 macOS 摄像头 Demo |

## 3. 着色器

```
src/genesis/renderer/shaders/**         单源 GLSL（common/genesis_shader.h 方言宏）
scripts/build_shaders.py
  ├─ GL   : #version 410 → glslang 语法校验 → 嵌入字符串
  ├─ VK   : #version 450 → glslang -V (vulkan1.1) → .spv → 嵌入 uint32 数组
  └─ Metal: SPIR-V → spirv-cross --msl --msl-version 20100 --msl-decoration-binding
            （顶点着色器加 --flip-vert-y）→ .metal → 嵌入字符串
产物：shaders/generated/{*.spv,*.metal,genesis_shaders_generated.{h,cpp}}（入库）
```

运行时 `Shaders::findShader("scene/pbr_mesh.frag")` 返回三个变体，RHI 按后端取 GLSL / SPIR-V / MSL（Metal 在创建时编译 MSL 源码，入口 `main0`）。

```bash
python3 scripts/build_shaders.py                          # 自动查找 glslang / spirv-cross
python3 scripts/build_shaders.py --glslang /path/glslangValidator --spirv-cross /path/spirv-cross
python3 scripts/build_shaders.py --check                  # 校验产物是否过期（CI）
```

## 4. 校验环境

| 后端 | 开关 |
|---|---|
| 全部 | `GENESIS_VALIDATION=1`（或 demo `--validation`） |
| Vulkan | 需要 `VK_LAYER_KHRONOS_validation`；macOS 上 SDK 层不在默认路径：`export VK_ADD_LAYER_PATH=$HOME/VulkanSDK/<ver>/macOS/share/vulkan/explicit_layer.d`（`build.sh --validation` 自动设置）。启用时日志打印 `[VK] validation layer VK_LAYER_KHRONOS_validation enabled` |
| OpenGL | `GENESIS_GL_VALIDATE=1`：每条命令后 `glGetError` + `glValidateProgram` |
| Metal | `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`。**只对 Metal 进程设置**：macOS 的 OpenGL 驱动构建在 Metal 之上，打开 shader validation 会使其回退软件渲染，所以 `build.sh` 按后端分别运行测试 |

## 5. 资产

```bash
python3 scripts/gen_assets.py            # assets/textures + assets/models（标准库实现，固定 seed，可复现）
python3 scripts/gen_fbx_test_asset.py    # FBX 二进制测试资产
```

## 6. Linux headless（CI）

```bash
sudo apt-get install -y mesa-vulkan-drivers xvfb
Xvfb :99 -screen 0 1920x1080x24 & export DISPLAY=:99
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
./build/bin/genesis_tests                 # GL(llvmpipe) + Vulkan(lavapipe)
```
