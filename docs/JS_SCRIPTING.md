# JavaScript Scripting (V8)

The engine now embeds V8 alongside Lua 5.4.  JavaScript is a first-class
scripting language: it uses the same `ScriptComponent`, the same generic RTTI
registry and the same `start` / `tick` lifecycle as Lua.

## 1. Build

V8 is optional (`GENESIS_ENABLE_JS`, default ON).  The build links `libnode`
which exports the V8 embedding API:

```bash
scripts/fetch_deps.sh                 # Linux: V8 + SDL2 + Vulkan headers
./build.sh                            # V8 auto-detected in ../deps/prefix
./build.sh --no-js                    # Lua-only build
```

CMake variables: `GENESIS_V8_INCLUDE_DIR`, `GENESIS_V8_LIBRARY`,
`GENESIS_V8_ROOT`.  When V8 is missing the engine still compiles and the demo
shows `JS: unavailable`; packages fall back to their `.lua` script.

## 2. Lifecycle

Attach a script to any entity:

```cpp
ECS::ScriptComponent sc;
sc.script_path = "scripts/game.js";
sc.language = ECS::ScriptLanguage::JavaScript;
```

or in a scene file:

```json
{ "components": { "script": { "path": "scripts/game.js", "language": "js" } } }
```

The script runs once in its own `v8::Context` (sandboxed globals) and defines:

```js
function start(entityId) {}          // once after loading
function tick(deltaTime, entityId) {} // every frame
```

`this` is the owning entity (a reflected `Entity` object).

## 3. Capability parity with Lua

| Capability | Lua | JavaScript |
|---|---|---|
| Generic RTTI property get/set | yes | yes (`obj.prop = value`) |
| Reflected methods | `obj:method(...)` | `obj.method(...)` |
| Dynamic component access | `e.Transform` | `e.Transform`, `e.RigidBody`, ... |
| `Type.new(...)` constructors | `Material.new`, `RenderTexture.new`, `CommandBuffer.new` | same |
| Engine globals | `engine, world, renderer, settings, input` | same + `physics` |
| Reflection metadata | `reflect.types/describe/typeof/valid/get/set/call/new` | same |
| Value types | `vec2/3/4`, `color`, `quat`, `mat4`, `euler`, `axis_angle`, `look_rotation`, `trs` | same, with prototype helpers (`length`, `normalized`, `dot`, `cross`, `lerp`) |
| Assets | `asset()`, `asset_text()` | same (resolves against the active package) |
| Logging | `print`, `genesis.log_*` | same |
| Legacy facade | `genesis.*` | `genesis.*` compatibility object |
| Time | `genesis.elapsed/frame/set_time_scale` | same |
| Console | Lua / JS console with examples | same |

Because both bindings are generated from `Reflect::Registry`, every type added
to `reflection/engine_bindings.cpp` (including the physics components and the
`Physics` system) is immediately available in both languages.

## 4. Value conversion

- `vec2/vec3/vec4` → plain objects `{x, y, z, w}` with a vector prototype.
- `color` → `{r, g, b, a}`.
- `quat` → `{x, y, z, w}` with the hidden `__kind = "quat"` marker.
- `mat4` → array of 16 numbers with `__kind = "mat4"`.
- Lists → JavaScript arrays.
- Reflected objects → cached wrapper objects with a named-property interceptor.
  Wrappers compare by engine identity and report `__type` / `__valid`.

## 5. Example (scene construction + physics + HUD)

```js
var hud;
function start(id) {
    var gold = renderer.createMaterial("JS gold");
    gold.base_color = color(1.0, 0.76, 0.33, 1);
    gold.metallic = 1.0;
    gold.roughness = 0.2;

    var e = world.create("JSSphere");
    e.setMesh(renderer.createMesh("sphere", 1.2, 32), gold);
    e.Transform.position = vec3(0, 2, 0);

    var rb = e.add("RigidBody");
    rb.type = "dynamic";
    rb.mass = 1.0;
    var col = e.add("Collider");
    col.shape = "sphere";
    col.radius = 0.6;

    hud = CommandBuffer.new("js_hud");
    renderer.addCommandBuffer("overlay", hud);
    print("JS started for entity " + id);
}

function tick(dt, id) {
    if (input.keyPressed("space"))
        physics.applyImpulse(world.find("JSSphere"), vec3(0, 8, 0), vec3(0, 2, 0));

    hud.clear();
    hud.drawText("fps " + engine.fps.toFixed(0) + "  bodies " + physics.body_count,
                 20, 20, 22, color(0.7, 1.0, 0.9, 1));
}
```

## 6. Console and automation

The demo console switches between **Lua 5.4** and **JavaScript (V8)**:

```bash
./build/bin/genesis_demo --lab inspector   # Windows -> Lua / JS console
```

For tests, `JSSystem::executeInScript(entity, code)` runs a snippet in a
loaded script's own context (used by the package mini-game test to call
`launch()` and verify the created ball is simulated).

## 7. Current limitations

- One V8 isolate per `JSSystem`; scripts run on the main thread.
- No modules / `require`; each script is a classic script with its own context.
- `setTimeout`, DOM, Node.js APIs and `fetch` are not exposed (deliberately:
  the sandbox contains only engine globals).
- The wrapper cache keeps one JS object per engine object for the lifetime of
  the isolate (bounded by the number of distinct reflected objects).
