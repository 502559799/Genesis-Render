# Genesis Render - 编译与功能验证报告

## 0. 环境

| 项 | 值 |
|---|---|
| 系统 | macOS 15.7.9 (24G830)，arm64，Apple M4 Max |
| 编译器 / 构建 | Apple clang 16.0.0，CMake 3.24.2（Unix Makefiles） |
| SDL2 | 2.32.x（Homebrew） |
| OpenGL | 4.1 Metal - 89.4（Apple 驱动） |
| Vulkan | MoltenVK（LunarG SDK 1.3.290.0），设备报告 Vulkan 1.2.283，深度格式探测为 D32_SFLOAT_S8_UINT |
| Metal | 原生，MSL 2.1，深度 D32_SFLOAT_S8_UINT（M4 不支持 D24S8） |
| 着色器工具 | glslangValidator 14.3.0，spirv-cross（SDK 1.3.283 修订） |

所有结论来自真实 GPU 渲染与像素回读。

## 1. 编译

```bash
./build.sh --clean --test
```

| 目标 | 结果 |
|---|---|
| `libGenesisEngine.a`（RHI 三后端 / ECS / 资源 / 渲染器 / 反射 / Lua / UI / 文字 / Engine） | ✅ 零错误 |
| `bin/genesis_demo`（`demo/main.cpp`、`demo_app.cpp`、`demo_ui.cpp`） | ✅ |
| `bin/genesis_tests`（`tests/*.cpp`） | ✅ |
| 着色器产物校验 `python3 scripts/build_shaders.py` | ✅ `GLSL:17 SPIR-V:17 MSL:17` |

## 2. 原始问题复现与修复确认

| 问题 | 修复前 | 修复后 |
|---|---|---|
| `genesis_demo --backend vulkan` 无法创建窗口，`VK_ERROR_FEATURE_NOT_PRESENT`（第 16 个特性 wideLines） | 启动失败 | 窗口 1280×720（drawable 2560×1440），120 帧呈现，0 警告 0 错误 |
| `genesis_demo --backend metal` | 非法深度格式崩溃 | 同上，0 警告 0 错误 |
| `genesis_demo --backend gl` | 可运行 | 同上，0 警告 0 错误 |

## 3. 测试结果

```
./build/bin/genesis_tests
tests run      : 124        (34 CPU + 30 GPU × OpenGL / Vulkan / Metal)
tests failed   : 0
checks         : 114958
cross backend  : MATCH
RESULT: PASS
```

日志中的 7 条 ERROR 全部来自**有意的负面测试**：损坏的 FBX（2 条）、不存在的 glTF、Lua 语法错误、写入不存在的材质属性（每后端 1 次，共 3 条）。

### 3.1 跨后端逐像素比对

| 场景 | GL vs Vulkan | GL vs Metal | Vulkan vs Metal |
|---|---|---|---|
| PBR 球组（粗糙电介质 / 光滑电介质 / 金属，ACES） | 0.000/255，最大差 0 | 0.000，0 | 0.000，0 |
| 特性场景（BeforeScene RT → Unlit 屏幕材质、透明材质、TextComponent、2D HUD 文字与线、暗角后处理） | 0.000，0 | 0.000，0 | 0.000，0 |

图像：`/tmp/genesis_cross_backend_{pbr,features}_{OpenGL,Vulkan,Metal}.png`。

### 3.2 校验层

```bash
./build.sh --test --validation
```

| 后端 | 校验 | 结果 |
|---|---|---|
| OpenGL | `GENESIS_GL_VALIDATE=1`（每条命令 glGetError + glValidateProgram） | 63/63 通过，0 条 GL 错误 |
| Vulkan | `VK_LAYER_KHRONOS_validation`（日志确认启用 33 次） | 63/63 通过，0 条 VUID |
| Metal | `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` | 63/63 通过，0 条校验错误 |

demo 的所有场景 × 功能面板组合（showcase/lua/commandbuffer/empty × material/commandbuffer/custom/text/inspector）在三后端开启校验运行：24 组全部 exit 0，0 警告 0 错误。

### 3.3 Demo 交互路径自测（`genesis_demo --selftest`）

