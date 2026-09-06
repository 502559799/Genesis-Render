-- rotate.lua : spin the entity around Y and gently bob it up and down.
local t = 0.0

function onStart(self)
    Genesis.log("rotate.lua started on " .. self:name())
end

function onUpdate(self, dt)
    t = t + dt
    self:rotateY(35.0 * dt)
    local x, y, z = self:getPosition()
    self:setPosition(x, y + math.sin(t * 1.5) * 0.002, z)
end
