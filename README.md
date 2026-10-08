# Genesis Render 2.0

跨平台（Linux / macOS）实时渲染引擎。从 v1 的「Vulkan + macOS 摄像头滤镜 Demo」
升级为具备 **ECS、多格式模型导入、PBR + IBL、实时 UI、Lua 脚本、OpenGL/Vulkan
双渲染后端** 的完整引擎，能力对齐并在若干维度超越参考引擎 Piccolo。

v2.1 新增 **SDF 文字渲染（2D + 3D）、可编程 CommandBuffer 自由绘制命令、
可编程后处理管线、脚本侧完整 RTTI 接口（纯脚本搭建场景）、材质实时编辑**，
并修复了 macOS 上 Vulkan 设备特性请求导致无法启动窗口的问题。

v2.2 新增 **Metal 渲染后端**（Objective-C++ + Metal Shading Language，
macOS 专用），引擎至此严格同时支持 **OpenGL / Vulkan / Metal** 三端，
与原 OpenGL/Vulkan 共用同一套 RHI 抽象接口。

```
┌──────────────────────────────────────────────────────────────────┐
│ demo / tests        genesis_demo, genesis_tests                  │
├──────────────────────────────────────────────────────────────────┤
│ Engine              帧编排：input → UI → world.update → render   │
├───────────────┬──────────────┬──────────────┬────────────────────┤
│ ECS           │ Renderer     │ UI (ImGui)   │ Script (Lua 5.4)   │
│ World/Entity  │ PBR + IBL    │ 双后端对接    │ 60+ 个 API + 沙箱  │
│ ComponentPool │ HDR→tonemap  │ present 内合成│ start/tick 钩子    │
│ System 调度   │ 剔除/排序     │              │ RTTI/材质/命令     │
├───────────────┼──────────────┼──────────────┼────────────────────┤
│ Overlay       │ CommandBuffer: 自由 2D 填充/线、3D 四边形绘制      │
│ (新)          │ 可编程后处理: invert/scanline/vignette 用户管线    │
│ Text (SDF)    │ stb_truetype 真 SDF 图集 · 2D 屏幕 + 3D 世界文字  │
├───────────────┴──────────────┴──────────────┴────────────────────┤
│ Resource        glTF/GLB(cgltf) · FBX(自研二进制+zlib) · OBJ      │
│                 stb_image(png/jpg/tga/hdr) · 程序化网格 · PNG/TGA 写│
├──────────────────────────────────────────────────────────────────┤
│ RHI (抽象层)     Device/CommandBuffer/Buffer/Texture/Sampler/     │
│                 Shader/Pipeline/RenderPass/Framebuffer/DescSet    │
│   ┌────────────────┬──────────────────┬──────────────────┐      │
│   │ OpenGL 4.1     │ Vulkan 1.1+      │ Metal (macOS,     │      │
│   │ core 后端      │ 后端 (VMA)       │ Obj-C++ + MSL)   │      │
│   └────────────────┴──────────────────┴──────────────────┘      │
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
| `--backend gl\|vulkan\|metal` | 渲染后端（默认 gl；metal 仅 macOS） |
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

## v2.1 新增能力

### SDF 文字渲染（2D 屏幕 + 3D 世界）
`src/genesis/text/text_system.hpp`：用 stb_truetype 将字形烘焙为**真
有向距离场**（R8 单通道图集，CPU 侧 Felzenszwalb 距离变换），片元着色器
一次 `smoothstep` 抗锯齿，任意字号 / 任意相机距离边缘都清晰。2D 文字画在
present 阶段（屏幕空间像素锚点），3D 文字画在 HDR 场景 pass（世界空间、
测试深度、面向相机的公告牌）。字体默认 `assets/fonts/DejaVuSans.ttf`，
可用 `EngineConfig::sdf_font_path` 指定。

### CommandBuffer：自由绘制命令列表 + 可编程渲染管线
`src/genesis/renderer/overlay_system.hpp` 实现了任务要求的两段式抽象：
- **组织（逻辑步）**：脚本在 `world.update` 阶段调用
  `genesis.cmd_fill2d / cmd_line2d / cmd_quad3d` 自由追加绘制命令，
  不再受限于 ECS Renderer + Camera 的固定管线；
- **提交（执行步）**：下一帧 render pass 内按录制顺序执行（2D fill/线、
  3D 四边形），命令表每帧自动清空。
- **可编程管线**：`genesis.post_effect(mode)` 在 tonemap 之后插入用户后
  处理 pass（0 关闭 / 1 反色 / 2 扫描线 / 3 暗角增强），着色器新增
  `shaders/post/effects.frag`，演示用户可自行组织渲染管线。

### 脚本侧完整 RTTI 接口（纯 Lua 搭场景）
`genesis.create_camera / create_light / create_environment / create_mesh`
直接用脚本创建相机 / 方向光·点光·聚光 / 环境(IBL+天空盒) / 程序化球体·
立方体·平面；`has_component` 查询实体；配合已有的 `load_model` 可完全
通过 Lua 构建真实场景（见 `assets/scripts/full_demo.lua`）。

### 材质系统完善 + 实时编辑
`RenderScene` 新增 `setMaterialBaseColor / setMaterialPBR / setMaterialEmissive`，
UI 滑条/颜色选择器可**实时**调整已有材质的基色、金属度、粗糙度并即时
反映到下一帧（无需重传网格）。

### Metal 渲染后端（第三原生后端）
`src/genesis/rhi/metal/metal_backend.{hpp,mm}` + `metal_shaders.hpp`：引擎
严格同时支持 **OpenGL / Vulkan / Metal** 三端。Metal 后端用 Objective-C++
实现，调用 Apple Metal（MTLDevice / MTLCommandQueue / MTLRenderCommandEncoder /
CAMetalLayer），**非 MoltenVK**；着色器用内嵌的 **Metal Shading Language (MSL)**
在设备创建时编译（`newLibraryWithSource`，prelude structs + 各 pass 函数合并为
一个库），逻辑着色器名与 GLSL/SPIR-V 一一对应（19 个 MSL 着色器），渲染器无需
感知后端。

已对齐能力（与 GL/Vulkan 一致）：
- 设备/交换链（CAMetalLayer nextDrawable→presentDrawable）、所有 RHI 对象
- **PerDraw 动态 UBO 偏移**（逐 draw 应用，网格矩阵正确）
- cubemap 面 framebuffer（按面/mip 创建 2D view）、cubemap 纹理（MTLTextureTypeCube）
- `generateMipmaps`（blit encoder）、存储模式（渲染目标 Private / CPU 上传 Shared）
- 完整场景：**Cook-Torrance PBR 直接光、天空盒、调试线、tonemap/可编程后处理、SDF 文字、
  CommandBuffer 自由绘制**
- **完整 IBL 环境光照**：equirect→cube、irradiance 余弦卷积、prefilter（GGX 重要性
  采样 + LOD）、BRDF LUT 四个 MSL 卷积 pass（资源槽 set×8+binding：U=b0、equirect=t1、
  cube=t2、材质 U=b8、albedo=t9）

描述符通过「set × 8 + binding」映射到 Metal 的 `[[buffer(n)]]` / `[[texture(n)]]` /
`[[sampler(n)]]` 资源槽，与 GL 的 uniform block / texture unit 公式一致。CMake 用
`GENESIS_BACKEND_METAL` 开关，仅在 Apple 平台默认开启并链接 Metal/MetalKit/Cocoa/
QuartzCore 框架；Linux 上自动关闭，不影响现有构建（47 个 GL 测试通过、跨后端 MATCH）。

### macOS Vulkan 修复
修复了 macOS（MoltenVK）上启动 Vulkan demo 报
`VK_ERROR_FEATURE_NOT_PRESENT: ... 16th flag` 无法创建窗口的问题：
设备特性请求改为按物理设备**实际支持**逐项请求（`wideLines`/
`largePoints` 等 MoltenVK 不支持的项不再强制开启），LLVMPipe 上
GL/Vulkan 像素级跨后端比对仍为 MATCH。

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
assets/scripts/demo_anim.lua  Lua 演示脚本（转动模型）
assets/scripts/full_demo.lua   Lua 全能力演示（RTTI 建景 + 命令绘制 + SDF 文字）
assets/fonts/DejaVuSans.ttf    SDF 文字字体
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
                              overlay_system（CommandBuffer 自由绘制 + 可编程后处理）,
                              shaders/{common,scene,ibl,post,ui,generated}
src/genesis/text              text_system（stb_truetype 真 SDF 图集，2D + 3D 文字）
src/genesis/ui                ui_system（ImGui 双后端）
src/genesis/script            script_system（Lua 5.4 + 60 个引擎 API + 沙箱）
src/genesis/engine            engine（帧编排 facade）
src/demo1                     v1 原始 macOS 摄像头 Demo（-DGENESIS_LEGACY_DEMO1=ON，仅 macOS）
demo/main.cpp                 v2 演示程序
tests/                        47 个测试用例（CPU + 双后端 GPU + 跨后端比对）
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
