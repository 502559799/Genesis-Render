-- Cascade - Lua fallback (used automatically when the build has no V8).
local config, hud, targets, balls
local score, time_left, state, cooldown, target_score, elapsed = 0, 60, "playing", 0, 6, 0

local function read_config()
  -- The Lua sandbox has no JSON library; the game config is mirrored here.
  -- (The JavaScript script reads config/game.json via asset_text.)
  return { duration = 60, target_score = 6, launch_speed = 22, ball_radius = 0.36,
           camera_height = 3.2, launcher_z = 8.5, displacement_threshold = 1.6,
           launch_cooldown = 0.28 }
end

local function collect()
  targets = {}
  for _, e in ipairs(world:entities()) do
    if e:has("Tag") and e:get("Tag").tag == "target" then
      local p = e.Transform.position
      targets[#targets + 1] = { entity = e, home = vec3(p.x, p.y, p.z), scored = false }
    end
  end
  balls = {}
end

local function restart()
  for _, t in ipairs(targets) do
    if reflect.valid(t.entity) then
      t.entity.Transform.position = t.home
      t.entity.Transform.euler = vec3(0, 0, 0)
      local rb = t.entity:get("RigidBody")
      if rb then
        rb.linear_velocity = vec3(0, 0, 0)
        rb.angular_velocity = vec3(0, 0, 0)
        rb.sleeping = false
      end
      t.scored = false
    end
  end
  for _, b in ipairs(balls) do if reflect.valid(b) then b:destroy() end end
  balls = {}
  score, time_left, state, cooldown = 0, config.duration or 60, "playing", 0
end

local function launch()
  local w = renderer.width
  local m = input.mouse
  local sx = (m.x / math.max(1, w) - 0.5) * 8
  local start = vec3(sx, config.camera_height or 3.2, config.launcher_z or 8.5)
  local dir = vec3(0 - start.x, 1.9 - start.y + 0.9, 0 - start.z).normalized()
  local speed = config.launch_speed or 22
  local radius = config.ball_radius or 0.36
  local e = world:create("Ball_" .. tostring(#balls))
  local mat = renderer:findMaterial("Ball")
  if not mat then mat = renderer:createMaterial("Ball") end
  e:setMesh(renderer:createMesh("sphere", radius * 2, 24), mat)
  e.Transform.position = start
  local rb = e:add("RigidBody")
  rb.type = "dynamic"
  rb.mass = 0.7
  rb.restitution = 0.45
  rb.linear_velocity = dir * speed
  local col = e:add("Collider")
  col.shape = "sphere"
  col.radius = radius
  balls[#balls + 1] = e
  cooldown = config.launch_cooldown or 0.28
end

local function draw_hud()
  if not hud then return end
  local w, h = renderer.width, renderer.height
  hud:clear()
  hud:drawRect(0, 0, w, 62, color(0.015, 0.02, 0.035, 0.78))
  hud:drawText("CASCADE", 22, 16, 28, color(1, 0.78, 0.25), "left", 0.25)
  hud:drawText("score " .. score .. " / " .. target_score, 210, 20, 24, color(0.9, 0.95, 1))
  hud:drawText(string.format("time %.1f s", math.max(0, time_left)), w * 0.52, 21, 22, color(0.8, 0.9, 1))
  hud:drawText("balls " .. #balls, w - 210, 21, 20, color(0.55, 0.85, 1))
  hud:drawText("drag: camera  wheel: zoom  click / SPACE: launch  R: restart", 22, h - 30, 16,
               color(0.82, 0.88, 0.98), "left", 0.15)
  if state == "won" or state == "lost" then
    local title = state == "won" and "CLEARED!" or "TIME UP"
    hud:drawRect(w * 0.5 - 190, h * 0.5 - 60, 380, 120, color(0.01, 0.02, 0.04, 0.82))
    hud:drawText(title, w * 0.5, h * 0.5 - 44, 46, color(0.4, 1, 0.6), "center", 0.3)
    hud:drawText("score " .. score .. " / " .. target_score .. "  -  press R to play again",
                 w * 0.5, h * 0.5 + 16, 19, color(0.85, 0.9, 1), "center", 0.2)
  end
end

function start(entity_id)
  config = read_config()
  target_score = config.target_score or 6
  time_left = config.duration or 60
  collect()
  hud = CommandBuffer.new("cascade_hud_lua")
  renderer:addCommandBuffer("overlay", hud)
  print("cascade.lua: " .. #targets .. " targets")
end

function tick(dt, entity_id)
  elapsed = elapsed + dt
  cooldown = math.max(0, cooldown - dt)
  if input:keyPressed("r") then restart() end
  if input:keyPressed("p") then physics.debug_draw = not physics.debug_draw end
  if state == "playing" then
    time_left = time_left - dt
    if (input:mousePressed(0) or input:keyPressed("space")) and cooldown <= 0 then launch() end
    for _, t in ipairs(targets) do
      if not t.scored and reflect.valid(t.entity) then
        local p = t.entity.Transform.position
        local dx, dy, dz = p.x - t.home.x, p.y - t.home.y, p.z - t.home.z
        if math.sqrt(dx * dx + dy * dy + dz * dz) > (config.displacement_threshold or 1.6) or p.y < 0.55 then
          t.scored = true
          score = score + 1
        end
      end
    end
    if score >= target_score then state = "won"
    elseif time_left <= 0 then time_left = 0; state = "lost" end
  end
  draw_hud()
end
