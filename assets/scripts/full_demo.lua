-- Genesis Render full capability demo (Lua / RTTI / CommandBuffer)
-- Adds its own lights/procedural meshes, records free command-buffer draws
-- and SDF text every frame.  The engine clears the command list each frame
-- end, so this only needs to ADD draws.  Post effect is driven by the UI
-- panel (programmable pipeline) in this scene.

function start(id)
  genesis.log_info('full_demo: building scripted scene')
  genesis.create_light(2.5, 6, 2, 0, 1, 0.95, 0.9, 4)
  genesis.create_light(3, 1.5, 2, 1, 1, 0.7, 0.3, 8)
  sphere_a = genesis.create_mesh('sphere', -1.2, 0.6, 0, 1, 0.3, 0.2, 0.0, 0.3)
  sphere_b = genesis.create_mesh('sphere',  1.2, 0.6, 0, 0.2, 0.6, 1.0, 1.0, 0.2)
  genesis.create_mesh('box', 0, 0.5, -2, 0.3, 0.3, 0.9, 0.2, 0.8)
  genesis.log_info('full_demo: scene ready, materials='..genesis.material_count())
end

function tick(dt, id)
  local t = genesis.elapsed()
  genesis.rotate(sphere_a, 0, 30*dt, 0)
  genesis.rotate(sphere_b, 0, -25*dt, 0)

  -- CommandBuffer free draws (organise; submitted next frame by the engine)
  genesis.cmd_quad3d(0, 2.6, 0, 0.5, 0.04, 0.9, 0.4, 0.2, 0.85)
  genesis.cmd_fill2d(20, 700, 220, 24, 0.1, 0.12, 0.16, 0.5)
  genesis.cmd_line2d(40, 712 + math.sin(t*2)*8, 260, 712, 0.3, 0.9, 0.5, 0.9)

  -- SDF 2D HUD + 3D world text
  genesis.text2d(30, 78, 18, 'Lua CommandBuffer + SDF text', 1, 1, 1, 0.95)
  genesis.text3d(-1.2, 1.9, 0, 0.28, 'Lua Sphere A', 0.1, 1.0, 0.3, 1.0)
end
