# Genesis Render

ECS + PBR 实时渲染引擎（Piccolo 能力对齐版），同一套内核同时支持
**Vulkan 1.0** 与 **OpenGL 4.1+** 双后端，支持 glTF/FBX 模型导入、
Cook-Torrance/Blinn-Phong 双直接光模型、运行时 IBL（辐照度 / 预滤波 /
BRDF LUT）、ImGui 实时编辑界面与 Lua 实体脚本。

详细架构见 **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**。

## 依赖

- CMake ≥ 3.16，C++17
- SDL2（`libsdl2-dev`）、Vulkan SDK/loader（`libvulkan1`）
- 可选：Assimp（`libassimp-dev`，FBX 支持；缺失时自动禁用）
- 构建期：`glslangValidator`（已随 glslang 包提供）
- 无头验证：Mesa llvmpipe（GL 与 lavapipe Vulkan）

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行

```bash
build/GenesisDemo --opengl                 # 交互窗口（OpenGL）
build/GenesisDemo --vulkan                 # 交互窗口（Vulkan）

# 无头渲染 + 截图（CI 友好，不需要 X/Wayland）
build/GenesisDemo --opengl --headless --frames 40 --screenshot scene.png
build/GenesisDemo --vulkan --headless --frames 40 --screenshot scene.png

# 场景/模型
build/GenesisDemo --opengl --headless --scenario noibl   # basic|nodirect|noibl|blinn|dark
build/GenesisDemo --opengl --headless --model assets/models/DamagedHelmet.fbx

# 自动化测试（场景矩阵、模型导入、Lua、GL/VK 一致性）
bash scripts/run_headless_tests.sh
```

CLI：

| 参数 | 说明 |
|---|---|
| `--opengl` / `--vulkan` | RHI 后端 |
| `--headless` | 离屏上下文（EGL pbuffer / Vulkan 无 surface） |
| `--width/--height/--frames` | 分辨率 / 帧数 |
| `--screenshot path.png` | 最后一帧输出 PNG |
| `--scenario name` | 光照场景（见上） |
| `--model path` | 自定义模型（.glb/.gltf/.fbx/.obj…） |
| `--no-env` | 不生成环境贴图 |
| `--script path` | 自定义 Lua |

## 内容

- `src/genesis/rhi` —— 后端中立 RHI（Vulkan/OpenGL 实现）
- `src/genesis/render` —— PBR forward、HDR/post、**CPU IBL 烘焙**
- `src/genesis/resource` —— cgltf(glTF) + Assimp(FBX) + stb 贴图
- `src/genesis/ecs` —— 稀疏组件 Registry、场景/层级
- `src/genesis/script` —— Lua 5.4 实体脚本
- `src/genesis/ui` —— ImGui 层级/检查器/资产/渲染面板
- `assets/shaders` —— 单源 GLSL（Vulkan 走构建期 SPIR-V）
- `scripts/run_headless_tests.sh` —— 无头回归

## 说明

- OpenGL 在 llvmpipe 上结果确定性；lavapipe（软件 Vulkan）在多采样器
  高迭代着色器上有驱动级段错误，IBL 因此在 CPU 卷积后上传。
  真机 Vulkan 驱动不受影响；测试脚本对 VK 做有限重试，严格判定走 GL。
- 原始相机滤镜 Demo 保留在 `src/demo1`（不参与当前构建）。
