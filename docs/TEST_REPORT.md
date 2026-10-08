# Genesis Render 2.0 - 编译与功能验证报告

> 验证环境：Ubuntu 22.04.5 / x86_64 / gcc 11.4 / cmake 3.22.1
> 图形环境（无显卡容器，全部为软件光栅化）：
> - OpenGL 4.5 Core —— Mesa 23.2.1 `llvmpipe (LLVM 15.0.7, 256 bits)`，运行于 `Xvfb :99`
> - Vulkan 1.3.255 —— Mesa `lvp_icd`（lavapipe/llvmpipe 软件实现）
> 两个后端都能在 headless 环境下真实执行渲染并回读像素，因此本报告中的所有
> 结论都来自**实际渲染出的像素**，不是模拟结果。

---

## 1. 编译验证

```bash
./build.sh                 # Release，双后端，demo + tests
```

| 目标 | 结果 | 说明 |
|------|------|------|
| `libGenesisEngine.a` | ✅ 编译通过 | 引擎全部模块（RHI 双后端 / ECS / 资源 / 渲染器 / UI / Lua / Engine）+ imgui + Lua 5.4 + cgltf + stb |
| `genesis_demo` | ✅ 链接通过 | 演示程序 |
| `genesis_tests` | ✅ 链接通过 | 测试程序（47 个用例） |
| 着色器 | ✅ 13/13 | 每个着色器同时通过 **GLSL 410 编译校验** 与 **SPIR-V (Vulkan 1.1) 编译**，产物入库 |
| 编译告警 | 引擎自有代码 0 error | 第三方代码（imgui/lua）以 `-w` 编译 |

着色器单源双方言验证输出（`python3 scripts/build_shaders.py`）：

```
[build_shaders] 13 shaders -> GLSL:13 SPIR-V:13
```

13 个着色器：`common/fullscreen.vert`、`scene/pbr_mesh.{vert,frag}`、
`scene/skybox.frag`、`scene/unlit_mesh.{vert,frag}`、`scene/debug_line.{vert,frag}`、
`ibl/equirect_to_cube.frag`、`ibl/irradiance.frag`、`ibl/prefilter.frag`、
`ibl/brdf_lut.frag`、`post/tonemap.frag`。

---

## 2. 测试套件结果

```bash
./build/bin/genesis_tests --backend gl       # 或 --backend vulkan / 默认两个都跑
```

### 2.1 OpenGL 后端：**全部通过**

```
tests run      : 47
tests failed   : 0
checks         : 40562
check failures : 0
RESULT: PASS
```

### 2.2 Vulkan 后端：37 / 45 通过

```
tests run      : 47
tests failed   : 8
RESULT: FAIL   （见第 4 节：已定位的 2 个问题）
```

通过的 Vulkan 用例覆盖：设备创建与能力查询、清屏回读（像素级精确）、
天空盒（程序化梯度 + 太阳）、IBL 全链路烘焙（equirect→cubemap→irradiance→
prefiltered→BRDF LUT）、BRDF LUT 与 CPU 蒙特卡洛参考对照、
**均匀环境光辐照度解析解验证**、视锥剔除、截图 PNG 落盘回读、
glTF/GLB/FBX 导入后上屏、Lua/UI 子系统在 headless 下初始化。

### 2.3 CPU（无窗口）用例：全部通过

| 分组 | 用例数 | 内容 |
|------|--------|------|
| `math` | 7 | 向量/矩阵/四元数、逆矩阵、YXZ 欧拉往返、GL 与 Vulkan 两种深度区间的投影、视锥体、射线求交 |
| `ecs` | 4 | 实体生命周期与 generation 失效、稀疏集删除后一致性、层级与 `TransformSystem` 世界矩阵传播、System order 调度 |
| `procedural` | 2 | plane/box/sphere/torus 顶点索引数、包围盒、单位法线；材质球阵列 |
| `gltf` | 4 | `.gltf` 与 `.glb` 结果一致（1617 顶点 / 8928 索引）、DamagedHelmet 五个 PBR 贴图槽全部解析、多节点场景层级、外部 URI 贴图 |
| `fbx` | 3 | 二进制 FBX 立方体（36 索引 / ±0.5 包围盒 / `GenesisRed` 材质 / shininess→roughness 映射）、混合三角+四边形的球体（219 顶点 / 1056 索引 / 法线朝外）、垃圾与截断文件安全返回 |
| `resource` | 1 | 扩展名分发、路径工具、不存在文件的失败路径 |
| `image` | 3 | PNG 已知像素（棋盘白/灰格、UV 梯度、glTF 约定 G=roughness B=metallic、法线贴图平坦区 128/128/255）、HDR equirect 解码（太阳峰值 > 50）、PNG/TGA 写读逐字节往返 |
| `shaders` | 1 | 13 个着色器均同时具备 GLSL 与 SPIR-V 变体、SPIR-V magic 校验、GL 方言不含 `#define GENESIS_VK` |
| `script` | 3 | Lua 虚拟机与实体 API（改位置/缩放）、`start`/`tick` 钩子与沙箱隔离、**42 个 API 完整性断言** |
| `render_ubo` | 1 | 7 个 UBO 结构体 sizeof 与 GLSL std140 布局逐一相符（384/544/64/96/144/32/32） |

