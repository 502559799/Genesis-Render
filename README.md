# Genesis Render 2.0

跨平台（Linux / macOS）实时渲染引擎。从 v1 的「Vulkan + macOS 摄像头滤镜 Demo」
升级为具备 **ECS、多格式模型导入、PBR + IBL、实时 UI、Lua 脚本、OpenGL/Vulkan
双渲染后端** 的完整引擎，能力对齐并在若干维度超越参考引擎 Piccolo。

```
┌──────────────────────────────────────────────────────────────────┐
│ demo / tests        genesis_demo, genesis_tests                  │
├──────────────────────────────────────────────────────────────────┤
│ Engine              帧编排：input → UI → world.update → render   │
├───────────────┬──────────────┬──────────────┬────────────────────┤
│ ECS           │ Renderer     │ UI (ImGui)   │ Script (Lua 5.4)   │
│ World/Entity  │ PBR + IBL    │ 双后端对接    │ 42 个 API + 沙箱   │
│ ComponentPool │ HDR→tonemap  │ present 内合成│ start/tick 钩子    │
│ System 调度   │ 剔除/排序     │              │                    │
├───────────────┴──────────────┴──────────────┴────────────────────┤
│ Resource        glTF/GLB(cgltf) · FBX(自研二进制+zlib) · OBJ      │
│                 stb_image(png/jpg/tga/hdr) · 程序化网格 · PNG/TGA 写│
├──────────────────────────────────────────────────────────────────┤
│ RHI (抽象层)     Device/CommandBuffer/Buffer/Texture/Sampler/     │
│                 Shader/Pipeline/RenderPass/Framebuffer/DescSet    │
│      ┌──────────────────────┬──────────────────────────────┐     │
│      │ OpenGL 4.1 core 后端  │ Vulkan 1.1+ 后端 (VMA)        │     │
│      └──────────────────────┴──────────────────────────────┘     │
├──────────────────────────────────────────────────────────────────┤
│ Platform        SDL2 窗口 / 输入 / 计时器     Foundation: math/log│
└──────────────────────────────────────────────────────────────────┘
```

## 快速开始

```bash
# Linux (Ubuntu 22.04)
sudo apt-get install -y build-essential cmake libsdl2-dev zlib1g-dev
# 可选（Vulkan 后端 + 重新生成着色器）
sudo apt-get install -y libvulkan-dev glslang-tools mesa-vulkan-drivers xvfb

# macOS
xcode-select --install
brew install cmake sdl2          # 可选：LunarG Vulkan SDK

./build.sh                       # Release，双后端，产出 build/bin/{genesis_demo,genesis_tests}
./build.sh --test                # 构建并运行全部测试
./build.sh --demo                # 构建并运行 demo
```

运行 demo：

```bash
./build/bin/genesis_demo --backend gl        # 或 --backend vulkan
./build/bin/genesis_demo --help
```

| 参数 | 说明 |
|------|------|
| `--backend gl\|vulkan` | 渲染后端（默认 gl） |
| `--headless` | 隐藏窗口、不呈现（CI 友好） |
| `--frames N` | 运行 N 帧后退出 |
| `--screenshot out.png` | 离屏渲染并保存截图后退出 |
| `--width/--height` | 窗口尺寸（默认 1280x720） |
| `--validation` | 启用 Vulkan validation layer |
| `--no-ui` / `--no-script` | 关闭 ImGui / Lua |
| `--asset-dir DIR` | 资产根目录 |

Demo 快捷键：`拖拽左键` 环绕相机、`滚轮` 缩放、`ESC` 退出、
`F1` UI、`F2` Cook-Torrance ↔ Blinn-Phong、`F3` 线框、`F4` IBL 开关；
Lua 侧另有 `1..6` 切换着色模型 / 色调映射 / IBL。

## 文档

| 文档 | 内容 |
|------|------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | **统一架构说明**：v1 现状、Piccolo 参考架构、能力对齐差距分析、v2 升级后架构（含架构图 / 类图 / 时序图） |
| [`docs/VERIFICATION.md`](docs/VERIFICATION.md) | **如何验证每一项能力 + 效果图清单与实测数值**（`docs/images/` 共 16 张） |
| [`docs/TEST_REPORT.md`](docs/TEST_REPORT.md) | 编译与功能验证报告、45 个测试用例清单、实测数据、已知问题 |
| [`docs/BUILD.md`](docs/BUILD.md) | 跨平台构建、依赖、着色器生成、headless 验证环境 |
| [`docs/PBR.md`](docs/PBR.md) | PBR 数学：直接光（GGX / Blinn-Phong + Fresnel + Smith）与间接光（irradiance / prefiltered / BRDF LUT）公式与实现对照 |
| [`docs/legacy/`](docs/legacy) | v1 原始文档与 CMakeLists 备份 |

