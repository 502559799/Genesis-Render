# PBR 光照实现说明

着色器源码：`src/genesis/renderer/shaders/common/pbr.h`（BRDF 库）、
`scene/pbr_mesh.frag`（直接光 + 间接光合成）、`ibl/*.frag`（IBL 烘焙）。
C++ 侧常量与 UBO：`src/genesis/renderer/render_ubo.hpp`。

约定：右手系，+Y up，-Z 前；线性工作流（颜色贴图为 sRGB 格式由硬件转线性，
数据贴图为 UNORM）；metallic-roughness 工作流（glTF 一致）。

---

## 1. 直接光

渲染方程的实时近似：

```
Lo = Σ_lights  f_r(p, ωi, ωo) · L_i(ωi) · (n·ωi) dωi
   ≈ Σ_lights  (k_D · albedo/π + k_S · D·F·G / (4·(n·ωo)(n·ωi))) · L_i · (n·ωi)
```

### 1.1 法线分布函数 D（两种模型，运行时可切换）

`u_material_params1.w` 选择模型（`ShadingMode`：0 = Cook-Torrance，1 = Blinn-Phong）。

**Cook-Torrance / GGX（Trowbridge-Reitz）**

```
α  = roughness²
D_GGX(n·h) = α² / (π · ((n·h)²(α² − 1) + 1)²)
```

**Blinn-Phong**（归一化到半球积分为 1，shininess 由感知粗糙度反推）

```
shininess = 2/α² − 2 ,  α = max(roughness², 1e-4)
D_BP(n·h) = (shininess + 2)/(2π) · (n·h)^shininess
```

两者的峰值差异极大（roughness=0.25 时 GGX 峰值约为 Blinn-Phong 的数倍），
测试用例 `pbr.blinn_phong_and_cook_torrance_both_work_and_differ` 以
**峰值像素差 > 0.02** 作为断言，实测 max diff ≈ 0.19。

### 1.2 几何项 G（Smith + Schlick-GGX）

```
k_direct = (roughness + 1)² / 8            # 直接光用 Karis 的 remapping
G₁(v)    = (n·v) / ((n·v)(1 − k) + k)
G_Smith  = G₁(n·ωo) · G₁(n·ωi)
```

### 1.3 菲涅尔 F（Schlick）

```
F_Schlick(cosθ, F0) = F0 + (1 − F0)·(1 − cosθ)⁵
F0 = mix(vec3(0.04), albedo, metallic)     # 金属/非金属统一工作流
```

### 1.4 合成与能量守恒

```
specular = D · G · F / max(4·(n·ωo)·(n·ωi), 1e-4)
k_S = F
k_D = (1 − F) · (1 − metallic)             # 金属没有漫反射
diffuse = k_D · albedo / π
Lo = (diffuse + specular) · radiance · (n·ωi)
```

`metallic` 的作用体现在两处：`F0` 从 0.04 变为 albedo（金属的反射带颜色），
以及 `k_D` 被压到 0（金属无漫反射）。测试
`pbr.metallic_differs_from_dielectric` 断言"同参数下非金属整体更亮"。

### 1.5 光源类型与衰减

| 类型 | L（指向光源） | radiance |
|------|---------------|----------|
| Directional | `−normalize(direction_cone.xyz)` | `color · intensity` |
| Point | `normalize(light_pos − P)` | `color · intensity · A_window(d, range)` |
| Spot | 同 Point | 再乘 `A_cone(cos_inner, cos_outer)` |

```
A_window(d, r) = (1 − (d/r)⁴)² / d²        # Unreal 风格平滑窗口 × 平方反比
A_cone         = saturate((cosθ − cos_outer) / (cos_inner − cos_outer))
```

方向向量做了零长度保护：`if (dot(dir,dir) < 1e-8) dir = (0,−1,0);`
避免 `normalize(vec3(0))` 产生 NaN 污染整帧。

---

## 2. 间接光（IBL，split-sum 近似）

```
Li ≈ k_D · irradiance(n) · albedo  +  prefiltered(R, lod(roughness)) · (F · brdf.x + brdf.y)
k_D = (1 − F_SchlickRough(n·ωo, F0, roughness)) · (1 − metallic)
lod = roughness · (mipCount − 1)
最后整体乘 ao 与 env_intensity
```

三张贴图全部在**运行时用 raster pass 烘焙**（OpenGL 4.1 无 compute，
所以不能用 compute shader；这也让两个后端共用同一套代码）：

