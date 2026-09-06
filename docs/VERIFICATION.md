# 功能验证说明（如何验证 + 效果图在哪里）

本文件回答两个问题：**每一项新增能力是用什么方法验证的**、**运行效果图在哪**。

---

## 1. 效果图位置

全部效果图都在仓库内：[`docs/images/`](images/)，由引擎**自己离屏渲染 + 自己的 PNG
编码器**写出（`RenderSystem::renderAndCapture` → `ResourceIO::writeImagePNG`），
不是外部工具截图，因此图像本身就是被测代码的产物。

复现命令（无需显示器，无需显卡）：

```bash
./build.sh                                              # 构建
export DISPLAY=:99; Xvfb :99 -screen 0 1920x1080x24 &   # 虚拟显示（GL 后端需要）
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
./build/bin/genesis_demo --backend gl --showcase docs/images    # 生成 15 张验证图
./build/bin/genesis_demo --backend gl --headless --frames 60 \
        --screenshot docs/images/01_demo_full_scene_gl.png      # 完整 demo 场景
```

### 1.1 图片清单与实测数值

`mean` = 全图平均亮度，`nonblk` = 非黑像素占比，R/G/B = 通道均值。
数值由 `python3 -c` 直接解码 PNG 统计得到（脚本见本文末）。

| 图片 | 尺寸 | mean | nonblk | R | G | B | 证明了什么 |
|------|------|------|--------|---|---|---|-----------|
| `gl_pbr_grid_cook_torrance_direct.png` | 512×384 | 0.188 | 39.1% | 55.2 | 47.0 | 36.6 | 6×4=24 球阵列（metallic 左→右、roughness 上→下），**仅直接光** Cook-Torrance |
| `gl_pbr_grid_blinn_phong_direct.png` | 512×384 | 0.184 | 39.1% | 53.7 | 46.0 | 36.1 | 同场景切到 **Blinn-Phong**（与上一张逐像素不同） |
| `gl_pbr_grid_cook_torrance_ibl.png` | 512×384 | 0.295 | 39.1% | 83.7 | 74.2 | 61.3 | 打开 **IBL** 后亮度 0.188→0.295（间接光确实注入能量） |
| `gl_pbr_grid_blinn_phong_ibl.png` | 512×384 | 0.294 | 39.1% | 83.5 | 74.0 | 61.1 | Blinn-Phong + IBL |
| `gl_tonemap_none.png` | 512×384 | 0.265 | 39.1% | 79.5 | 65.6 | 50.9 | 色调映射 **关闭**（仅裁剪） |
| `gl_tonemap_reinhard.png` | 512×384 | 0.252 | 39.1% | 71.9 | 63.2 | 53.2 | **Reinhard** |
| `gl_tonemap_aces.png` | 512×384 | 0.295 | 39.1% | 83.7 | 74.2 | 61.3 | **ACES filmic** —— 三条曲线给出三个不同结果 |
| `gl_skybox_ibl_env.png` | 512×384 | 0.684 | 100% | 192.1 | 172.0 | 147.2 | 天空盒 + HDR 环境（暖色，R>G>B 符合日落天空） |
| `gl_debug_gizmos.png` | 512×384 | 0.201 | 40.5% | 58.4 | 50.5 | 39.5 | 调试线框/gizmo（走 RHI **push constant** 路径） |
| `gl_model_glb_damaged_helmet.png` | 384×384 | 0.319 | 99.2% | 66.5 | 83.4 | 105.7 | **GLB 导入** + 五槽 PBR 贴图（Khronos 官方样例资产） |
| `gl_model_glb_sphere.png` | 384×384 | 0.557 | 100% | 134.7 | 143.4 | 149.9 | GLB（自生成资产）导入 |
| `gl_model_gltf_torus.png` | 384×384 | 0.292 | 100% | 66.0 | 75.4 | 89.3 | **.gltf + 外部 .bin** 导入，metallic=1 圆环 |
| `gl_model_gltf_textured_cube.png` | 384×384 | 0.456 | 100% | 111.1 | 116.8 | 126.7 | .gltf 引用**外部 PNG 贴图**（albedo/MR/normal）导入 |
| `gl_model_fbx_cube.png` | 384×384 | 0.552 | 100% | **168.3** | 132.1 | 144.4 | **自研 FBX 二进制解析**：R 通道明显主导，与 FBX 里写的 DiffuseColor (0.8,0.2,0.2) 一致 |
| `gl_model_fbx_sphere.png` | 384×384 | 0.480 | 100% | 101.3 | 125.6 | **152.1** | FBX 球体（三角+四边形混合多边形）：B 通道主导，与 GenesisBlue 材质一致 |
| `01_demo_full_scene_gl.png` | 1280×720 | 0.428 | 100% | 111.1 | 108.7 | 106.2 | **完整 demo**：地面四贴图 + 8 个导入模型 + 材质球阵列 + 太阳/两点光 + HDR 环境 + IBL 烘焙 + Lua 动画 |

