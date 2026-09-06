-- Example entity script: continuously rotates the entity around Y.
-- API: genesis.getEuler / setEuler (degrees), key, time, log, ...
local M = {}

function M.onStart(e)
    genesis.log("rotate.lua started on: " .. genesis.entityName(e))
end

function M.onUpdate(e, dt)
    local x, y, z = genesis.getEuler(e)
    y = y + 25.0 * dt
    if y > 360.0 then y = y - 360.0 end
    genesis.setEuler(e, x, y, z)
end

return M
