# Animation System

The animation system (order 92) supports two independent animation types:

| Type | Data path | Backend coverage |
|---|---|---|
| 2D sprite-sheet sequence frames | textured UV-rect quads through the existing `ui/batch` pipeline | OpenGL / Vulkan / Metal |
| 3D vertex animation | compute shader writes a vertex stream, mesh pass draws the animated buffer | Vulkan / Metal / OpenGL 4.3+ |

## 1. Sprite sequence animation

`ECS::SpriteAnimationComponent`:

| Field | Meaning |
|---|---|
| `texture` | sprite-sheet path (PNG/JPG) |
| `columns`, `rows`, `frame_count` | sheet grid and number of active frames |
| `fps`, `loop`, `playing` | playback |
| `screen_space` | screen position (pixels) or project the entity transform |
| `size` | pixel size / world size |
| `screen_position` | used when `screen_space == true` |
| `tint`, `depth_test` | colour and depth behaviour |
| `frame`, `time` | runtime state written by the system |

The system advances the frame with a fixed `fps` and emits a quad with the
current UV rect (`drawSprite` command).  The quad goes through the existing
batch shader, so it is identical on all backends and works with the same
texture cache as materials.

The demo scene `--scene animation` uses `assets/textures/sprite_sheet.png`
(4x2 frames) in both screen space and projected world space.

## 2. 3D compute vertex animation

`ECS::VertexAnimationComponent`:

| Field | Meaning |
|---|---|
| `shader` | compute shader name (`anim/vertex_anim.comp`) |
| `amplitude`, `frequency`, `speed` | deformation parameters |
| `enabled` | toggle |

Per animated entity the system:

1. Locates the `GpuMesh` and creates a second vertex buffer with
   `Vertex | Storage` usage (the rest-pose buffer also has `Storage`).
2. Creates a params storage buffer and a compute kernel with three bindings:
   `BaseVertexBuffer`, `AnimatedVertexBuffer`, `AnimParams`.
3. Each frame writes the params (time, amplitude, frequency, speed, vertex
   count) and records a dispatch into the frame command buffer.
4. Records a `ShaderWrite -> VertexAttributeRead` buffer barrier.
5. The mesh renderer binds the animated buffer instead of the rest pose.

The shader (`src/genesis/renderer/shaders/anim/vertex_anim.comp`) displaces
position and normal with a travelling sine wave.  The same single source is
built into GLSL 4.30 / SPIR-V / MSL 2.1 by `scripts/build_shaders.py`.

### Backend notes

- **OpenGL**: compute requires GL 4.3.  The Linux backend runs on a 4.5 context;
  macOS caps OpenGL at 4.1, so GL compute reports unavailable there and Metal
  is the Apple compute path.
- **Vulkan**: compute pipeline, storage descriptors and buffer barriers are
  native.
- **Metal**: `MTLComputeCommandEncoder` + `MTLComputePipelineState`, MSL kernel
  `main0`, buffers at slots 0/1/2; Metal's automatic hazard tracking covers the
  compute -> vertex transition.

## 3. Demo and tests

```bash
./build/bin/genesis_demo --scene animation --lab animation
```

Controls: play/pause, fps, previous/next frame, compute enable, amplitude,
frequency, speed, live dispatch/vertex-animation counters.

Tests (run per backend):

- `compute.vertex_animation_kernel` - dispatches the compute shader on a tiny
  vertex buffer and verifies the exact displaced floats.
- `animation.sprite_and_vertex_animation_scene` - renders a sprite sheet and a
  compute-animated mesh, checks the system counters and the captured frame.

The sprites and the compute dispatch are both verified on OpenGL and Vulkan in
this environment; the same `TEST_GPU` cases run on Metal on macOS.

## 4. Limitations

- Sprite animation is a 2D sheet animation; there is no skeletal animation.
- The vertex shader is a procedural deformation shader; morph-target / skinning
  buffers can replace the params/inputs without changing the system.
- Vertex animation computes into a full second vertex buffer (no in-place
  ping-pong); memory cost is one extra vertex stream per animated mesh.