> 说明：`gl_model_fbx_cube.png` 的 R=168 明显大于 G/B，`gl_model_fbx_sphere.png`
> 的 B=152 明显大于 R/G —— 这两条是"FBX 里写的材质颜色经过解析→上传→着色→
> 回读后仍然正确"的端到端证据（单元测试 `fbx.binary_cube_geometry_and_material`
> 另外在 CPU 侧断言了 base_color_factor == (0.8, 0.2, 0.2)）。

### 1.2 视觉复核

`gl_model_glb_damaged_helmet.png` 另做了一次独立视觉模型复核，结论：
"可见完整头盔模型，无黑屏/几何破碎；带科幻纹路与金属磨损细节贴图；
有自然明暗过渡，面罩与金属边缘有清晰高光；背景为柔和渐变。"

---

## 2. 每项能力的验证方法

验证分三层，**层层可复现**：

| 层级 | 手段 | 数量 |
|------|------|------|
| L1 单元 | CPU 侧断言（数学/ECS/解析器/图片编解码/UBO 布局/Lua API） | 29 个用例 |
| L2 像素 | 真实创建 GPU 设备 → 离屏渲染 → **回读像素** → 断言 | 16 个用例 |
| L3 端到端 | demo 全场景跑通 + 截图 + 日志 0 error | 1 条链路 |

```
tests run      : 45        checks: 40562        check failures: 0        RESULT: PASS   (OpenGL)
```

### 2.1 逐能力对照表

| 需求 | 验证用例 / 证据 | 判据（实测） |
|------|----------------|-------------|
| **ECS 架构** | `ecs.entity_lifecycle_and_generation`、`ecs.components_sparse_set_integrity`、`ecs.view_intersection_and_hierarchy`、`ecs.system_order_and_scheduling` | 64 实体删 22 个后稀疏集仍一致（存活 42、tag 21）；父子层级世界矩阵 (10,0,0)+(1,2,3)=(11,2,3)；System 按 order 100<200<300 执行 |
| **glTF 导入** | `gltf.sphere_json_and_glb_match`、`gltf.textured_cube_external_images`、`gltf.multi_node_scene_hierarchy` | .gltf 与 .glb 结果一致（1617 顶点/8928 索引）；外部 URI 贴图解析到磁盘文件；3 节点场景 flatten 出 3 个带变换的网格 |
| **GLB + 完整 PBR 材质** | `gltf.damaged_helmet_full_pbr_material` + 图 `gl_model_glb_damaged_helmet.png` | 五个贴图槽（baseColor/MR/normal/AO/emissive）全部解析且**内嵌图片成功解码**（>512px、4 通道）；>10000 三角形 |
| **FBX 导入** | `fbx.binary_cube_geometry_and_material`、`fbx.binary_sphere_mixed_polygons`、`fbx.rejects_garbage_safely` + 图 `gl_model_fbx_{cube,sphere}.png` | 立方体 36 索引、包围盒 ±0.5、材质名 `GenesisRed`、shininess 32→roughness 0.75；球体 219 顶点/1056 索引/法线朝外；垃圾与截断文件不崩溃 |
| **贴图加载/生成** | `image.png_loading_known_pixels`、`image.hdr_equirect_loading`、`image.png_and_tga_roundtrip` | 棋盘 (0,0)=白、(96,32)=灰128；MR 图 G=230(roughness) B=0(metallic)；法线平坦区=(128,128,255)；HDR 太阳峰值>50；PNG/TGA 写读**逐字节相同** |
| **多后端渲染 GL** | 全部 16 个 GPU 用例在 OpenGL 4.5(llvmpipe) 下通过 | 见下 |
| **多后端渲染 Vulkan** | `rhi.device_creation_and_limits`、`clear_colour_readback_is_exact`、`skybox_fills_the_background`、`frustum_culling`、`ibl.*`、`screenshot_file_roundtrip` 等 37/45 通过 | 直接光相关 8 个用例失败，见 `TEST_REPORT.md` 第 4 节（已定位到单个 float 符号） |
| **PBR 直接光：diffuse/specular/fresnel/metallic** | `pbr.directional_light_creates_shading_gradient`、`pbr.metallic_differs_from_dielectric`、`pbr.roughness_blurs_the_highlight`、`pbr.point_and_spot_lights_attenuate` | 亮侧>暗侧且背景不被点亮；金属比非金属整体更暗（金属无漫反射）且两者都有高光；光滑表面高光峰值 > 粗糙表面×1.05；点光近处>远处且暖光 R>1.2B |
| **PBR 直接光：blinnPhong 可切换** | `pbr.blinn_phong_and_cook_torrance_both_work_and_differ` + 图 `gl_pbr_grid_{cook_torrance,blinn_phong}_direct.png` | 两种模型都能出图，**峰值像素差 0.19**（同一场景同一光照） |
| **PBR 间接光：cubemap** | `ibl.irradiance_of_uniform_environment_is_constant`、图 `gl_skybox_ibl_env.png` | equirect→cubemap 6 面烘焙成功；天空盒采样 cubemap 出图（100% 非黑、暖色渐变） |
| **PBR 间接光：diffuseIrradiance** | 同上 | **解析解验证**：常数环境 (0.42,0.21,0.105) 烘焙后球面三点采样 `0.2360 / 0.2360 / 0.2360`（空间恒定），通道比 4.09:2.03:1.00 ≈ 环境的 4:2:1 |
| **PBR 间接光：BRDF LUT** | `ibl.brdf_lut_matches_cpu_reference` | 与**独立 CPU 蒙特卡洛实现**一致：scale 随粗糙度递减、bias 随粗糙度递增、smooth 表面 scale≈1；关闭 IBL 后球体变纯黑（<0.005）证明间接光确实来自这三张贴图 |
| **实时 UI 交互** | `render.screenshot_file_roundtrip`（headless 下 ImGui 初始化成功）+ demo 7 个面板 | ImGui 1.86 双后端对接（GL `#version 410` / Vulkan swapchain render pass），在 present pass 内合成；输入经 `ui_wants_mouse` 路由，拖 UI 不会转动相机 |
| **脚本系统** | `script.lua_vm_and_entity_api`、`script.component_lifecycle_hooks`、`script.api_surface_is_complete` + `assets/scripts/demo_anim.lua` | Lua 脚本改实体位置 (7.5,8.5,9.5) 生效；`start`/`tick` 钩子按帧调用；**42 个 API 全部存在**（脚本内 `type(genesis.x)=='function'` 断言）；两个脚本沙箱互不可见 |
| **跨平台构建** | `./build.sh --clean` 一次通过；CMake 对 macOS 走 `-framework OpenGL/Cocoa/...`、GL 特性严格限制在 4.1 | 77 个编译单元，0 error；着色器 13/13 同时通过 GLSL 410 校验与 SPIR-V 编译 |
| **色调映射/曝光/gamma** | `render.tonemap_and_gamma_change_the_output` + 三张 tonemap 图 | 三种算子给出三个不同 mean（0.265 / 0.252 / 0.295），输出不越界 |
| **视锥剔除** | `render.frustum_culling_removes_draw_calls` | 相机背后的物体被剔除（items 2→1），关闭剔除后恢复 2 |
| **深度/遮挡** | `render.unlit_geometry_has_expected_coverage` | 近处绿色盒子遮挡远处红色盒子：中心像素由红变绿 (26,236,49) |
| **清屏/回读正确性** | `rhi.clear_colour_readback_is_exact` | 空场景 64×64 **逐像素**等于清屏色 |

