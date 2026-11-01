# Ray Tracing API

`Genesis::API::RayTracingAPI` provides a backend-independent GPU ray tracer.

## 1. GPU path

The default implementation is a compute ray tracer (`rt/raytrace.comp`):

- one 8x8 workgroup dispatch per tile,
- sphere and ground-plane intersections,
- hard shadow rays,
- one reflection bounce for metallic spheres,
- sky gradient + checker ground.

The shader is compiled from one GLSL source into:

| Backend | Artifact |
|---|---|
| OpenGL 4.3+ | GLSL 4.50 compute source |
| Vulkan 1.1 | SPIR-V |
| Metal | MSL 2.1 kernel `main0` |

The tracer writes packed RGBA8 pixels into a storage buffer; the API reads the
buffer back, uploads it into an RGBA8 texture and exposes `outputTexture()`.
The demo draws that texture with the existing batch path.

## 2. API

```cpp
API::RayTracingAPI &rt = engine.rayTracing();
rt.setScene({{Math::Vec3(0,1,0), 0.8f, Math::Color(0.2f,0.8f,1.0f), 1.0f}});

API::RTCamera camera;
camera.eye = Math::Vec3(0,2,6);
camera.forward = Math::normalize(-camera.eye);
camera.right = Math::normalize(Math::cross(camera.forward, Math::Vec3(0,1,0)));
camera.up = Math::cross(camera.right, camera.forward);
camera.fov_degrees = 50.0f;
camera.aspect = 16.0f / 9.0f;

API::RTSettings settings;
settings.width = 320; settings.height = 180; settings.samples = 2;
rt.trace(camera, settings);
// rt.outputTexture(), rt.stats().gpu, rt.stats().trace_ms
```

## 3. Native hardware ray tracing

`hardwareRayTracing()` reports whether a native path is active.  This iteration
uses the compute path on all three backends, which is a real GPU ray tracer but
does not use dedicated RT cores.  The extension points are:

- **Vulkan**: add `VK_KHR_ray_query` (or `VK_KHR_ray_tracing_pipeline`) behind
  the same `trace()` entry point.
- **Metal**: use `MTLAccelerationStructure` / `intersection_query` where
  supported.

No changes to the scene/camera API are required for those paths.

## 4. CPU fallback

On backends without compute (macOS OpenGL 4.1) the API runs an equivalent CPU
tracer with the same shading model, so the page, demo and tests still work.

## 5. Demo and tests

```bash
./build/bin/genesis_demo --scene raytracing --lab raytracing
```

- auto trace / trace now, resolution, samples,
- add/reset spheres,
- live GPU/CPU path, backend, timing and sphere count.

Tests:

- `raytracing.gpu_trace_to_texture` (per backend) checks that the GPU path is
  used, the output buffer is populated and the texture exists.
- The compute kernel itself is also covered by
  `compute.vertex_animation_kernel`.
