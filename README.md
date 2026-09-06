# Genesis Render Engine v2.0

跨平台（Linux / macOS）、多后端（**Vulkan + OpenGL**）渲染引擎。由 v1.0 的 Vulkan 滤镜 demo 升级而来，能力对齐 Piccolo 引擎：**数据导向 ECS、assimp 多格式模型导入（fbx/gltf/glb/obj）、Lua 脚本系统、ImGui 实时交互界面、PBR 渲染（直接光 BlinnPhong/Cook-Torrance 双模型 + 间接光 IBL 运行时烘焙）**。

详细架构说明见 [docs/统一架构设计文档.md](docs/统一架构设计文档.md)（含 Genesis 原架构、Piccolo 架构、v2.0 新架构三部分的架构图/类图/时序图）。

## 目录结构

```
src/genesis/
  foundation_layer/    基础设施: Memory(TLSF)/Log/File/Array/Service
  application_layer/   Window (SDL2, Vulkan/OpenGL 双模式) + Input
  rhi_layer/           渲染硬件接口 (RHIDevice 纯虚接口)
    vulkan/            VulkanDevice (VMA, 资源池, 管线/描述符/离屏目标)
    opengl/            OpenGLDevice (core profile, 自研 GL loader, VAO/FBO 缓存)
  ecs_layer/           ECS: World + ComponentPool(稀疏集) + System + 内置组件
  resource_layer/      AssetManager: assimp 模型加载, stb 纹理加载(含 HDR)
  render_layer/        RenderSystem(前向 PBR) + IBLBaker + ShaderLibrary
  script_layer/        ScriptSystem: Lua5.4 + sol2, per-entity environment
  ui_layer/            UIService: ImGui 双后端实时界面
src/demo2/             demo2_pbr: 能力验证 demo (PBR 多后端模型查看器)
src/demo1/             旧版 macOS 相机滤镜 demo (仅 APPLE 构建)
assets/                程序化生成的演示资产 (models/textures/env/scripts)
tools/                 build_shader.py / gen_assets.py / verify.sh / compare_screenshots.py
```

## 构建

```bash
# Linux (依赖: libsdl2-dev libvulkan-dev libassimp-dev glslang-tools)
bash build_linux.sh

# macOS (依赖: sdl2, assimp, glslang via brew; Vulkan 经由 MoltenVK)
bash build_macos.sh
```

## 运行 demo

```bash
# Vulkan 后端
./build-v2/src/demo2/demo2_pbr --backend vulkan --assets ./assets --shaders ./build-v2/shaders

# OpenGL 后端
./build-v2/src/demo2/demo2_pbr --backend opengl --assets ./assets --shaders ./build-v2/shaders
```

参数：`--backend vulkan|opengl` `--lighting pbr|blinnphong` `--no-ibl` `--no-skybox`
`--headless`（无头渲染） `--frames N`（N 帧后退出） `--screenshot out.png` `--selftest`（逻辑自检） `--debug-channel N`

窗口内操作：鼠标右键拖拽旋转相机，滚轮缩放；ImGui 面板可切换直接光模型、IBL/天空盒、曝光、灯光方向/强度、材质 metallic/roughness/baseColor、脚本热重载。

## 验证

```bash
bash tools/verify.sh   # 编译 -> 26 项逻辑自检 -> 双后端截图 -> 图像指标对比
```

无头环境依赖 `xvfb`（X 虚拟帧缓冲）与软件渲染 ICD（mesa-vulkan-drivers / llvmpipe）。

## 重新生成资产

```bash
python3 tools/gen_assets.py
assimp export assets/models/sphere_gold.gltf assets/models/sphere_gold.fbx
```
