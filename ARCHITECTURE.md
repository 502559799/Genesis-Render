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
| Reflection | `Reflect::Value / ObjectRef / TypeInfo / Registry`. 25 engine types are registered (engine, world, entity, components, material, render texture, mesh asset, command buffer, renderer, settings, input, physics, animation). |
| Script | Lua 5.4 **and JavaScript (V8)**. Both bindings are generic over the reflection registry, so scripts build complete scenes and render pipelines; each language has `start` / `tick` hooks and sandboxed globals. The legacy `genesis.*` API is kept for compatibility. |
| Physics | `PhysicsSystem` (order 95): rigid bodies, sphere/box/plane/capsule colliders, sequential-impulse contacts, friction/restitution/sleeping, hinge joints (limits/motor), triggers, ray casts. |
| Assets / Scene | JSON scene loader (`scenes/*.scene.json`, standalone materials, components, physics, scripts) and runtime `.gpkg` packages with zlib/CRC, offline packer and three-backend shader artifacts. |
| Gaussian | PLY 3DGS parser + CPU depth sort + screen-space alpha splats, isolated in `renderer/gaussian_splat.*` and a dedicated demo page. |
| Animation | `AnimationSystem` (order 92): 2D sprite-sheet sequence frames (batch quads) and 3D vertex animation through a compute shader (GL 4.3+/Vulkan/Metal). |
| APIs | Four public facades: `API::RenderingAPI`, `API::ComputeAPI`, `API::RayTracingAPI`, `API::EncoderDecoderAPI`; the legacy `render()`/`world()` paths remain compatibility aliases. |
| Media | `EncoderDecoderAPI` links libavcodec/libavformat/libswscale/libswresample directly (no subprocess) for H.264 encode/decode and `MediaPlayer` audio/video playback with SDL audio output. |
| UI | ImGui rendered through the RHI on all backends, plus a reflection-driven inspector. |

## Backend conventions

- Offscreen targets always receive NDC y = -1 in row 0. Metal shaders use `--flip-vert-y`, and Vulkan and Metal map the front face CCW to clockwise, so all three rasterise identically.
- `TargetRef::row0_top` marks display targets (swapchain, capture). Only presentation, readback, 2D and UI look at it.
- `DeviceInfo` exposes the depth clip range, row order, depth format (probed) and frames in flight. The renderer contains no backend branches.
- Every per-frame uniform is a dynamic UBO slice, so descriptor sets never change while the GPU may be using them.

## Frame

`Engine::tick`: input, then the UI frame, `device.beginFrame`, `world.update`
(Lua 50, JavaScript 51, CameraController 90, Animation 92, Physics 95,
Transform 100, RenderSystem 200 sync), `renderFrame` (forward or custom pipeline with its
command buffer events, then the UI) and `device.endFrame`.

A frame can therefore mix C++ systems, Lua, V8, physics and recorded command
buffers; scripted logic runs before the solver, and the solver writes transforms
before the renderer syncs them.

## Verification

`./build.sh --test [--validation]` runs the CPU and GPU suites on every
available backend, including the new package, V8, physics and gaussian tests.
In this container OpenGL (llvmpipe) and Vulkan (lavapipe) both pass and the
cross-backend reference scenes match; Metal runs the same backend-parameterised
tests on macOS (`docs/BACKEND_COMPATIBILITY.md`).
It also renders cross-backend reference scenes that must match pixel for pixel,
with zero validation messages.  See `docs/TEST_REPORT.md`,
`docs/VERIFICATION.md`, `docs/PHYSICS.md`,
`docs/ASSET_PACKAGES.md`, `docs/JS_SCRIPTING.md` and
`docs/GAUSSIAN_SPLATTING.md`.