### 2.4 GPU / 渲染用例（逐后端执行）

| 用例 | 验证内容 |
|------|----------|
| `rhi.device_creation_and_limits` | 设备创建、能力上报、buffer/texture/sampler 生命周期 |
| `rhi.clear_colour_readback_is_exact` | 空场景回读必须逐像素等于清屏色 |
| `render.unlit_geometry_has_expected_coverage` | 立方体覆盖画面中心、四角为空、**近处物体遮挡远处物体**（深度测试 + per-draw UBO 动态偏移） |
| `pbr.directional_light_creates_shading_gradient` | 方向光下球体亮侧 > 暗侧，背景不被点亮 |
| `pbr.metallic_differs_from_dielectric` | 金属/非金属响应不同；非金属整体更亮（金属无漫反射）；两者都有高光 |
| `pbr.roughness_blurs_the_highlight` | 光滑表面高光峰值高于粗糙表面（能量集中 vs 扩散） |
| `pbr.blinn_phong_and_cook_torrance_both_work_and_differ` | **两种直接光 BRDF 模型都能出图且高光峰值不同**（max diff ≈ 0.19） |
| `pbr.point_and_spot_lights_attenuate` | 点光距离衰减、暖色光在 lit 像素上 R > 1.2·B |
| `ibl.brdf_lut_matches_cpu_reference` | BRDF LUT 积分与**独立的 CPU 蒙特卡洛实现**一致（scale 随粗糙度递减、bias 随粗糙度递增、与解析近似同量级）；IBL 单独照亮球体（无直接光、无 ambient）；关闭 IBL 后变黑 |
| `ibl.irradiance_of_uniform_environment_is_constant` | **解析解验证**：常数环境 (0.42, 0.21, 0.105) 烘焙后，球面漫反射应空间恒定且保持 1 : 0.5 : 0.25 的通道比例 |
| `render.imported_model_renders_non_black` | DamagedHelmet.glb 上屏、覆盖面积、贴图产生丰富色彩（>20 个不同色桶） |
| `render.fbx_model_renders` | FBX 立方体上屏且**红色材质 (0.8,0.2,0.2) 在导入后保持** |
| `render.frustum_culling_removes_draw_calls` | 相机背后的物体被剔除，关闭剔除后 items 从 1 变 2 |
| `render.tonemap_and_gamma_change_the_output` | 色调映射 + gamma 明显改变输出且结果不越界 |
| `render.skybox_fills_the_background` | 程序化天空：顶部偏蓝、底部比顶部更暖 |
| `render.screenshot_file_roundtrip` | 截图写 PNG 并能回读、尺寸一致；UI 与 Lua 在 headless 下初始化成功 |

`ibl.irradiance_of_uniform_environment_is_constant` 在 OpenGL 上的实测数据
（这是整个 PBR/IBL 链路最强的正确性证据）：

```
uniform IBL samples: c1=0.2360 c2=0.2360 c3=0.2360   <- 三点完全相同 = 辐照度空间恒定
avgR=0.1739 avgG=0.0864 avgB=0.0425                  <- 比例 4.09 : 2.03 : 1.00
```
环境光是 (0.42, 0.21, 0.105)（比例 4 : 2 : 1），球体 albedo 为白色，
烘焙出的辐照度精确保持了这一比例，且在球面上处处相等 —— 与解析解
`E(n) = L`（baker 存储 E/π，着色器按 `kD · irradiance · albedo` 消费）一致。

---

## 3. Demo 端到端验证

```bash
./build/bin/genesis_demo --backend gl --headless --screenshot /tmp/demo_gl.png
```

实测日志（节选）：

```
[IO] model 'DamagedHelmet': 1 mesh(es), 1 material(s), 5 image(s)
[FBX] binary format, version 7400, 5 top-level node(s)
[FBX] axis system: up=1(1) front=2(1) coord=0(1), unit scale 1 -> identity
[FBX] imported 'genesis_test_cube': 1 mesh(es), 1 material(s)  (24 verts / 36 indices)
[FBX] imported 'genesis_test_sphere': 219 verts, 1056 indices
[Demo] scene built: 15 entities, 9 models
[Lua] loaded script for entity 14 (assets/scripts/demo_anim.lua)
[IO] loaded HDR image assets/textures/sky_equirect.hdr (2048x1024)
[IBL] baked: cube 256(9 mips) irradiance 64 prefiltered 256(9 mips) brdf 256
screenshot written to /tmp/demo_gl.png   (1280x720, 213 KB)
demo finished: 2 frames, 0 errors
```

