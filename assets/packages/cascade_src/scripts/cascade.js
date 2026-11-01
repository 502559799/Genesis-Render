// Cascade - physics mini-game (JavaScript / V8, all engine access through RTTI).
var config = null;
var hud = null;
var targets = [];
var balls = [];
var score = 0;
var time_left = 0.0;
var state = "playing";
var cooldown = 0.0;
var target_score = 6;
var elapsed_round = 0.0;

function readConfig() {
    var text = asset_text("config/game.json");
    if (!text) return {};
    try { return JSON.parse(text); } catch (e) { print("config parse failed: " + e); return {}; }
}

function collectTargets() {
    targets = [];
    var entities = world.entities();
    for (var i = 0; i < entities.length; ++i) {
        var e = entities[i];
        if (!e.has("Tag")) continue;
        var tag = e.get("Tag");
        if (tag.tag !== "target") continue;
        var p = e.Transform.position;
        targets.push({ entity: e, home: vec3(p.x, p.y, p.z), scored: false });
    }
}

function restart() {
    for (var i = 0; i < targets.length; ++i) {
        var t = targets[i];
        if (!reflect.valid(t.entity)) continue;
        t.entity.Transform.position = t.home;
        t.entity.Transform.euler = vec3(0, 0, 0);
        var rb = t.entity.get("RigidBody");
        if (rb) {
            rb.linear_velocity = vec3(0, 0, 0);
            rb.angular_velocity = vec3(0, 0, 0);
            rb.active = false;
            rb.sleeping = false;
            rb.active = true;
        }
        t.scored = false;
    }
    for (var b = 0; b < balls.length; ++b) {
        if (reflect.valid(balls[b])) balls[b].destroy();
    }
    balls = [];
    score = 0;
    time_left = config.duration || 60.0;
    elapsed_round = 0.0;
    state = "playing";
    cooldown = 0.0;
    print("cascade: round restarted");
}

function launch() {
    if (state !== "playing" || cooldown > 0.0) return;
    var w = renderer.width;
    var mouse = input.mouse;
    var sx = (mouse.x / Math.max(1, w) - 0.5) * 8.0;
    var start = vec3(sx, config.camera_height || 3.2, config.launcher_z || 8.5);
    var target = vec3(0, 1.9, 0);
    var dir = vec3(target.x - start.x, target.y - start.y + 0.9, target.z - start.z).normalized();
    var speed = config.launch_speed || 22.0;
    var radius = config.ball_radius || 0.36;

    var e = world.create("Ball_" + balls.length + "_" + Math.floor(elapsed_round * 1000));
    var material = renderer.findMaterial("Ball");
    if (!material) {
        material = renderer.createMaterial("Ball");
        material.base_color = color(0.2, 0.75, 1.0, 1.0);
        material.metallic = 0.7;
        material.roughness = 0.18;
    }
    e.setMesh(renderer.createMesh("sphere", radius * 2.0, 24), material);
    e.Transform.position = start;
    var rb = e.add("RigidBody");
    rb.type = "dynamic";
    rb.mass = config.ball_mass || 0.7;
    rb.restitution = 0.45;
    rb.friction = 0.35;
    rb.linear_velocity = vec3(dir.x * speed, dir.y * speed, dir.z * speed);
    var col = e.add("Collider");
    col.shape = "sphere";
    col.radius = radius;
    balls.push(e);
    cooldown = config.launch_cooldown || 0.28;
}

function drawHud() {
    if (!hud) return;
    var w = renderer.width;
    var h = renderer.height;
    var s = h / 720.0;
    hud.clear();
    hud.drawRect(0, 0, w, 62 * s, color(0.015, 0.02, 0.035, 0.78));
    hud.drawText("CASCADE", 22 * s, 16 * s, 28 * s, color(1.0, 0.78, 0.25, 1.0), "left", 0.25);
    hud.drawText("score " + score + " / " + target_score, 210 * s, 20 * s, 24 * s, color(0.9, 0.95, 1.0, 1.0));
    hud.drawText("time " + Math.max(0, time_left).toFixed(1) + " s", w * 0.52, 21 * s, 22 * s,
                 color(time_left < 10 ? 1.0 : 0.8, time_left < 10 ? 0.35 : 0.9, 0.45, 1.0));
    hud.drawText("balls " + balls.length, w - 210 * s, 21 * s, 20 * s, color(0.55, 0.85, 1.0, 1.0));
    hud.drawText("drag: camera   wheel: zoom   click / SPACE: launch   R: restart   P: physics debug",
                 22 * s, h - 30 * s, 16 * s, color(0.82, 0.88, 0.98, 1.0), "left", 0.15);

    if (state === "won" || state === "lost") {
        var title = state === "won" ? "CLEARED!" : "TIME UP";
        var tint = state === "won" ? color(0.35, 1.0, 0.55, 1.0) : color(1.0, 0.4, 0.35, 1.0);
        hud.drawRect(w * 0.5 - 190 * s, h * 0.5 - 60 * s, 380 * s, 120 * s, color(0.01, 0.02, 0.04, 0.82));
        hud.drawText(title, w * 0.5, h * 0.5 - 44 * s, 46 * s, tint, "center", 0.3);
        hud.drawText("score " + score + " / " + target_score + "   -   press R to play again",
                     w * 0.5, h * 0.5 + 16 * s, 19 * s, color(0.85, 0.9, 1.0, 1.0), "center", 0.2);
    }
}

function start(entityId) {
    config = readConfig();
    target_score = config.target_score || 6;
    time_left = config.duration || 60.0;
    collectTargets();
    hud = CommandBuffer.new("cascade_hud");
    renderer.addCommandBuffer("overlay", hud);
    print("cascade: " + targets.length + " targets, target score " + target_score +
          ", package root " + asset("config/game.json"));
}

function tick(dt, entityId) {
    elapsed_round += dt;
    cooldown = Math.max(0, cooldown - dt);
    if (input.keyPressed("r")) restart();
    if (input.keyPressed("p")) physics.debug_draw = !physics.debug_draw;

    if (state === "playing") {
        time_left -= dt;
        if (input.mousePressed(0) || input.keyPressed("space")) launch();
        for (var i = 0; i < targets.length; ++i) {
            var t = targets[i];
            if (t.scored || !reflect.valid(t.entity)) continue;
            var p = t.entity.Transform.position;
            var dx = p.x - t.home.x, dy = p.y - t.home.y, dz = p.z - t.home.z;
            var displaced = Math.sqrt(dx * dx + dy * dy + dz * dz);
            if (displaced > (config.displacement_threshold || 1.6) || p.y < 0.55) {
                t.scored = true;
                score += 1;
            }
        }
        if (score >= target_score) state = "won";
        else if (time_left <= 0.0) { time_left = 0.0; state = "lost"; }
    }
    drawHud();
}
