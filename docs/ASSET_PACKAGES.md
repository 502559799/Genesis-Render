# Runtime Asset Packages (`.gpkg`)

Genesis Render v4 can ship a complete effect or game as a **single file** and
mount it while the engine is running.  The package contains everything the
scene needs, including source and compiled shaders for all three backends.

## 1. Source layout

```
my_effect_src/
  package.json                  package metadata + scene entry point
  scenes/my_effect.scene.json   ECS / render / physics scene description
  materials/*.material.json     standalone material definitions
  shaders/
    shader_manifest.json        GLSL <-> SPIR-V <-> MSL mapping
    scene/pbr_mesh.vert         GLSL 4.10 source
    scene/pbr_mesh.frag
    generated/pbr_mesh.vert.spv SPIR-V (Vulkan)
    generated/pbr_mesh.frag.spv
    generated/pbr_mesh.vert.metal MSL 2.1 (Metal)
    generated/pbr_mesh.frag.metal
  textures/*.png|jpg|hdr
  scripts/*.js|*.lua
  config/*.json                 gameplay / render configuration
```

The offline tool validates that **all** of these categories are present:

```bash
python3 tools/genesis_pack.py validate assets/packages/cascade_src
python3 tools/genesis_pack.py build    assets/packages/cascade_src
python3 tools/genesis_pack.py list     assets/packages/cascade.gpkg
python3 tools/genesis_pack.py extract  assets/packages/cascade.gpkg /tmp/cascade
```

`build` writes `assets/packages/cascade.gpkg` (the output name is derived from
the `_src` suffix).  `scripts/build_demo_packages.sh` regenerates the gaussian
sample and both demo packages.

## 2. Binary format

Little endian, mirrored by `src/genesis/resource/asset_package.cpp` and
`tools/genesis_pack.py`:

```
char   magic[8]        "GENPKG01"
u32    version         1
u32    file_count
u32    manifest_size
char   manifest[]      generated JSON (package.json + file table)
per file:
  u32  path_size
  char path[]          relative, '/' separated, no ".."
  u32  data_offset     relative to the data blob
  u32  stored_size
  u32  raw_size
  u32  crc32(raw)
  u8   compression     0 = stored, 1 = zlib
  u8   reserved[3]
u32    data_size
char   data[]
```

Advantages over a ZIP wrapper: no external dependency, random access, CRC
validation on every read, and a safe extraction path (absolute paths and
`..` components are rejected).  Individual files are zlib compressed only when
that makes them smaller.

## 3. Runtime loading

```cpp
AssetPackagePtr package = engine.loadAssetPackage("assets/packages/cascade.gpkg");
// The engine extracts the package to:
//   <asset_dir>/.package_cache/<package-name>/
// so the existing image / model / shader importers can consume it.
Scene::LoadResult result = Scene::loadFromFile(
    engine,
    engine.packageRoot() + "/scenes/cascade.scene.json",
    engine.packageRoot());
```

- `Engine::activePackage()` / `Engine::packageRoot()` expose the mounted
  package.
- `AssetPackage::readText` / `read` read straight from memory (used by the
  console and tools); `extractAll` is used for file-based importers.
- Packages can be mounted and unmounted at runtime; the demo's **Asset package
  browser** does exactly that (`--lab package`, `--scene package`,
  `--package FILE.gpkg`).

## 4. Scene schema

```jsonc
{
  "name": "My Effect",
  "render": {
    "tonemap": "aces",          // none | reinhard | aces
    "exposure": 1.15, "gamma": 2.2,
    "ambient": [0.03, 0.035, 0.05], "ambient_intensity": 1.0,
    "skybox": true, "ibl": true, "shading": "cook_torrance",
    "pipeline": "forward"        // forward | custom
  },
  "environment": {
    "hdr": "textures/sky.hdr", "intensity": 1.1,
    "skybox": true, "ibl": true, "rotation": 20
  },
  "physics": {
    "gravity": [0, -9.81, 0], "iterations": 10,
    "fixed_timestep": 0.008333, "debug_draw": false
  },
  "materials": [ /* inline material objects (optional) */ ],
  "material_files": ["materials/gold.material.json"],
  "entities": [
    {
      "name": "MainCamera",
      "transform": { "position": [8, 4, 11], "euler": [0, 30, 0], "scale": [1, 1, 1] },
      "components": {
        "camera": { "fov": 50, "near": 0.05, "far": 300 },
        "controller": { "mode": "orbit", "target": [0, 1.5, 0], "distance": 13,
                        "yaw": 30, "pitch": 18 }
      }
    },
    {
      "name": "Orb",
      "mesh": { "kind": "sphere", "size": 1.6, "detail": 48 },
      "material": "Gold",
      "components": {
        "rigidbody": { "type": "dynamic", "mass": 1.2, "restitution": 0.4 },
        "collider": { "shape": "sphere", "radius": 0.8 },
        "text": { "text": "Gold", "size": 0.25, "offset": [0, 1.2, 0] },
        "tag": { "tag": "pickup", "layer": 1 }
      }
    },
    {
      "name": "Hinge",
      "mesh": { "kind": "box", "size": 1.0 },
      "components": {
        "rigidbody": { "type": "dynamic" },
        "collider": { "shape": "box", "half_extents": [0.3, 1.0, 0.3] },
        "hinge": { "anchor": [0, 5, 0], "axis": [0, 0, 1],
                   "enable_limit": true, "min_angle": -75, "max_angle": 75 }
      }
    },
    {
      "name": "Scripts",
      "components": { "script": { "path": "scripts/game.js", "language": "js" } }
    }
  ]
}
```

Mesh sources: procedural `{ "kind": "sphere|box|plane|torus", "size": 1.0,
"detail": 32 }` or `{ "model": "models/foo.glb" }`.

Component names map 1:1 to the RTTI types (`Camera`, `Light`, `Transform`,
`Mesh`, `Environment`, `Text`, `CameraController`, `Tag`, `Script`,
`RigidBody`, `Collider`, `HingeJoint`).  See `src/genesis/scene/scene_loader.cpp`
for every accepted key.

Scripts loaded from a package automatically receive the package root as their
`asset_root`; `asset("config/game.json")` and `asset_text(...)` therefore work
in both Lua and JavaScript.  When a build has no V8 and a scene requests
`language: "js"`, the loader automatically falls back to the sibling `.lua`
file if one exists.

## 5. Shader three-backend contract

`shaders/shader_manifest.json` documents the mapping.  A package must contain:

| Backend | Artifact |
|---|---|
| OpenGL 4.1 | GLSL 4.10 source (`.vert`, `.frag`) |
| Vulkan 1.1 | SPIR-V (`.spv`), compiled from the same GLSL source |
| Metal | MSL 2.1 (`.metal`), generated by SPIRV-Cross |

The engine's built-in shaders are regenerated by `scripts/build_shaders.py`
(requires `glslangValidator` and `spirv-cross`).  A package can either ship
copies of the built-in shaders (the samples do) or new shaders produced by the
same single-source pipeline.

## 6. Demo packages

| Package | Contents |
|---|---|
| `assets/packages/fx_showcase.gpkg` | PBR gold/glass/neon materials, textured ground, crate stack, hinge pendulum, trigger, JS script with HUD + impulses, Lua fallback, GLSL/SPIR-V/MSL shaders, config |
| `assets/packages/cascade.gpkg` | The complete physics mini-game (scene, 5 material files, 3-backend shaders, textures, `cascade.js`, `cascade.lua`, `config/game.json`) |

```bash
./build/bin/genesis_demo --scene package --package assets/packages/fx_showcase.gpkg
./build/bin/genesis_demo --scene game
```