### 2.2 验证过程中真实发现并修复的缺陷

这些是"跑起来才发现"的问题，说明验证是有效的而不是走过场：

| # | 缺陷 | 定位方式 | 修复 |
|---|------|---------|------|
| 1 | `Mat4()` 与 `Mat4::identity()` 互相递归 → 任何默认构造都栈溢出 | FBX 子任务自测时崩溃 | 构造函数直接 memset + 置对角线 |
| 2 | YXZ 欧拉角反解用了错误的矩阵元素 | `math.look_rotation_and_euler_roundtrip` 失败 | 按 R=Ry·Rx·Rz 推导 M12/M02/M22/M10/M11 重写 |
| 3 | 材质描述符集用了 set index 0（应为 1）→ GL 下材质 UBO 覆盖了 PerFrame | 画面全黑 + 逐 draw 打印实际绑定的 buffer id | `createDescriptorSet(layout, 1)`，并给 RHI 增加 set_index 参数 |
| 4 | GL 后端的 dynamic offset 按"每个 set 一个"消费（应按"每个 dynamic binding 一个"） | 第二个物体渲染到第一个物体的位置 | 在 dynamic 分支内 `dyn_index++` |
| 5 | 程序化 sphere/torus 绕序为 CW → 被背面剔除 | 关闭剔除后立刻出图 | 改为 CCW |
| 6 | 投影矩阵做 Y 翻转 → 同时反转了绕序 | 上面第 5 条的根因之一 | 取消翻转，改为只在 present pass 翻转采样 V |
| 7 | `captureFrame` 没有先跑 `world.update()` → 世界矩阵全是单位阵 | 相机位置打印为 (0,0,0) | capture 前先 update |
| 8 | IBL 烘焙完成后 `sky_mode` 未更新 → 着色器一直走程序化后备 | 均匀环境测试得到偏蓝结果 | `ibl_ready_ ? 2 : sky_mode` |
| 9 | 描述符 stage flag 只写 VS 或只写 FS（Vulkan 下另一 stage 读到 0） | Vulkan 直接光失效 | Material/PerDraw 改为 VS\|FS |
| 10 | **多网格模型的节点变换被忽略**（24 个球叠在原点） | 效果图里只看到 1 个球 | 改用 `ModelData::flattenNodes()`，逐节点 `entity × node` 变换 |