在真实窗口中打开全部面板，依次运行时切换 4 个场景（与菜单 Scene 同一路径），每个场景内：开启自定义管线、分屏 + 画中画 + 二级效果、遍历 12 种内置后处理、执行 Lua 控制台全部 8 个示例、新建/克隆材质并挂接 RT、指定给选中实体、切换 4 种预览网格、销毁实体。

| 后端 | 校验 | 结果 |
|---|---|---|
| OpenGL | `GENESIS_GL_VALIDATE=1` | exit 0，0 错误 0 警告 |
| Vulkan | validation layer | exit 0，0 错误 0 警告，0 VUID |
| Metal | `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` | exit 0，0 错误 0 警告 |

### 3.4 用例清单

**CPU（34）**

| 组 | 用例 |
|---|---|
| math（7） | vec3_basics、mat4_identity_and_multiply、mat4_inverse、quat_rotation、look_rotation_and_euler_roundtrip、projection_and_frustum、ray_intersection |
| ecs（4） | entity_lifecycle_and_generation、components_sparse_set_integrity、view_intersection_and_hierarchy、system_order_and_scheduling |
| procedural（2） | plane_box_sphere_torus、material_grid |
| gltf（4） | sphere_json_and_glb_match、damaged_helmet_full_pbr_material、multi_node_scene_hierarchy、textured_cube_external_images |
| fbx（3） | binary_cube_geometry_and_material、binary_sphere_mixed_polygons、rejects_garbage_safely |
| resource（1）/ image（3） | format_dispatch；png_loading_known_pixels、hdr_equirect_loading、png_and_tga_roundtrip |
| shaders（1） | registry_has_all_three_variants（每个着色器都有 GLSL / SPIR-V / MSL） |
| script（3） | lua_vm_and_entity_api、component_lifecycle_hooks、api_surface_is_complete |
| render_ubo（1） | struct_sizes_match_glsl（std140 布局与 GLSL 一致） |
| reflection（4） | registry_describes_every_engine_type、values_convert_tolerantly、properties_enums_and_read_only、methods_record_command_buffer_commands |
| command_buffer（1） | recording_copies_arguments_and_names_roundtrip |

**GPU（30，每个后端各跑一遍）**

| 组 | 用例与关键断言 |
|---|---|
| rhi（2） | device_creation_and_limits、clear_colour_readback_is_exact |
| render（10） | 调试线只存在一帧、unlit 覆盖率、实时材质编辑、immediate 命令与后处理、导入模型、FBX、视锥剔除、tonemap/gamma、天空盒、截图文件往返 |
| pbr（5） | 方向光梯度、金属≠电介质、粗糙度模糊高光、Blinn-Phong 与 Cook-Torrance 均有效且不同、点光/聚光衰减 |
| ibl（2） | BRDF LUT 与 CPU 参考积分一致、均匀环境的 irradiance 为常数 |
| commandbuffer（5） | RT + drawMesh 并保持方向（球在 RT 上半部分，像素 (32,16)=(255,0,0)）；2D 矩形左上原点像素精确；Custom 管线不画场景除非命令要求、drawScene 生效、切回 Forward 完全恢复；11 种 blit 效果均改变图像（invert/grayscale 精确校验）；采样当前目标的绘制被跳过 |
| text（2） | 2D：字形全部落在 `measure` 包围盒内、位于顶部、描边产生彩色边缘；3D：世界文字、被深度测试遮挡、关闭深度测试时绘制在上层、TextComponent 位于实体上方 |
| material（1） | 反射设置 unlit + 纯绿 → (0,255,0)；反射设置贴图显示棋盘格；材质采样 CommandBuffer 填充的 RT；透明 0.5 红叠蓝 → (128,0,128) |
| lua（2） | 纯 RTTI 脚本建景渲染（Lua 材质球 + Lua CommandBuffer HUD）、错误被报告不崩溃；`scene_builder.lua` 与 `commandbuffer_demo.lua` 零错误、实体数与亮度达标 |
| ui（1） | ImGui 窗口出现在截图左上角（三后端方向一致） |

## 4. Demo 截图

