-- Lua fallback for the FX showcase package (same capabilities as the JS script).
local hud, time, orb
function start(entity_id)
  orb = world:find("GoldOrb")
  hud = CommandBuffer.new("fx_package_hud_lua")
  renderer:addCommandBuffer("overlay", hud)
  print("fx_showcase.lua: started")
end
function tick(dt, entity_id)
  time = time + dt
  if orb and reflect.valid(orb) then
    if input:keyPressed("space") then
      local p = orb.Transform.position
      physics:applyImpulse(orb, vec3(0, 7, 0), vec3(p.x, p.y, p.z))
    elseif input:mousePressed(0) then
      local p = orb.Transform.position
      physics:applyImpulse(orb, vec3((math.random() - 0.5) * 3, 7.5, (math.random() - 0.5) * 3), vec3(p.x, p.y, p.z))
    end
  end
  local w, h = renderer.width, renderer.height
  hud:clear()
  hud:drawRect(16, h - 82, 620, 66, color(0.01, 0.02, 0.04, 0.62))
  hud:drawText("fx_showcase.gpkg | Lua fallback | bodies " .. physics.body_count, 32, h - 66, 20,
               color(0.65, 1, 0.9), "left", 0.2)
  hud:drawText("click / SPACE: impulse the gold orb", 32, h - 40, 16, color(0.82, 0.88, 0.98), "left", 0.15)
end
