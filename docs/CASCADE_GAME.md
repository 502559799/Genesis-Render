# Cascade - packaged physics mini-game

Cascade is the end-to-end demonstration that a complete game can be built from
an offline `.gpkg` package, loaded at runtime, and driven by JavaScript with a
Lua fallback.

## 1. Package contents

`assets/packages/cascade_src/` → `assets/packages/cascade.gpkg`:

```
package.json                    name, scene entry, capabilities
scenes/cascade.scene.json       platform, crates, pendulum, trigger, camera, lights
materials/platform.material.json
materials/crate.material.json
materials/ball.material.json
materials/pendulum.material.json
materials/trigger.material.json
shaders/scene/pbr_mesh.{vert,frag}      GLSL 4.10
shaders/generated/pbr_mesh.*.spv        SPIR-V
shaders/generated/pbr_mesh.*.metal      MSL 2.1
textures/{checker_albedo,checker_metallic_roughness,panel_normal}.png
textures/small_equirect.hdr
scripts/cascade.js              gameplay (V8 + RTTI)
scripts/cascade.lua             automatic fallback when V8 is unavailable
config/game.json                duration, score target, launch speed, radius, ...
config/render.json              suggested rendering settings
```

## 2. Gameplay

- Aim with the mouse; click or press `SPACE` to launch a ball.
- Knock golden crates off the platform; each displaced crate scores a point.
- `R` restarts the round, `P` toggles physics debug shapes.
- Win at 6 points before the 60 s timer; otherwise "TIME UP".
- HUD: score, time, ball count, instructions, win/lose overlay.

The scene includes all three requested physics behaviours:

| Physics feature | In the game |
|---|---|
| Rigid bodies | crates, launched balls, pendulum |
| Collision detection / bodies | box and sphere colliders against the platform, crates and ground |
| Hinge joint | swinging pendulum obstacle with angle limits |

## 3. Runtime loading

```bash
./build/bin/genesis_demo --scene game
# equivalent: --scene package --package assets/packages/cascade.gpkg
```

`Engine::loadAssetPackage()` extracts the package into
`assets/.package_cache/cascade/`, then `Scene::loadFromFile()` builds the ECS
scene and attaches `scripts/cascade.js` with the package root as `asset_root`.
The script reads `config/game.json` through `asset_text()`.

Without V8, the scene loader detects the missing JS engine and falls back to
`scripts/cascade.lua` automatically.

## 4. Scripting API used

Only reflected engine APIs are used:

- `world.create/find/entities`, `e.has/add/get/destroy`, `e.Transform`
- `renderer.createMaterial/createMesh/findMaterial`
- `CommandBuffer.new`, `drawRect`, `drawText`
- `input.keyPressed/mousePressed/mouse`
- `physics.applyImpulse`, `physics.debug_draw`
- `asset_text` for the package config

This keeps the game portable across Lua and JavaScript and exercises the same
C++ RTTI surface as the editor.

## 5. Verification

- `package.cascade_scene_runs_from_package` (GPU, per backend) loads the
  package, runs JS, calls the game's `launch()` entry point, verifies a ball is
  created, steps physics and checks that the ball travelled toward the crates
  and displaced a target.
- `./build/bin/genesis_demo --scene game` is part of `--selftest`.
- The package is validated by `tools/genesis_pack.py validate`.
