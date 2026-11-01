// FX showcase script - runs inside the V8 runtime, drives the scene through
// the same RTTI surface as Lua (renderer, physics, materials, CommandBuffer).
var hud = null;
var time = 0.0;
var orb = null;

function start(entityId) {
    orb = world.find("GoldOrb");
    hud = CommandBuffer.new("fx_package_hud");
    renderer.addCommandBuffer("overlay", hud);
    print("fx_showcase.js: started (entity " + entityId + ", reflected types " + reflect.types().length + ")");
}

function tick(dt, entityId) {
    time += dt;
    var dt_ = dt;
    if (orb && reflect.valid(orb)) {
        var impulse = null;
        if (input.keyPressed("space")) impulse = vec3(0.0, 7.0, 0.0);
        else if (input.mousePressed(0)) impulse = vec3((Math.random() - 0.5) * 3.0, 7.5, (Math.random() - 0.5) * 3.0);
        if (impulse) {
            var p = orb.Transform.position;
            physics.applyImpulse(orb, impulse, vec3(p.x, p.y, p.z));
        }
    }
    var w = renderer.width;
    var h = renderer.height;
    var scale = h / 720.0;
    hud.clear();
    hud.drawRect(16 * scale, h - 82 * scale, 620 * scale, 66 * scale, color(0.01, 0.02, 0.04, 0.62));
    hud.drawText("fx_showcase.gpkg  |  JS (V8) + RTTI scene  |  bodies " + physics.body_count + "  contacts " + physics.contacts,
                 32 * scale, h - 66 * scale, 20 * scale, color(0.65, 1.0, 0.9, 1.0), "left", 0.2);
    hud.drawText("click / SPACE: impulse the gold orb    F2 shading   F3 wireframe   F4 IBL",
                 32 * scale, h - 40 * scale, 16 * scale, color(0.82, 0.88, 0.98, 1.0), "left", 0.15);
    if (Math.floor(time * 0.5) % 2 === 0) {
        hud.drawRect(w - 150 * scale, 20 * scale, 130 * scale, 30 * scale, color(0.05, 0.35, 0.2, 0.7));
        hud.drawText("JS RUNNING", w - 138 * scale, 27 * scale, 17 * scale, color(0.5, 1.0, 0.7, 1.0));
    }
}