> 第 10 条正是"看效果图"发现的：单元测试用的是单网格模型，全部通过；
> 只有把材质球阵列渲染出来才发现节点变换没生效。这也是本次补充
> `--showcase` 效果图集的直接原因。

---

## 3. 自己复现验证

```bash
# 1) 构建
./build.sh --clean

# 2) headless 图形环境
export DISPLAY=:99 XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
Xvfb :99 -screen 0 1920x1080x24 &
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json

# 3) 测试（L1 + L2）
./build/bin/genesis_tests --no-gpu              # 仅 CPU 用例
./build/bin/genesis_tests --backend gl          # OpenGL 全量（45/45）
./build/bin/genesis_tests --backend vulkan      # Vulkan 全量（37/45，见已知问题）
./build/bin/genesis_tests --verbose             # 打印每一条断言

# 4) 效果图（L3）
./build/bin/genesis_demo --backend gl --showcase docs/images
./build/bin/genesis_demo --backend gl --headless --frames 60 \
        --screenshot docs/images/01_demo_full_scene_gl.png

# 5) 资产可复现性
python3 scripts/gen_assets.py --verify          # 34 项断言，重复运行字节一致
python3 scripts/build_shaders.py --check        # 着色器产物是否过期
```

统计 PNG 数值的小脚本（本文表格数据来源）：

```python
import zlib, struct
def read_png(p):
    d=open(p,'rb').read(); pos=8; idat=b''
    while pos<len(d):
        ln=struct.unpack('>I',d[pos:pos+4])[0]; t=d[pos+4:pos+8]; body=d[pos+8:pos+8+ln]; pos+=12+ln
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',body[:10])
        elif t==b'IDAT': idat+=body
        elif t==b'IEND': break
    raw=zlib.decompress(idat); ch={0:1,2:3,4:2,6:4}[ct]; st=w*ch
    out=bytearray(h*st); prev=bytearray(st); q=0
    for y in range(h):
        f=raw[q]; q+=1; line=bytearray(raw[q:q+st]); q+=st
        for i in range(st):
            a=line[i-ch] if i>=ch else 0; b=prev[i]; c=prev[i-ch] if i>=ch else 0
            if f==1: line[i]=(line[i]+a)&255
            elif f==2: line[i]=(line[i]+b)&255
            elif f==3: line[i]=(line[i]+((a+b)>>1))&255
            elif f==4:
                pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
                line[i]=(line[i]+(a if pa<=pb and pa<=pc else (b if pb<=pc else c)))&255
        out[y*st:(y+1)*st]=line; prev=line
    return w,h,ch,bytes(out)
```

---

## 4. 当前未通过项（如实记录）

Vulkan 后端 8 个用例失败，全部集中在**直接光**：环境光、IBL、天空盒、几何、
深度、剔除、导入、截图均正确。已逐层排除 CPU 数据、UBO 布局、SPIR-V
binding/ArrayStride、描述符 range、写入路径（mapped 与 staging 两种）、
动态索引，最终定位到 `GpuLight::direction_cone.z` 一个 float 的符号与写入值相反。
另有同进程第 5 次创建 `VkDevice` 时 SIGSEGV（单用例运行均正常）。
完整证据链与下一步排查方向见 [`TEST_REPORT.md`](TEST_REPORT.md) 第 4 节，
跟踪任务为 #12。

### 4.1 截图路径的 UI 朝向说明（已知瑕疵）

`--screenshot/--showcase` 走的是**离屏 capture 路径**：该路径把 HDR 结果翻转后
写入"row 0 = top"的离屏目标（见 `docs/ARCHITECTURE.md` 图像朝向一节），而 ImGui
在 GL 下按"NDC +y = 显示顶部"自行构建正交投影，其顶部落在显存最后一行，因此
**截图里的 ImGui 面板是上下颠倒的**；真实窗口呈现（`genesis_demo` 不带
`--screenshot`）时 UI 方向正确。3D 内容在截图中方向正确。
修复方式（后续）：capture 路径改为先把 UI 渲染到一张独立 RT 再在 present pass 里
与 HDR 合成，或为 capture 路径单独翻转 ImGui 的正交投影。跟踪任务并入 #12。
