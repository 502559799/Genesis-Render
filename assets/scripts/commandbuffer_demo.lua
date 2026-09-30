-- Genesis Render - CommandBuffer / programmable pipeline demo (pure RTTI).
--
--   genesis_demo --script assets/scripts/commandbuffer_demo.lua
--   (works on top of any scene; with --scene empty it builds a small one)
--
-- 1. Render to texture: a second camera renders the scene into a
--    RenderTexture (command buffer injected at "before_scene").  The texture
--    is shown on a monitor mesh in the world and as a HUD picture-in-picture.
-- 2. Custom pipeline (key C): the built-in forward pipeline is switched off
--    and this script builds the whole frame itself:
--       scene -> HDR RT -> tone map -> LDR RT -> effect -> backbuffer
--    plus picture-in-picture and HUD.  Keys 1..9 / 0 select the effect.
-- 3. Organise / execute: every command below is *recorded* in tick(); the
--    renderer executes it later on the GPU.

local S = {
  effects = {"none", "edge", "blur", "pixelate", "posterize", "sepia", "grayscale", "chromatic", "scanline", "invert"},
  effect = 2,
  custom = false,
  monitor = true,
}

local function ensureScene()
  if world:find("MainCamera") or world:find("ScriptCamera") then return end
  local cam = world:create("ScriptCamera")
  cam:add("Camera").fov = 50
  local ctrl = cam:add("CameraController")
  ctrl.target = vec3(0, 0.8, 0); ctrl.distance = 8; ctrl.pitch = 20
  local sun = world:create("Sun"); local l = sun:add("Light"); l.intensity = 3.5
  sun.Transform.euler = vec3(-45, 30, 0)
  world:create("Env"):add("Environment").hdr = asset("textures/sky_equirect.hdr")
  local floor = renderer:createMaterial("cb_floor")
  floor:setTexture("base_color", asset("textures/checker_albedo.png")); floor.roughness = 0.8; floor.uv_scale = vec2(4, 4)
  world:create("Floor"):setMesh(renderer:createMesh("plane", 20), floor)
  local mats = {color(0.9, 0.3, 0.2), color(0.2, 0.8, 0.4), color(0.2, 0.5, 1.0)}
  for i = 1, 3 do
    local m = renderer:createMaterial("cb_ball_" .. i); m.base_color = mats[i]; m.roughness = 0.3; m.metallic = i == 2 and 1 or 0
    local e = world:create("Ball" .. i); e:setMesh(renderer:createMesh("sphere", 1.0, 40), m)
    e.Transform.position = vec3(-2 + (i - 1) * 2, 0.5, 0)
  end
end

function start(id)
  ensureScene()
  local w, h = renderer.width, renderer.height

  -- ---- 1. render-to-texture from an orbiting "security camera" ----------
  S.monitor_rt = RenderTexture.new("monitor", 512, 288, true, true)
  S.monitor_cb = CommandBuffer.new("security_camera")
  renderer:addCommandBuffer("before_scene", S.monitor_cb)

  S.screen_mat = renderer:createMaterial("monitor_screen", "unlit")
  S.screen_mat:setTexture("base_color", S.monitor_rt)
  S.screen_mat.double_sided = true
  S.screen = world:create("MonitorScreen")
  S.screen:setMesh(renderer:createMesh("plane", 1.0), S.screen_mat)
  S.screen.Transform.position = vec3(-4.2, 1.6, -1.5)
  S.screen.Transform.scale = vec3(3.2, 1.0, 1.8)
  S.screen.Transform.euler = vec3(90, 25, 0)
  local frame = renderer:createMaterial("monitor_frame")
  frame.base_color = color(0.05, 0.05, 0.06); frame.roughness = 0.4; frame.metallic = 0.8
  local back = world:create("MonitorFrame")
  back:setMesh(renderer:createMesh("box", 1.0), frame)
  back.parent = S.screen
  back.Transform.position = vec3(0, -0.03, 0)
  back.Transform.scale = vec3(1.06, 0.04, 1.08)

  -- ---- 2. custom pipeline resources -------------------------------------
  S.scene_rt = RenderTexture.new("custom_scene", w, h, true, true)
  S.ldr_rt = RenderTexture.new("custom_ldr", w, h, false, false)
  S.fx_rt = RenderTexture.new("custom_fx", w, h, false, false)
  S.custom_cb = CommandBuffer.new("custom_pipeline")
  renderer:addCustomCommandBuffer(S.custom_cb)

  -- ---- HUD --------------------------------------------------------------
  S.hud = CommandBuffer.new("cb_hud")
  renderer:addCommandBuffer("overlay", S.hud)
  print("commandbuffer_demo: C = custom pipeline, 1..0 = effect, M = monitor")
