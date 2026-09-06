-- 呼吸灯脚本: 通过键盘控制亮度方向的示例 (需引擎侧扩展光照绑定, 这里演示 input API)
function on_create()
    log("pulse script attached to: " .. entity.get_name())
end

function on_update(dt)
    local s = 0.6 + 0.4 * math.sin(time.elapsed * 3.0)
    entity.set_scale(s, s, s)
    if input.is_key_down("Space") then
        entity.set_scale(1.5, 1.5, 1.5)
    end
end
