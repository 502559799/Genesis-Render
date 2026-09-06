-- Genesis Render 2.0 - demo animation script
--
-- Loaded by ScriptSystem into a sandboxed environment table.  The engine calls
--   start(entity_id)            once, right after the chunk is compiled
--   tick(delta_time, entity_id) every frame
-- Everything below uses the `genesis.*` API exported by script_system.cpp.

local time = 0.0
local torus = nil
local sphere = nil
local warm = nil
local cool = nil
local frames = 0

function start(id)
    genesis.log_info("demo_anim.lua: start")
    torus  = genesis.entity_find("GenesisTorus")
    sphere = genesis.entity_find("GenesisSphere")
    warm   = genesis.entity_find("PointLightWarm")
    cool   = genesis.entity_find("PointLightCool")
    genesis.log_info(string.format("demo_anim.lua: torus=%s sphere=%s warm=%s cool=%s",
        tostring(torus), tostring(sphere), tostring(warm), tostring(cool)))
end

function tick(dt, id)
    time = time + dt
    frames = frames + 1

    -- Spin the imported glTF primitives.
    if torus then
        genesis.rotate(torus, 0.0, 35.0 * dt, 12.0 * dt)
    end
    if sphere then
        local y = 0.9 + math.sin(time * 1.3) * 0.25
        genesis.set_position(sphere, -2.6, y, 0.8)
    end

    -- Orbit the two point lights around the scene.
    if warm then
        genesis.set_position(warm, math.cos(time * 0.7) * 3.0, 1.6 + math.sin(time * 1.1) * 0.4,
                             math.sin(time * 0.7) * 3.0)
    end
    if cool then
        genesis.set_position(cool, math.cos(time * 0.5 + 2.4) * 3.4, 1.3,
                             math.sin(time * 0.5 + 2.4) * 3.4)
    end

    -- Draw a debug gizmo showing the warm light's orbit (exercises the RHI
    -- push-constant path through the debug line pipeline).
    if warm then
        local x, y, z = genesis.get_position(warm)
        genesis.render_draw_line(x, y, z, x, y + 0.5, z, 1.0, 0.6, 0.2)
        genesis.render_draw_box(x, y, z, 0.12, 0.12, 0.12, 1.0, 0.6, 0.2)
    end

    -- Keyboard driven render settings.
    if genesis.input_key_pressed("1") then
        genesis.render_set_shading(0)   -- Cook-Torrance (GGX)
        genesis.log_info("shading -> Cook-Torrance")
    end
    if genesis.input_key_pressed("2") then
        genesis.render_set_shading(1)   -- Blinn-Phong
        genesis.log_info("shading -> Blinn-Phong")
    end
    if genesis.input_key_pressed("3") then
        genesis.render_set_tonemap(2)   -- ACES
        genesis.log_info("tonemap -> ACES")
    end
    if genesis.input_key_pressed("4") then
        genesis.render_set_tonemap(1)   -- Reinhard
        genesis.log_info("tonemap -> Reinhard")
    end
    if genesis.input_key_pressed("5") then
        genesis.render_set_ibl(true)
        genesis.log_info("IBL on")
    end
    if genesis.input_key_pressed("6") then
        genesis.render_set_ibl(false)
        genesis.log_info("IBL off")
    end

    -- Periodic statistics so the log panel shows the script is alive.
    if frames % 240 == 0 then
        local s = genesis.render_stats()
        genesis.log_info(string.format(
            "lua stats: backend=%s draws=%d tris=%d meshes=%d lights=%d ibl=%s frame=%d",
            tostring(s.backend), s.draw_calls, s.triangles, s.meshes, s.lights,
            tostring(s.ibl_ready), s.frame))
    end
end
