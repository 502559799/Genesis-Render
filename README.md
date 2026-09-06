# Genesis Render

一个小型跨平台 3D 渲染引擎，在原 Vulkan 相机滤镜 Demo 基础上重构，能力对齐
[Piccolo 引擎](https://github.com/BoomingTech/Piccolo)：**OpenGL 4.1 / Vulkan 双后端**、
组件式 ECS、PBR + 图像基光照（运行时 cubemap/irradiance/prefilter/BRDF 烘焙）、
Assimp 多模型导入（glTF2/glb、FBX、OBJ …）、Lua 脚本、Dear ImGui 实时编辑器。

## 特性

- **双 RHI 后端**：同一套 `IRHIDevice` 接口同时落地 Vulkan 1.1+（SPIR-V）与
  OpenGL 4.1 core（GLSL 运行期转换，可运行于 macOS；Vulkan 侧兼容 MoltenVK
  portability）。
- **单一 shader 源码**：Vulkan 风格 GLSL（set/binding/push_constant/#include）
  构建期编译为 SPIR-V，运行期同一源码转换为桌面 GLSL，无需手写两份。
- **ECS**：generation 句柄实体、泛型组件池、多组件视图、Transform 父子层级、
  System 基类。
- **PBR**：金属-粗糙度工作流；直接光支持 GGX Cook–Torrance **或** Blinn-Phong
  （明确的 fresnel/specular/diffuse/metallic 项，可在 UI 切换）；
  间接光为运行时 split-sum IBL：equirect→cubemap、irradiance 余弦卷积、
  GGX prefilter、BRDF LUT；ACES 色调映射 + sRGB。
- **模型导入**：Assimp 加载 glTF2/glb、FBX（含构建期生成的 `genesis_cube.fbx`）、
  OBJ；自动法线/切线、材质槽映射、嵌入纹理解码、sRGB/mipmap、glTF 通道约定。
- **Lua 5.4 脚本**（sol2，每实体沙箱 environment，onStart/onUpdate，可热重载），
  可控制变换、灯光、材质，支持键盘查询。
- **Dear ImGui 编辑器**：场景层级、检查器（Transform/Camera/Light/Material/
  Environment/Lua）、渲染设置（光照模型、IBL、天空盒、强度），可运行期 Add
  Primitive、Reload Scripts。
- 程序化资产兜底：立方体/球/平面网格与程序化 HDR 天空，无需联网即可验证全部管线。

## 依赖

| 依赖 | 提供方式 |
|---|---|
| SDL2、Assimp、Vulkan Loader/SDK、OpenGL | 系统包 / SDK（CMake find_package） |
| Dear ImGui 1.86、Lua 5.4 + sol2、glad 4.1、stb_image/write、cglm、enkiTS、TLSF | 随仓 `src/external/`（部分系统装） |

Ubuntu 示例：

```bash
sudo apt install cmake build-essential libsdl2-dev libassimp-dev \
                 libvulkan-dev glslang-tools mesa-utils
```

## 构建

```bash
# Linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# macOS（OpenGL；安装 LunarG Vulkan SDK 后自动启用 Vulkan/MoltenVK）
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

辅助脚本：`tests/run_validation.sh`（构建并双后端自测）。

## 运行

```bash
cd build
./bin/GenesisPBR --backend vulkan           # Vulkan
./bin/GenesisPBR --backend opengl           # OpenGL 4.1
```

操作：左键拖拽环绕、滚轮缩放；UI 中可切换光照模型/IBL/天空、改材质与灯光、
添加 Cube/Sphere/Plane、Reload Lua。

### 命令行参数

| 参数 | 说明 |
|---|---|
| `--backend opengl\|vulkan` | RHI 后端（默认 vulkan，找不到自动回落 opengl） |
| `--width/--height` | 窗口分辨率 |
| `--frames N` | N 帧后退出（CI/无头环境） |
| `--selftest` | 自动验证（4 种光照组合截图 + 18 项像素/资源断言） |
| `--screenshot out.png` | 退出前保存场景 RT |
| `--model file` | 额外导入 glTF/FBX/OBJ |
| `--hdri file.hdr` | 指定 equirect HDR（否则使用内置程序化天空） |
| `--script file.lua` | 给主模型挂载脚本 |

无头/CI（Xvfb + lavapipe/llvmpipe）：

```bash
tests/run_validation.sh            # 双后端 18/18 断言
# 或指定 lavapipe ICD：
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
    ./bin/GenesisPBR --backend vulkan --selftest
```

自测会在运行目录写出 `screenshot_<backend>_{pbr_ibl,blinnphong_ibl,
direct_only,ibl_only}.png`。

## 目录结构

```
src/genesis/
  foundation_layer/  基础：数组、TLSF 内存、日志、文件
  application_layer/ SDL2 窗口（按后端创建 GL/VK 表面）
  math/              向量/矩阵/四元数
  rhi/               IRHIDevice + rhi_types + GLSL 预处理 + opengl/ + vulkan/
  resource/          Assimp 导入器、stb 图像、AssetManager、程序化网格
  ecs/               Scene / Entity / ComponentPool / 内建组件
  render/            Renderer、IBL Baker、std140 UBO、GLSL shader 头
  script/            Lua/sol2 脚本系统
  ui/                ImGui 编辑器
src/demo/pbr_demo/   GenesisPBR 可执行（场景搭建/自测）
assets/              HDR、glb 样例、Lua；FBX 由构建期 gen_fbx 生成
assets/shaders/glsl/ PBR / IBL / blit shader（单一源码，两端共用）
cmake/embed_shaders.py  GLSL→SPIR-V + 源码嵌入
tools/gen_fbx.cpp    构建期 FBX 生成工具
tests/run_validation.sh 双后端一键验证
```

完整架构（类图、时序图、绑定表、踩坑记录）：
**[docs/ARCHITECTURE_FULL.md](docs/ARCHITECTURE_FULL.md)**。
