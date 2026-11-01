# Four Public APIs

The engine facade now exposes four distinct API domains.  Existing code paths
(`Engine::render()`, `RenderSystem`, `world()`, Lua/JS, demo scenes and all
prior tests) remain intact; the facades delegate to them, so the refactor is
behaviour preserving.

```
Engine
 ├─ API::RenderingAPI       cross-platform GPU rendering
 ├─ API::ComputeAPI         compute shaders / storage buffers
 ├─ API::RayTracingAPI      portable GPU ray tracing
 └─ API::EncoderDecoderAPI  hardware / software media codecs
```

`Engine::apis()` returns all four; individual accessors are
`engine.rendering()`, `engine.compute()`, `engine.rayTracing()` and
`engine.encoderDecoder()`.

## 1. Rendering API

`src/genesis/api/rendering_api.hpp`

- Wraps `RHIDevice` + `RenderSystem` (the implementation layer).
- Exposes backend, width/height, `RenderSettings`, `RenderScene`, `RenderStats`,
  camera, `createRenderTexture`, `waitIdle`.
- The existing per-draw APIs (materials, ECS, CommandBuffer) are unchanged and
  are used through this facade or directly for compatibility.

## 2. Compute API

`src/genesis/api/compute_api.hpp`

- Storage buffer creation / writes / readback.
- Kernels loaded from the baked shader registry; the backend-specific payload
  (GLSL 4.30, SPIR-V, MSL) is selected automatically.
- Descriptor binding, dispatch into the frame command buffer or one-shot
  execution, and explicit `ShaderWrite -> VertexAttributeRead/HostRead`
  barriers.
- `supportsHardwareCompute()` reports the backend capability.

Backend implementation:

| Backend | Compute implementation |
|---|---|
| OpenGL 4.3+ | SSBO + `glDispatchCompute` + `glMemoryBarrier`; storage blocks mapped by name at pipeline build |
| Vulkan 1.1 | compute pipeline, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `vkCmdDispatch`, buffer memory barriers |
| Metal | `MTLComputePipelineState`, compute command encoder, `dispatchThreadgroups`, automatic hazards |

## 3. Ray Tracing API

`src/genesis/api/ray_tracing_api.hpp`

- `setScene(spheres)`, `trace(camera, settings) -> RGBA8 texture`.
- Default path: a GPU compute ray tracer (`rt/raytrace.comp`): brute-force
  sphere/plane intersections, hard shadows and one reflection bounce.
- `hardwareRayTracing()` reports whether a native ray-tracing path is active.
  The current implementation is compute based; `VK_KHR_ray_tracing` and Metal
  ray tracing can replace the backend behind the same API.
- When compute is unavailable (macOS OpenGL 4.1) a CPU tracer with the same
  shading model is used so the page and tests remain functional.

## 4. Encoder & Decoder API

`src/genesis/api/encoder_decoder_api.hpp`

- Raw (zlib) and PNG frame codecs implemented in-process.
- H.264 sequence encode/decode through **libavcodec/libavformat linked directly
  into the engine**, with hardware encoder selection: `h264_nvenc`,
  `h264_vaapi`, `h264_videotoolbox`; libx264 is the software fallback.  No
  subprocess is spawned.
- `createPlayer()` returns an in-process `Media::MediaPlayer`: libavformat
  demuxing, libavcodec video/audio decoding, libswscale RGBA conversion,
  libswresample 48 kHz S16 stereo, SDL2 audio output.
- `codecs()` reports every detected codec, whether it is hardware, and whether
  it is usable on this machine (e.g. VA-API is built but unusable without
  `/dev/dri`).
- `stats()` reports encode/decode time and the backend actually used.

In this headless container the libav* libraries are linked and `libx264` is
available, but there is no hardware codec device, so the hardware entries are
reported as unavailable.  The hardware path is selected automatically on
machines where the device exists.

## 5. Verification

`tests/test_new_features.cpp` covers:

| Test | What it proves |
|---|---|
| `compute.vertex_animation_kernel` | compute storage buffers, dispatch, readback |
| `animation.sprite_and_vertex_animation_scene` | sprite quads + compute animation in one frame |
| `raytracing.gpu_trace_to_texture` | GPU trace produces a non-empty image and texture |
| `api.four_facades` | all four APIs exist and the old render path still works |
| `codec.raw_and_png_roundtrip` | exact lossless roundtrip |
| `codec.h264_hardware_or_software_roundtrip` | real H.264 encode/decode (libavcodec, in-process) |
| `codec.media_player_decodes_video_and_audio` | H.264 + AAC playback metadata, video frames, audio PCM, seek |

Full suite in this environment (OpenGL + Vulkan):

```
tests run      : 119
tests failed   : 0
cross backend  : MATCH
RESULT: PASS
```

All pre-existing tests continue to pass and the cross-backend reference scenes
still match pixel for pixel.
