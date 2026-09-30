# Genesis Render - architecture overview

The complete architecture document (class relations, frame sequence, backend
conventions, material / CommandBuffer / RTTI / text design, fixes) is
[`架构设计文档.md`](架构设计文档.md). This page is a short English summary.

## Layers

| Layer | Contents |
|---|---|
| RHI | One abstract device / command buffer interface with three native backends: OpenGL 4.1 core, Vulkan 1.1 (MoltenVK on macOS), Metal (Objective-C++, ARC). |
| Shaders | Single-source GLSL compiled offline into GLSL 410, SPIR-V and MSL 2.1 (`scripts/build_shaders.py`, glslang + SPIRV-Cross). |
| Renderer | `RenderSystem` (ECS sync, IBL baking), `FrameExecutor` (forward or custom pipeline, command buffer interpreter, 2D/3D batching), `FrameAllocator` (per-frame dynamic UBO and vertex rings), `PipelineCache`, `RenderTexture`, `Material` library. |
| CommandBuffer | Recorded command lists (render targets, cameras, meshes with any material and matrix, scene, skybox, blits with 12 effects, 2D / 3D primitives, SDF text). They can be injected at six camera events, executed once, used as per-frame immediates, or build the whole frame (`PipelineMode::Custom`). |
| Text | SDF font atlas (distance transform), 2D and 3D text, `TextComponent` labels, outlines. |
| Reflection | `Reflect::Value / ObjectRef / TypeInfo / Registry`. 19 engine types are registered (engine, world, entity, components, material, render texture, mesh asset, command buffer, renderer, settings, input). |
| Script | Lua 5.4. A generic binding over the reflection registry lets scripts build complete scenes and render pipelines. The legacy `genesis.*` API is kept for compatibility. |
| UI | ImGui rendered through the RHI on all backends, plus a reflection-driven inspector. |

## Backend conventions

- Offscreen targets always receive NDC y = -1 in row 0. Metal shaders use `--flip-vert-y`, and Vulkan and Metal map the front face CCW to clockwise, so all three rasterise identically.
- `TargetRef::row0_top` marks display targets (swapchain, capture). Only presentation, readback, 2D and UI look at it.
- `DeviceInfo` exposes the depth clip range, row order, depth format (probed) and frames in flight. The renderer contains no backend branches.
- Every per-frame uniform is a dynamic UBO slice, so descriptor sets never change while the GPU may be using them.

## Frame

`Engine::tick`: input, then the UI frame, `device.beginFrame`, `world.update` (Script 50, CameraController 90, Transform 100, RenderSystem 200 sync), `renderFrame` (forward or custom pipeline with its command buffer events, then the UI) and `device.endFrame`.

## Verification

`./build.sh --test [--validation]` runs 121 tests (34 CPU tests plus 29 GPU tests on each of the three backends). It also runs two cross-backend scenes that match pixel for pixel, with zero messages from the Vulkan validation layer, GL error checks and Metal API / shader validation. See `docs/TEST_REPORT.md` and `docs/VERIFICATION.md`.
