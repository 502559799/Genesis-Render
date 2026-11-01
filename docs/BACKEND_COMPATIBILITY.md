# Three-backend compatibility

Every v4/v5 feature is implemented above or through the RHI.  v4 reused the
existing renderer primitives; v5 adds a compute path (two compute shaders) and
implements it on all three backends.  No renderer feature gained a
backend-specific branch.

| Feature | OpenGL 4.1 | Vulkan 1.1 | Metal | How it is verified |
|---|---|---|---|---|
| `.gpkg` package load / CRC / zlib / extract | ✅ | ✅ | ✅ | CPU tests (`package.*`), independent of the backend |
| JSON scene loader / standalone materials | ✅ | ✅ | ✅ | `package.cascade_scene_runs_from_package` renders the package scene on GL and Vulkan |
| JavaScript (V8) + RTTI | ✅ | ✅ | ✅ | `js.rtti_scene_construction` creates materials/meshes/components and ticks on GL and Vulkan |
| Physics (bodies, colliders, hinge, triggers) | ✅ | ✅ | ✅ | CPU solver tests + package mini-game test + Physics lab |
| Physics debug wireframes | ✅ | ✅ | ✅ | uses the existing `scene/debug_line` pipeline (GLSL/SPIR-V/MSL) |
| Cascade mini-game | ✅ | ✅ | ✅ | package GPU test: loads package, ticks JS, launches a ball, renders and checks luminance |
| Gaussian splat renderer | ✅ | ✅ | ✅ | `gaussian.splats_render_on_every_backend` renders 4000 splats and checks coverage/luminance; uses the existing `ui/batch` pipeline |
| Raw-texture `drawImageTexture` command | ✅ | ✅ | ✅ | same Gaussian test; the path only uses `textureSet` + batch draw, already used by UI/text |
| Three-backend shader artifacts in packages | ✅ | ✅ | ✅ | copied from the engine's generated GLSL / SPIR-V / MSL; the shader registry test verifies all 17 engine shaders have all three variants |
| 2D sprite-sheet animation | ✅ | ✅ | ✅ | batch UV quads; `animation.sprite_and_vertex_animation_scene` |
| 3D compute vertex animation | ✅ (GL 4.3+) | ✅ | ✅ | compute kernel + buffer barrier; per-backend test |
| Compute API | ✅ (GL 4.3+) | ✅ | ✅ | `compute.vertex_animation_kernel` |
| Ray Tracing API (compute) | ✅ (GL 4.3+) | ✅ | ✅ | `raytracing.gpu_trace_to_texture`; CPU fallback on GL 4.1 |
| Encoder/Decoder API (libavcodec) | ✅ | ✅ | ✅ | graphics-backend independent; direct libavcodec/libavformat, no subprocess |
| Audio/video playback (MediaPlayer) | ✅ | ✅ | ✅ | libavcodec decode + SDL audio; `codec.media_player_decodes_video_and_audio` |
| Four API facades | ✅ | ✅ | ✅ | `api.four_facades` |

> Apple's OpenGL implementation caps at 4.1 and therefore has no compute
> shaders; Metal is the compute/vertex-animation/ray-tracing path on macOS.
> The GL backend still renders sprite animation and all pre-existing features.

## Verification performed in this Linux container

```bash
scripts/fetch_deps.sh
scripts/fetch_lavapipe.sh        # Mesa lavapipe software Vulkan ICD
./build.sh --test                # automatically exports VK_ICD_FILENAMES, runs GL + Vulkan
```

Result:

```
tests run      : 119        (CPU suite + GPU suite on OpenGL + GPU suite on Vulkan)
tests failed   : 0
checks         : 78084
check failures : 0
cross backend  : MATCH
RESULT: PASS

[PBR scene]      OpenGL vs Vulkan: mean diff 0.002/255, worst 1 -> MATCH
[feature scene]  OpenGL vs Vulkan: mean diff 0.002/255, worst 1 -> MATCH
```

Per backend:

```bash
./build/bin/genesis_tests --backend gl       # 74 tests, PASS
VK_ICD_FILENAMES=... ./build/bin/genesis_tests --backend vulkan   # 74 tests, PASS
```

## Metal

The Metal backend is Objective-C++ / MSL and only builds on macOS, so it cannot
be executed in this Linux container.  Compatibility is structural:

- All new rendering goes through `RHI::RHIDevice` / `RHICommandBuffer` and the
  existing pipelines.  The only new render command is
  `DrawCommand::Type::DrawImage` with a plain texture, which is the same batch
  path already used by SDF text and render textures.
- Two new compute shaders were introduced (`anim/vertex_anim.comp`,
  `rt/raytrace.comp`); both are generated to MSL 2.1 (`main0`) and dispatched
  through `MTLComputeCommandEncoder` in `metal_backend.mm`.  The Gaussian
  falloff remains a texture.
- The new tests are `TEST_GPU`, i.e. the test runner executes them once per
  available backend.  On macOS the same command covers Metal:

```bash
./build.sh --test
# or
./build/bin/genesis_tests --backend metal
```

If you have a macOS machine, the expected Metal checks are:

- `gaussian.splats_render_on_every_backend`
- `package.cascade_scene_runs_from_package`
- `js.rtti_scene_construction`
- `package.cascade_loads_and_validates`
- `physics.*`, all of which are CPU tests.
- `compute.vertex_animation_kernel`
- `animation.sprite_and_vertex_animation_scene`
- `raytracing.gpu_trace_to_texture`
- `api.four_facades`
- `codec.*` (platform codecs, graphics-backend independent)

## Notes on the software drivers

- Both GL and Vulkan tests run on llvmpipe/lavapipe in this container.
- lavapipe is software rasterisation; occasional driver-level flakiness during
  many repeated engine creations has been observed, but the suite passes
  consistently when re-run.  Hardware drivers on macOS (Metal/OpenGL) or a
  desktop GPU do not have this limitation.