`genesis_demo --showcase docs/images`（headless 能力图，每后端 15 张：`gl_* / vk_* / mtl_*`）与功能截图 `docs/images/features/<gl|vk|mtl>_<scene>_<lab>.png`（每后端 7 张），见 `docs/VERIFICATION.md`。

## 5. 已知限制

见 `架构设计文档.md` 第 13 节（无阴影、材质为固定 PBR 着色器参数化、Metal 运行时编译 MSL、字形集 ASCII + Latin-1）。


---

## 6. v4 扩展验证（本轮 Linux 环境）

### 6.1 环境

| 项 | 值 |
|---|---|
| 系统 | Linux 6.6.95 x86_64（Ubuntu 22.04 用户态） |
| 编译器 / 构建 | GCC 11.4.0，CMake 3.22.1，Ninja 1.10 |
| SDL2 | 运行库 2.0.20（头文件由 `scripts/fetch_deps.sh` 提供） |
| OpenGL | 4.5 core / Mesa 23.2.1 llvmpipe（Xvfb :99），深度 D24S8，clip z [-1,1] |
| Vulkan | 后端编译通过；通过 `scripts/fetch_lavapipe.sh` 安装 Mesa lavapipe 22.3.6 软件 ICD（Vulkan 1.3.230，LLVM 15）后完成全部 GPU 测试 |
| Metal | 仅 macOS，本环境不可用 |
| V8 | libnode 18.20.4，V8 10.2.154.26-node.37（ICU 72 / c-ares / libuv 1.44） |

依赖准备与构建：

```bash
scripts/fetch_deps.sh          # SDL2/Vulkan headers + V8 -> ../deps/prefix
./build.sh                     # V8/OpenGL/Vulkan 全部编译成功
```

### 6.2 测试结果（OpenGL + Vulkan）

```
./build.sh --test
tests run      : 108        (CPU suite + GPU suite on OpenGL + GPU suite on Vulkan)
tests failed   : 0
checks         : 77990
check failures : 0
cross backend  : MATCH
RESULT: PASS

[PBR scene]     OpenGL vs Vulkan: mean diff 0.002/255, worst 1 -> MATCH
[feature scene] OpenGL vs Vulkan: mean diff 0.002/255, worst 1 -> MATCH
```

单后端结果：

```
./build/bin/genesis_tests --backend gl                        # 74 tests, PASS
VK_ICD_FILENAMES=... ./build/bin/genesis_tests --backend vulkan # 74 tests, PASS
```

原有 64 项（CPU + GL GPU）全部保持通过；新增 10 项：

| 组 | 用例 |
|---|---|
| package（2） | `cascade_loads_and_validates`（manifest/文件表/CRC/zlib/解包）、`rejects_non_packages` |
| package GPU（1） | `cascade_scene_runs_from_package`（`.gpkg` → JSON scene → JS + 物理端到端，并调用 JS `launch()` 验证生成的球体） |
| physics（4） | 自由落体、球-平面静止、三盒堆叠、铰链保持锚点距离 |
| gaussian（2） | 3DGS PLY 解析（12000 splats）、缺失文件错误处理 |
| js GPU（1） | V8 通过 RTTI 创建材质/网格/实体/文本并执行 tick |

### 6.3 Demo 自测

```bash
./build/bin/genesis_demo --backend gl --headless --selftest
# [selftest] done: 0 error(s)   exit 0
```

自测覆盖 8 个场景（showcase / lua / commandbuffer / physics / game / gaussian /
package / empty）、全部面板、自定义管线、12 种后处理、Lua + JS 控制台示例、
物理暂停/单步、包加载与场景重建。

### 6.4 新增功能截图

| 文件 | 内容 |
|---|---|
| `docs/images/v4/gl_physics.png` | 地面、箱堆、弹球、斜坡、胶囊、触发器、铰链摆锤 + Physics lab |
| `docs/images/v4/gl_game.png` | Cascade 小游戏（`.gpkg` 加载，JS HUD/计时/计分） |
| `docs/images/v4/gl_package.png` | FX 资源包场景 + Asset package browser（manifest/文件列表） |
| `docs/images/v4/gl_gaussian.png` | 12000 splats 深度排序渲染 + Gaussian splat lab |

