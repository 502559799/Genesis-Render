-- Genesis Render - a complete scene built ONLY through the RTTI script API.
--
-- Nothing here uses hand written engine bindings: every object (entities,
-- components, materials, meshes, render settings, command buffers) is a
-- reflected C++ object reached through generic property / method access.
--
--   genesis_demo --scene empty --script assets/scripts/scene_builder.lua

local S = {}

local function spawn(name, mesh, material, position, scale)
  local e = world:create(name)
  e:setMesh(mesh, material)
  e.Transform.position = position
  if scale then e.Transform.scale = vec3(scale) end
  return e
end

local function label(e, text, height, col)
  local t = e:add("Text")
  t.text = text
  t.size = 0.22
  t.offset = vec3(0, height, 0)
  t.color = col or color(1, 1, 1)
  t.outline = 0.18
  t.outline_color = color(0, 0, 0, 0.85)
  return t
end

function start(id)
  log_msg = "scene_builder: building the scene through RTTI"
  print(log_msg)

  -- ---- renderer settings ------------------------------------------------
  settings.tonemap = "aces"
  settings.exposure = 1.1
  settings.ibl = true
  settings.skybox = true

  -- ---- camera with an orbit controller ----------------------------------
  S.camera = world:create("ScriptCamera")
  local cam = S.camera:add("Camera")
  cam.fov = 50
  cam.far = 300
  local ctrl = S.camera:add("CameraController")
  ctrl.mode = "orbit"
  ctrl.target = vec3(0, 0.9, 0)
  ctrl.distance = 10.5
  ctrl.yaw = 28
  ctrl.pitch = 24

  -- ---- lights -----------------------------------------------------------
  local sun = world:create("ScriptSun")
  local l = sun:add("Light")
  l.type = "directional"
  l.intensity = 3.5
  l.color = color(1.0, 0.95, 0.88)
  sun.Transform.euler = vec3(-42, 35, 0)

  S.warm = world:create("ScriptWarmLight")
  local w = S.warm:add("Light")
  w.type = "point"; w.color = color(1.0, 0.55, 0.25); w.intensity = 14; w.range = 12
  S.cool = world:create("ScriptCoolLight")
  local c = S.cool:add("Light")
  c.type = "point"; c.color = color(0.3, 0.6, 1.0); c.intensity = 12; c.range = 12

  -- ---- environment (HDR sky -> IBL) -------------------------------------
  local env = world:create("ScriptEnvironment"):add("Environment")
  env.hdr = asset("textures/sky_equirect.hdr")
  env.intensity = 1.0

  -- ---- ground: textured PBR material ------------------------------------
  local ground = renderer:createMaterial("script_ground")
  ground:setTexture("base_color", asset("textures/checker_albedo.png"))
  ground:setTexture("metallic_roughness", asset("textures/checker_metallic_roughness.png"))
  ground:setTexture("normal", asset("textures/panel_normal.png"))
  ground:setTexture("occlusion", asset("textures/tile_ao.png"))
  ground.metallic = 1.0
  ground.roughness = 1.0
  ground.uv_scale = vec2(3, 3)
  spawn("ScriptGround", renderer:createMesh("plane", 24), ground, vec3(0, 0, 0))

  -- ---- a metallic x roughness grid of spheres ---------------------------
  local sphere = renderer:createMesh("sphere", 0.8, 48)
  for row = 0, 1 do
    for col = 0, 4 do
      local m = renderer:createMaterial(string.format("grid_%d_%d", row, col))
      m.base_color = row == 0 and color(0.95, 0.64, 0.25) or color(0.2, 0.45, 0.95)
      m.metallic = row == 0 and 1.0 or 0.0
      m.roughness = 0.08 + col * 0.22
      spawn(m.name, sphere, m, vec3(-3.2 + col * 1.6, 0.55 + row * 1.1, -3.2))
    end
  end

  -- ---- an imported glTF model (its own materials) -----------------------
  local helmet = renderer:loadModel(asset("models/DamagedHelmet.glb"))
  if helmet then
    S.helmet = world:create("ScriptHelmet")
    S.helmet:setMesh(helmet)
    S.helmet.Transform.position = vec3(0, 1.4, 0.4)
    label(S.helmet, "glTF model", 1.25, color(1.0, 0.9, 0.4))
  end

  -- ---- special materials: emissive, unlit, transparent ------------------
  S.glow = renderer:createMaterial("script_glow")
  S.glow.base_color = color(0.05, 0.05, 0.05)
  S.glow.emissive = color(1.0, 0.28, 0.06)
  S.glow.emissive_strength = 1.5
  S.torus = spawn("ScriptTorus", renderer:createMesh("torus", 1.4, 64), S.glow, vec3(3.0, 1.0, 0.8))
  label(S.torus, "emissive", 0.9)

  local unlit = renderer:createMaterial("script_unlit", "unlit")
  unlit.base_color = color(0.2, 0.9, 0.5)
  local cube = spawn("ScriptUnlitCube", renderer:createMesh("box", 0.9), unlit, vec3(-3.0, 0.45, 0.8))
  label(cube, "unlit", 1.0)

  local glass = renderer:createMaterial("script_glass")
  glass.base_color = color(0.6, 0.85, 1.0, 0.35)
  glass.blend = "transparent"
  glass.roughness = 0.05
  local g = spawn("ScriptGlass", renderer:createMesh("sphere", 1.1, 48), glass, vec3(0.0, 0.55, 2.6))
  label(g, "transparent", 0.85)

  -- ---- HUD: a command buffer recorded every tick (organise step) ---------
  S.hud = CommandBuffer.new("script_hud")
  renderer:addCommandBuffer("overlay", S.hud)

  print(string.format("scene_builder: %d entities, %d materials, %d reflected types",
                      world.entity_count, renderer.material_count, #reflect.types()))
end

function tick(dt, id)
  local t = engine.time
  -- animate lights and the emissive torus through reflected properties
  S.warm.Transform.position = vec3(math.cos(t * 0.7) * 3.2, 1.6 + math.sin(t * 1.3) * 0.3, math.sin(t * 0.7) * 3.2)
  S.cool.Transform.position = vec3(math.cos(t * 0.5 + 2.4) * 3.6, 1.2, math.sin(t * 0.5 + 2.4) * 3.6)
  S.torus.Transform:rotate(vec3(0, 40 * dt, 15 * dt))
  S.glow.emissive_strength = 1.2 + 0.8 * math.sin(t * 2.0)
  if S.helmet then S.helmet.Transform:rotate(vec3(0, 12 * dt, 0)) end

  -- re-record the HUD: the renderer executes it after tone mapping
  local s = renderer.height / 720
  local x, y = renderer.width * 0.5 - 210 * s, renderer.height - 108 * s
  S.hud:clear()
  S.hud:drawRect(x, y, 420 * s, 92 * s, color(0.02, 0.03, 0.05, 0.72))
  S.hud:drawRectOutline(x, y, 420 * s, 92 * s, color(0.3, 0.7, 1.0, 0.9), 2 * s)
  S.hud:drawText("Scene built by Lua through RTTI", x + 14 * s, y + 10 * s, 26 * s, color(1, 1, 1))
  S.hud:drawText(string.format("%s  |  %.0f fps  |  %d draws  |  %d tris", engine.backend, engine.fps,
                               renderer.draw_calls, renderer.triangles), x + 14 * s, y + 46 * s, 18 * s, color(0.6, 0.85, 1.0))
end