```
sky_equirect.hdr (Radiance RGBE, 2048×1024)
   │  ibl/equirect_to_cube.frag        6 次绘制（每面一次），RGBA16F
   ▼
env cubemap 256² × 9 mips             ← generateMipmaps（供 prefilter 的 textureLod 采样）
   │                                   │
   │ ibl/irradiance.frag               │ ibl/prefilter.frag（每 mip 一次，roughness = mip/(n−1)）
   ▼                                   ▼
irradiance cubemap 64²          prefiltered cubemap 256² × 9 mips
                                       │
             ibl/brdf_lut.frag  ──►  BRDF LUT 256² (RG16F)
```

### 2.1 irradiance 卷积

```
E(n) = ∫_Ω L(ω)(n·ω) dω  ，  dω = sinθ dθ dφ
离散：irradiance = π · Σ L(ω)·cosθ·sinθ / N
```

存的是 `E/π`（即"漫反射出射辐射率 / albedo"），因此着色器里直接
`diffuse = k_D · irradiance · albedo`，不需要再除 π。

**解析解验证**：常数环境 `L = (0.42, 0.21, 0.105)` 时，`E(n)/π = L` 与 n 无关。
测试 `ibl.irradiance_of_uniform_environment_is_constant` 实测：

```
球面上三点采样：c1 = c2 = c3 = 0.2360          （空间恒定）
整图通道均值：  R = 0.1739  G = 0.0864  B = 0.0425  →  4.09 : 2.03 : 1.00
```

与环境光 4 : 2 : 1 的比例一致，证明卷积的立体角权重与归一化都正确。

### 2.2 prefiltered specular（GGX 重要性采样）

```
xi = Hammersley(i, N)                       # (i/N, radicalInverseVdc(i))
H  = importanceSampleGGX(xi, n, roughness)  # cosθ = √((1−xi.y)/(1+(α²−1)xi.y))
L  = 2(V·H)H − V
prefiltered = Σ L(ω)·(n·ωi) / Σ (n·ωi)      # 只保留 n·ωi > 0 的样本
```

采样源 cubemap 时用 `textureLod(src, L, mipBias)` 降低方差：

```
pdf       = D_GGX(n·h)·(n·h) / (4·(v·h))
sa_texel  = 4π / (6·256²)
sa_sample = 1 / (N · pdf)
mipBias   = 0.5 · log2(sa_sample / sa_texel)
```

### 2.3 BRDF 积分 LUT

对 GGX 重要性采样，`D` 与 `1/(4·(n·ωo)(n·ωi))` 相互抵消，估计量退化为
`G_Vis` 的均值（Karis / LearnOpenGL 约定，注意 IBL 的 `k = roughness²/2`
与直接光不同）：

```
G_Vis = G_Smith_IBL · (v·h) / ((n·h)(n·ωo))
Fc    = (1 − v·h)⁵
A = Σ (1 − Fc)·G_Vis / N        →  LUT.r（scale）
B = Σ      Fc ·G_Vis / N        →  LUT.g（bias）
最终：F ≈ F0 · A + B
```

LUT 的 x 轴 = `n·ωo`，y 轴 = roughness。
测试 `ibl.brdf_lut_matches_cpu_reference` 用**独立的 CPU 蒙特卡洛实现**
（`tests/test_gpu.cpp` 中的 `integrateBRDF`，与着色器逐行对应）断言：
scale 随粗糙度递减、bias 随粗糙度递增、smooth 表面 scale ≈ 1，
并与解析近似 `gsEnvBRDFApprox`（Karlis/Frostbite 拟合，作为无 LUT 时的
运行时后备）交叉比对。

### 2.4 未烘焙时的后备

`u_ibl_params.z`（sky mode）< 2 时，着色器改用解析的程序化天空
（`u_sky_tint` / `u_ground_tint` 梯度 + 太阳圆盘）配合 `gsEnvBRDFApprox`，
保证资产加载期间画面不会全黑。`u_ibl_params.x = 0`（关闭 IBL）时
间接光完全不参与——测试用这一行为断言"关掉 IBL 与全部光源后球体变纯黑"。

---

## 3. 后处理

`post/tonemap.frag`（全屏 pass，在 present render pass 内、UI 之前执行）：

```
color *= exposure
mode 0:  color = saturate(color)                 # 仅裁剪
mode 1:  color = color / (color + 1)             # Reinhard
mode 2:  color = ACES(color)                     # Narkowicz 拟合
color   = mix(luma, color, saturation)
color  *= vignette(uv)
color   = pow(color, 1/gamma)
```

ACES：`(x(2.51x+0.03)) / (x(2.43x+0.59)+0.14)`。

场景渲染到 RGBA16F 的 HDR target（不可用时自动回退 RGBA32F → RGBA8），
再 tone map 到呈现目标；采样 V 方向的翻转只发生在这一个 pass
（详见 `docs/ARCHITECTURE.md` 的"图像朝向约定"一节）。
