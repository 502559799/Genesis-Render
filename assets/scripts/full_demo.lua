-- Genesis Render - script content of the showcase scene (RTTI API).
--
-- Creates its own material, meshes and 3D labels through reflected engine
-- objects, animates them every frame and records world-space gizmos into a
-- command buffer that the renderer executes after the transparent pass.

local S = { orbiters = {} }

function start(id)
  local mat = renderer:createMaterial("Lua orbiter")
  mat.base_color = color(0.05, 0.05, 0.05)
  mat.emissive = color(1.0, 0.45, 0.15)
  mat.emissive_strength = 3.0
  mat.roughness = 0.4
  S.material = mat

  local mesh = renderer:createMesh("sphere", 0.22, 24)
  for i = 1, 3 do
    local e = world:create("LuaOrbiter" .. i)
    e:setMesh(mesh, mat)
    S.orbiters[i] = e
  end
  local t = S.orbiters[1]:add("Text")
  t.text = "Lua (RTTI)"
  t.size = 0.16
  t.offset = vec3(0, 0.3, 0)
  t.color = color(1.0, 0.7, 0.4)
  t.outline = 0.2

  S.gizmos = CommandBuffer.new("lua_gizmos")
  renderer:addCommandBuffer("after_transparent", S.gizmos)
  print("full_demo.lua: " .. #S.orbiters .. " orbiters, material '" .. mat.name .. "'")
end

function tick(dt, id)
  local t = engine.time
  local center = vec3(0, 1.35, 0)
  S.gizmos:clear()
  for i, e in ipairs(S.orbiters) do
    local a = t * 0.9 + i * 2.094
    local p = center + vec3(math.cos(a) * 1.25, math.sin(t * 1.7 + i) * 0.35, math.sin(a) * 1.25)
    e.Transform.position = p
    S.gizmos:drawLine3D(center, p, color(1.0, 0.5, 0.2, 0.5), 0.01)
  end
  S.material.emissive_strength = 2.0 + math.sin(t * 3.0)
end
