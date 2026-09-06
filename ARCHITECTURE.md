# Genesis Render 架构总览

> 完整架构说明（含 Mermaid 类图/时序图、RHI 对照表、IBL/Lua/UI 细节）见
> **[docs/ARCHITECTURE_FULL.md](docs/ARCHITECTURE_FULL.md)**。

Genesis Render 是一个对齐 Piccolo 能力的小型 3D 引擎，在原 Vulkan 相机滤镜 Demo
基础上重构，新增双图形后端、ECS、PBR/IBL、多格式模型导入与 Lua 脚本。

```
Application (demo/pbr_demo)
  ├─ ui (Dear ImGui 实时编辑：层级 / 检查器 / 渲染设置)
  ├─ script (Lua 5.4 + sol2，每实体沙箱 environment，onStart/onUpdate)
  ├─ render (前向 PBR：方向光/点光 · GGX / Blinn-Phong · 运行时 IBL 烘焙)
  ├─ ecs (Entity(generation) + ComponentPool<T> + System + Transform 层级)
  ├─ resource (Assimp: glTF2/glb·FBX·OBJ；stb 贴图；PBR 材质；程序化网格)
  ├─ rhi (IRHIDevice)
  │    ├─ OpenGL 4.1 core backend (glad，GLSL 运行期从 Vulkan 风格源码转换)
  │    └─ Vulkan backend (SPIR-V，swapchain/描述符/PSO/离屏 RT)
  └─ application / foundation (SDL2 窗口 · log · TLSF 内存 · array)
```

## 渲染一帧

1. `IRHIDevice::beginFrame()`
2. `Scene::update(dt)`：Transform 世界矩阵 → Lua `onUpdate`
3. `Renderer::renderScene()`：写 Camera/Lighting UBO → 天空盒 → PBR 网格
   （结果到离屏 sceneColor/sceneDepth）
4. `beginSwapchainPass()`：全屏 blit sceneColor → ImGui
5. `endFrame()`：GL SwapBuffers / VK queue submit+present

## 环境光照（运行时烘焙，两后端同一套 pass）

equirect HDR → environment cubemap(256) → irradiance(64, 余弦卷积)
→ prefiltered env(128, GGX 8 mip) + BRDF integration LUT(256², split-sum)。
无 HDR 时使用程序化天空。