## 目录结构

```
CMakeLists.txt              跨平台构建（后端可选、依赖探测、shader 生成）
build.sh                    一键构建脚本（Linux / macOS）
cmake/GenesisShaders.cmake  着色器生成集成
scripts/build_shaders.py    单源 GLSL → GLSL410 字符串 + SPIR-V 数组（含双方言校验）
scripts/gen_assets.py       程序化资产生成（PNG/HDR/glTF/GLB，纯标准库，可复现）
scripts/gen_fbx_test_asset.py  合法 FBX 二进制测试资产生成器
assets/models               DamagedHelmet.glb, Duck.glb, genesis_*.{gltf,glb,bin,fbx}
assets/textures             checker/normal/AO/emissive/UV 贴图 + sky_equirect.hdr
assets/scripts/demo_anim.lua  Lua 演示脚本
src/external                imgui(+GL/Vulkan/SDL 后端), lua-5.4.4, cgltf, stb_image,
                            vk_mem_alloc, cglm, meshoptimizer, enkiTS, tracy, tlsf, ...
src/genesis/foundation_layer  math(新), log, memory, file, array, service（v1 遗留 + 新增）
src/genesis/platform_layer    window_system, input_system, timer
src/genesis/rhi               rhi.hpp / rhi_enums / rhi_desc / rhi_factory
src/genesis/rhi/opengl        GL 4.1 core 后端
src/genesis/rhi/vulkan        Vulkan 1.1 后端（VMA）
src/genesis/ecs               ecs.hpp（稀疏集 World/ComponentPool/System）, components, systems
src/genesis/resource          resource_types, resource_io(+PNG/TGA 写), gltf_importer,
                              fbx_importer（自研二进制/ASCII 解析 + zlib）, procedural_mesh
src/genesis/renderer          render_ubo, render_scene, render_system(+frame/+ibl),
                              shaders/{common,scene,ibl,post,generated}
src/genesis/ui                ui_system（ImGui 双后端）
src/genesis/script            script_system（Lua 5.4 + 42 个引擎 API + 沙箱）
src/genesis/engine            engine（帧编排 facade）
src/demo1                     v1 原始 macOS 摄像头 Demo（-DGENESIS_LEGACY_DEMO1=ON，仅 macOS）
demo/main.cpp                 v2 演示程序
tests/                        45 个测试用例（CPU + 双后端 GPU + 跨后端比对）
```

## 相对 v1 的能力变化

| 能力 | v1 | v2 |
|------|----|----|
| 渲染后端 | 仅 Vulkan，且抽象层未实现（demo 直接调 vk*） | RHI 抽象 + **OpenGL 4.1 / Vulkan 双后端** |
| 平台 | 仅 macOS（OBJCXX + AVFoundation 硬链接） | Linux + macOS，后端可裁剪 |
| 场景管理 | 无 | **ECS**：稀疏集组件池、generation 句柄、层级、System 调度 |
| 模型导入 | 无 | **glTF / GLB / FBX（二进制+ASCII，自研解析）/ OBJ** |
| 材质光照 | 8 个 2D 滤镜 | **PBR**：直接光（Cook-Torrance GGX 与 Blinn-Phong，含 Fresnel/Smith/metallic）+ 间接光（运行时烘焙 cubemap / irradiance / prefiltered / BRDF LUT） |
| UI | ImGui 固定 4 个面板 | ImGui 双后端 + 输入路由 + 7 个可交互面板 + Lua 控制台 |
| 脚本 | 无 | **Lua 5.4**，单虚拟机 + 每脚本沙箱 + `start/tick` + 42 个 API |
| 数学库 | 无（依赖 cglm） | 自研 header-only：Vec/Mat4/Quat/Transform/AABB/Frustum/Ray |
| 测试 | 无 | **45 个用例 / 4 万+ 断言**，含像素级回读验证与解析解验证 |
| 构建 | 单 CMakeLists，mac 专用 | 跨平台 CMake + `build.sh` + 着色器预生成入库 |

## 许可证

第三方依赖各自遵循其原始许可证（imgui: MIT，Lua 5.4: MIT，cgltf: MIT，
stb_image: MIT/Unlicense，VulkanMemoryAllocator: MIT，SDL2: zlib，zlib: zlib）。
引擎自身代码沿用仓库原有许可。