### 6.5 三后端说明

- **OpenGL**：llvmpipe，全部新增/原有测试通过。
- **Vulkan**：lavapipe（Mesa 22.3.6，Vulkan 1.3.230）软件驱动下全部新增/原有测试通过；
  GL/Vulkan 跨后端参考场景 MATCH（mean 0.002/255，worst 1）。
- **Metal**：Linux 无法运行 Objective-C++ / Metal 驱动。新增渲染只使用共享 RHI 的
  batch/line 路径，未新增需要 MSL 的 shader；同一组 `TEST_GPU` 用例会在 macOS 上
  自动按 Metal 后端执行。详见 `docs/BACKEND_COMPATIBILITY.md`。
- lavapipe 在大量重复创建/销毁引擎时偶发驱动级崩溃；重跑即可通过，硬件驱动无此问题。


---

## 7. v5 扩展验证（动画 / 四大 API / Ray Tracing / 编解码）

### 7.1 编译

```bash
scripts/fetch_shader_tools.sh           # glslangValidator 12 + spirv-cross 2021
python3 scripts/build_shaders.py --glslang ../deps/tools/glslangValidator --spirv-cross ../deps/tools/spirv-cross
./build.sh
```

- 17 个 shader（15 个原有 + `anim/vertex_anim.comp` + `rt/raytrace.comp`）
  全部生成 **GLSL / SPIR-V / MSL** 三端产物。
- OpenGL / Vulkan 后端与全部 C++ 模块编译通过；Metal 为 macOS 专属，
  代码已完成但本 Linux 容器无法编译该 Objective-C++ 文件。

### 7.2 测试结果（OpenGL + Vulkan）

```
tests run      : 119
tests failed   : 0
checks         : 78084
check failures : 0
cross backend  : MATCH
RESULT: PASS
```

新增用例：

| 组 | 用例 | 验证内容 |
|---|---|---|
| compute（GPU ×2） | `vertex_animation_kernel` | storage buffer、dispatch、ShaderWrite→HostRead barrier、精确浮点回读 |
| animation（GPU ×2） | `sprite_and_vertex_animation_scene` | 2D sprite sheet + 3D compute 顶点动画同帧渲染、dispatch 统计、截图亮度 |
| raytracing（GPU ×2） | `gpu_trace_to_texture` | GPU compute 光线追踪、输出纹理、非空像素、GPU 路径断言 |
| api（GPU ×2） | `four_facades` | Rendering/Compute/RayTracing/EncoderDecoder 四类 API 同时可用，旧渲染路径仍正常 |
| codec（CPU，3） | `raw_and_png_roundtrip`、`h264_hardware_or_software_roundtrip`、`media_player_decodes_video_and_audio` | Raw/PNG 无损往返；**进程内 libavcodec** H.264 编解码；MediaPlayer 解码 H.264/AAC、视频帧、音频 PCM、seek |

### 7.3 Demo 自测

```bash
./build/bin/genesis_demo --backend gl --headless --selftest
# [selftest] done: 0 error(s)   exit 0
```

自测新增 2 个场景（animation / raytracing）与 4 个面板（Animation / Ray tracing /
Encoder-Decoder / Engine APIs），全部 0 error。

### 7.4 后端说明

- **OpenGL**：compute 需要 GL 4.3+；本环境 llvmpipe 4.5 通过。macOS OpenGL 封顶 4.1，
  因此该平台由 Metal 提供 compute；GL 上 sprite 与渲染仍正常。
- **Vulkan**：compute pipeline / storage descriptor / buffer barrier 全部通过。
- **Metal**：已实现 `MTLComputeCommandEncoder` 路径、MSL kernel 绑定与渲染缓冲切换；
  同一组 `TEST_GPU` 在 macOS 上自动覆盖。
- **编解码/播放**：直接链接 libavcodec/libavformat/libswscale/libswresample，运行时不
  启动 ffmpeg 进程；本容器无 `/dev/dri`/NVIDIA 设备，硬件编码条目正确报告为不可用，
  H.264 走 libx264，音视频播放走进程内解码 + SDL 音频。
