-- light_pulse.lua : animate a point light color/intensity from Lua.
local t = 0.0

function onStart(self)
    Genesis.log("light pulse started")
end

function onUpdate(self, dt)
    t = t + dt
    local r = 0.5 + 0.5 * math.sin(t * 1.7)
    local g = 0.3
    local b = 1.0 - r
    self:setLightColor(r, g, b)
    self:setLightIntensity(18.0 + 14.0 * math.sin(t * 2.3) * math.sin(t * 2.3))
end