end

local function recordMonitor(t)
  local cb = S.monitor_cb
  cb:clear()
  if not S.monitor then return end
  local eye = vec3(math.cos(t * 0.4) * 7, 3.5, math.sin(t * 0.4) * 7)
  cb:setRenderTarget(S.monitor_rt)
  cb:clearRenderTarget(color(0, 0, 0, 1), true)
  cb:setCamera(eye, vec3(0, 0.8, 0), 55)
  cb:drawSkybox()
  cb:drawScene()
  cb:drawText3D("SECURITY CAM", vec3(0, 2.6, 0), 0.35, color(1, 0.2, 0.2), true, 0.2)
end

local function recordCustom(t)
  local cb = S.custom_cb
  cb:clear()
  local w, h = renderer.width, renderer.height
  if S.scene_rt.width ~= w or S.scene_rt.height ~= h then
    S.scene_rt:resize(w, h); S.ldr_rt:resize(w, h); S.fx_rt:resize(w, h)
  end
  local fx = S.effects[S.effect]
  -- scene into an HDR target, with gizmos drawn by the script
  cb:setRenderTarget(S.scene_rt)
  cb:clearRenderTarget(color(0, 0, 0, 1), true)
  cb:resetCamera()
  cb:drawSkybox()
  cb:drawScene()
  for i = 0, 11 do
    local a = t + i * math.pi / 6
    cb:drawLine3D(vec3(0, 0.05, 0), vec3(math.cos(a) * 3, 0.05, math.sin(a) * 3), color(0.2, 0.9, 1.0, 0.8), 0.03)
  end
  cb:drawText3D("CUSTOM PIPELINE", vec3(0, 3.0, 0), 0.45, color(1, 0.85, 0.3), true, 0.25)
  -- tone map, effect, present
  cb:blit(S.scene_rt, S.ldr_rt)                         -- HDR -> LDR (auto tone mapping)
  cb:blit(S.ldr_rt, S.fx_rt, fx, vec4(1.0, 2.5, 0, 0), color(0.2, 1.0, 0.6))
  cb:blit(S.fx_rt, "backbuffer")
  -- picture-in-picture of the untouched image
  cb:setRenderTarget("backbuffer")
  cb:drawRect(w - w * 0.3 - 24, h - h * 0.3 - 24, w * 0.3 + 8, h * 0.3 + 8, color(1, 1, 1, 0.9))
  cb:drawImage(S.ldr_rt, w - w * 0.3 - 20, h - h * 0.3 - 20, w * 0.3, h * 0.3)
  cb:drawText("original", w - w * 0.3 - 12, h - h * 0.3 - 16, 18 * (h / 720), color(0, 0, 0))
end

function tick(dt, id)
  local t = engine.time
  if input:keyPressed("C") then S.custom = not S.custom end
  if input:keyPressed("M") then S.monitor = not S.monitor end
  for i = 1, 10 do
    if input:keyPressed(tostring(i % 10)) then S.effect = i end
  end
  settings.pipeline = S.custom and "custom" or "forward"

  recordMonitor(t)
  recordCustom(t)

  local s = renderer.height / 720
  local hud = S.hud
  hud:clear()
  hud:drawRect(16 * s, renderer.height - 116 * s, 520 * s, 100 * s, color(0.02, 0.03, 0.05, 0.75))
  hud:drawText(S.custom and "Custom pipeline (C)" or "Forward pipeline + injected command buffers (C)",
               28 * s, renderer.height - 106 * s, 20 * s, color(1, 1, 1))
  hud:drawText("effect [1..0]: " .. S.effects[S.effect] .. "    monitor [M]: " .. (S.monitor and "on" or "off"),
               28 * s, renderer.height - 76 * s, 18 * s, color(0.6, 0.85, 1))
  hud:drawText("render to texture -> monitor mesh + picture-in-picture", 28 * s, renderer.height - 48 * s, 16 * s,
               color(0.75, 0.75, 0.75))
  if S.monitor and not S.custom then
    local pw, ph = 256 * s, 144 * s
    hud:drawRect(renderer.width - pw - 24 * s, 20 * s, pw + 8 * s, ph + 8 * s, color(0.9, 0.2, 0.2, 0.9))
    hud:drawImage(S.monitor_rt, renderer.width - pw - 20 * s, 24 * s, pw, ph)
  end
end
