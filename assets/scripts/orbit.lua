-- 自转 + 公转演示脚本: 验证脚本系统驱动 ECS transform
local angle = 0.0

function on_create()
    log("orbit script attached to: " .. entity.get_name())
end

function on_update(dt)
    angle = angle + dt * 0.8
    -- 绕 Y 轴公转 (半径 2.2), 同时自转
    local x = math.cos(angle) * 2.2
    local z = math.sin(angle) * 2.2
    entity.set_position(x, 0.0, z)
    entity.set_rotation_euler(0.0, math.deg(angle * 2.0), 0.0)
end

function on_destroy()
    log("orbit script destroyed")
end
