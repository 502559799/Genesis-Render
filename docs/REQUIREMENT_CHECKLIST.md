# v4 / v5 requirement checklist

This page maps every requested subtask to its implementation, demo entry point,
automated tests and documentation.

## First round (v4)

| Requirement | Implementation | Demo / entry | Tests | Docs |
|---|---|---|---|---|
| Read code/docs, build and run, fix bugs | Baseline clean build; OpenGL/Vulkan verified; fixed compute-era build/runtime issues | `./build.sh`, `genesis_tests`, `--selftest` | 119 tests, cross-backend MATCH | `docs/TEST_REPORT.md`, `docs/VERIFICATION.md` |
| Runtime effect asset package with scene/material/3-backend shaders/textures/scripts/config | `AssetPackage`, `AssetPackage` loader/extractor, JSON `SceneLoader`, packer | `--scene package --package FILE`, Asset package browser | `package.*` | `docs/ASSET_PACKAGES.md` |
| Offline asset authoring | `tools/genesis_pack.py` build/validate/list/extract; `assets/packages/*_src` | `scripts/build_demo_packages.sh` | packer validation + package tests | `docs/ASSET_PACKAGES.md` |
| JavaScript V8 parity with Lua + RTTI + scene building | `JSSystem`, generic RTTI binding, constructors, globals, `genesis.*` facade, Lua fallback | Lua / JS console, `--script file.js` | `js.rtti_scene_construction`, `package.cascade_scene_runs_from_package` | `docs/JS_SCRIPTING.md` |
| Physics: rigid bodies, collision detection, collider shapes, hinge; dedicated subpage | `PhysicsSystem`, `RigidBody`, `Collider`, `HingeJoint`, raycast, debug draw | `--scene physics --lab physics` | `physics.*` | `docs/PHYSICS.md` |
| Mini-game using multiple engine capabilities, packaged assets, physics | Cascade package + JS/Lua gameplay + HUD + scoring + restart | `--scene game`, Cascade game panel | `package.cascade_scene_runs_from_package` | `docs/CASCADE_GAME.md`, `docs/ASSET_PACKAGES.md` |
| 3D Gaussian splatting, dedicated page, limitations doc | PLY 3DGS parser, depth sort, screen-space Gaussian splats | `--scene gaussian --lab gaussian` | `gaussian.*` | `docs/GAUSSIAN_SPLATTING.md` |
| Self-test and README/architecture update | `--selftest` covers all scenes/panels; docs updated | `./build/bin/genesis_demo --selftest` | 0 errors | `README.md`, `ARCHITECTURE.md`, `架构设计文档.md` |

## Second round (v5)

| Requirement | Implementation | Demo / entry | Tests | Docs |
|---|---|---|---|---|
| 2D sprite-sheet sequence animation | `SpriteAnimationComponent`, `drawSprite` UV quads | Animation lab | `animation.sprite_and_vertex_animation_scene` | `docs/ANIMATION.md` |
| 3D vertex animation with compute shader | `VertexAnimationComponent`, compute-written vertex buffer, barriers | Animation lab | `compute.vertex_animation_kernel`, `animation.*` | `docs/ANIMATION.md` |
| Three backend implementations | GL 4.3+ SSBO/dispatch; Vulkan compute pipeline + barriers; Metal compute encoder + MSL | per backend `TEST_GPU` | GL + Vulkan executed; Metal code path implemented (macOS only) | `docs/ANIMATION.md`, `docs/BACKEND_COMPATIBILITY.md` |
| Refactor into 4 API types | `API::RenderingAPI`, `ComputeAPI`, `RayTracingAPI`, `EncoderDecoderAPI`, `Engine::apis()` | Engine APIs (4) panel | `api.four_facades`, all legacy tests | `docs/FOUR_APIS.md` |
| Rendering API | RHI/RenderSystem facade | existing scenes | all render tests | `docs/FOUR_APIS.md` |
| Compute API | storage buffers, kernels, dispatch, barriers, readback | Animation + RT pages | `compute.vertex_animation_kernel` | `docs/FOUR_APIS.md` |
| Ray Tracing API | GPU compute ray tracer, shadows/reflection, CPU fallback, hardware-RT extension point | Ray tracing lab | `raytracing.gpu_trace_to_texture` | `docs/RAY_TRACING.md` |
| Encoder & Decoder API | Raw/PNG in-process; H.264 + demux/mux through directly linked libavcodec/libavformat; hardware encoder detection + software fallback; **MediaPlayer** video + SDL audio playback | Encoder / Decoder lab, Media player | `codec.*`, `codec.media_player_decodes_video_and_audio` | `docs/ENCODER_DECODER.md` |
| Preserve old functionality | Existing code paths untouched; refactor is a facade layer | all scenes | 119 tests, cross-backend MATCH | `docs/TEST_REPORT.md` |

## Verification summary

```text
tests run      : 119
tests failed   : 0
checks         : 78084
cross backend  : MATCH
RESULT: PASS

./build/bin/genesis_demo --backend gl --headless --selftest
[selftest] done: 0 error(s)
```

## Honest platform limitations

- **Metal** cannot be compiled or run in this Linux container.  The Metal
  compute path and MSL kernel are implemented; the same backend-parameterised
  tests execute it on macOS.
- **Hardware ray tracing** (RT cores) is not enabled; the Ray Tracing API uses
  the portable GPU compute tracer, with documented `VK_KHR_ray_tracing` /
  Metal RT extension points.
- **Hardware video codecs** require a device (NVENC/VA-API/QSV/VideoToolbox).
  This container has none, so H.264 runs through libx264; the API reports the
  hardware entries as unavailable instead of claiming acceleration.
- Apple OpenGL caps at 4.1 and has no compute shaders; Metal covers compute,
  vertex animation and ray tracing on macOS.