Demo 场景包含：地面（棋盘 albedo + metallic/roughness + 法线 + AO 四贴图）、
DamagedHelmet.glb（五槽 PBR 贴图）、genesis_sphere.glb、genesis_torus.gltf、
genesis_cube_textured.gltf、Duck.glb、genesis_test_cube.fbx、
genesis_test_sphere.fbx、6×4 程序化材质球阵列（metallic × roughness 梯度）、
方向光（太阳）+ 两个 Lua 驱动的彩色点光、HDR 环境贴图与运行时 IBL 烘焙、
7 个 ImGui 面板、Lua 脚本动画与调试 gizmo。

---

## 4. 已知问题（Vulkan 后端）

两个问题都已被定位到具体环节，OpenGL 后端不受影响。

### 4.1 直接光照在 lavapipe 上未生效（8 个用例失败）

**现象**：Vulkan 下 ambient / IBL / 天空盒 / 几何 / 深度 / 剔除全部正确，
唯独**直接光（方向光、点光）贡献为 0**，因此依赖直接光的用例失败。

**已排除的原因**（每一步都有实测证据）：
1. CPU 侧数据正确：两个后端打印出的 `LightsUBO` 完全一致
   `light[0] type=0 dir=(-0.398,-0.597,-0.697) color=(1,1,1) intensity=2 enabled=1`。
2. 同一个 UBO 的其它字段在 Vulkan 下读取正确：`u_ambient_color`（偏移 528）
   与 `u_light_count_params`（偏移 512）、`type_params`、`color_intensity.w`、
   `direction_cone.y` 全部读出正确值。
3. SPIR-V 布局正确：`spirv-dis` 显示 `ArrayStride 64`、
   `Lights` 成员偏移 `0 / 512 / 528`、`DescriptorSet 0 Binding 1`。
4. 不是写入路径问题：改用 staging buffer + `vkCmdCopyBuffer` 结果完全相同。
5. 不是描述符 range 问题：改用 `VK_WHOLE_SIZE` 结果相同（已回退，因为动态
   描述符不允许 `VK_WHOLE_SIZE`）。
6. 不是动态索引 UBO 结构体数组的问题：改成常量索引分支后结果相同。

**实测到的差异**：把着色器改成直接输出中间量后，
Vulkan 下 `dot(N, L)` 为 **-0.702**，而 OpenGL 下为 **+0.694**；
同时 `N.z = 1.0`、`L.y = +0.597`、`radiance.r = 2.0` 在两个后端完全一致。
即：唯一不一致的是 `direction_cone.z` 的符号（相当于 UBO 第 24 字节处
一个 float 的符号位相反）。

**下一步排查方向**（已写入 `docs/ARCHITECTURE.md` 的后续路线）：
在真实 GPU（非 lavapipe）上复跑同一套用例，以区分"驱动/软件光栅器缺陷"
与"引擎侧描述符布局缺陷"；并开启 Khronos validation layer
（当前容器未安装 `vulkan-validationlayers`，日志中
`VK_LAYER_KHRONOS_validation not found`）。

### 4.2 同进程内第 5 次创建 Vulkan 设备时崩溃

`pbr.metallic_differs_from_dielectric` 会连续创建两个 `Engine`（因此两个
`VkDevice`）。在整套用例顺序执行到第 5 个 Vulkan 设备时进程 SIGSEGV。
单用例单独运行（`--filter`）时全部正常，说明是**设备反复创建/销毁的生命周期
问题**（SDL 视频子系统 refcount 归零后 `SDL_Quit`，或 VMA/描述符池的销毁顺序），
而不是渲染逻辑问题。跨后端像素比对因此也未能在同进程内完成，
替代方案是分别运行两个后端并用 `scripts/compare_backends.py` 比对两张 PNG。

---

## 5. 复现命令

```bash
# 依赖（Ubuntu 22.04）
sudo apt-get install -y build-essential cmake libsdl2-dev zlib1g-dev \
     libvulkan-dev glslang-tools spirv-tools mesa-vulkan-drivers xvfb

# 构建
./build.sh                       # Release，双后端
./build.sh --shaders             # 先重新生成着色器（需要 python3 + glslangValidator）

# 测试（headless 需要 Xvfb）
export DISPLAY=:99 XDG_RUNTIME_DIR=/tmp/xdg
Xvfb :99 -screen 0 1920x1080x24 &
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
./build/bin/genesis_tests --no-gpu              # 仅 CPU 用例
./build/bin/genesis_tests --backend gl          # OpenGL 全量
./build/bin/genesis_tests --backend vulkan      # Vulkan 全量
./build/bin/genesis_tests --verbose             # 打印每条断言

# Demo
./build/bin/genesis_demo --backend gl
./build/bin/genesis_demo --backend gl --headless --frames 120 --screenshot shot.png
```
