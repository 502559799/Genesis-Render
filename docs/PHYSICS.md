# Physics

The engine ships a compact, dependency-free rigid body solver.  It is an ECS
system (`order = 95`) that runs after scripting / camera control and before
`TransformSystem`, so the transforms it produces are rendered in the same
frame.

## 1. Components

| Component | Fields |
|---|---|
| `RigidBody` | `type` (`static` / `dynamic` / `kinematic`), `mass`, `restitution`, `friction`, `linear_damping`, `angular_damping`, `gravity_scale`, `use_gravity`, `linear_velocity`, `angular_velocity`, `sleeping`, `active` |
| `Collider` | `shape` (`sphere` / `box` / `plane` / `capsule`), `half_extents`, `radius`, `height`, `center`, `is_trigger`, `debug_draw` |
| `HingeJoint` | `other` (entity id or `-1` for the world), world `anchor`, world `axis`, `enable_limit`, `min_angle`, `max_angle`, `enable_motor`, `motor_speed`, `motor_force`, readonly `current_angle` |

All fields are exposed through RTTI, so the same physics setup is available in
C++, Lua, JavaScript, the inspector and JSON scenes.

```lua
local e = world:create("Ball")
e:setMesh(renderer:createMesh("sphere", 0.8), renderer:createMaterial("Ball"))
local rb = e:add("RigidBody")
rb.type = "dynamic"; rb.mass = 1.0; rb.restitution = 0.6
local col = e:add("Collider")
col.shape = "sphere"; col.radius = 0.4
```

## 2. Solver

- **Integration**: semi-implicit Euler, gravity/damping, quaternion integration
  for angular velocity, fixed 1/120 s substeps (configurable, max 6 per frame).
- **Broadphase**: AABB overlap (n²; demos stay in the hundreds of bodies).
- **Narrowphase**:
  - sphere/sphere, sphere/box, box/box (15-axis SAT with support points),
  - capsule/sphere, capsule/box, capsule/capsule (segment / 3-sphere approximation),
  - every shape against an infinite plane.
- **Response**: sequential impulses over 8–12 iterations, restitution with a
  low-speed cutoff, Coulomb friction, positional correction (75 %, 4 mm slop),
  sleeping after 0.8 s below the velocity thresholds.
- **Joints**: point constraint at the anchor + angular velocity projected onto
  the hinge axis, optional limits and motor.  `other == -1` anchors to the world.
- **Triggers**: contacts are reported but not resolved (`is_trigger`).
- **Numerical safety**: a non-finite body is reset and logged instead of
  poisoning the transform hierarchy.

Physics settings live in `Physics::PhysicsSettings` and are reflected as the
`Physics` object (`engine.physics` in C++, `physics` in Lua/JS):

```js
physics.gravity = vec3(0, -9.81, 0);
physics.solver_iterations = 12;
physics.debug_draw = true;
physics.paused = true;
physics.applyImpulse(entity, vec3(0, 8, 0), entity.Transform.position);
```

## 3. Demo

```bash
./build/bin/genesis_demo --scene physics --lab physics
```

The scene contains a static ground plane, a crate pyramid, bouncy spheres, a
static ramp, a capsule, a trigger volume and a hinge pendulum.  The **Physics
lab** panel provides:

- pause / single step, gravity, solver iterations,
- live body / contact / trigger / solve-time statistics,
- debug collider wireframes (toggleable),
- spawning spheres/boxes/capsules at a chosen height and spread,
- a list of recent contacts.

## 4. Mini-game integration

`assets/packages/cascade.gpkg` uses the same components from pure JavaScript:
`cascade.js` creates balls with `RigidBody` + `Collider`, scores displaced
crates, and restarts the round by teleporting bodies and clearing velocities.

## 5. API (`src/genesis/physics/physics.hpp`)

```cpp
Physics::PhysicsSystem &physics = engine.physics();
physics.settings().gravity = Math::Vec3(0, -9.81f, 0);
physics.applyImpulse(world, entity, impulse, world_point);
Physics::RayHit hit;
physics.rayCast(world, origin, direction, 100.0f, hit);
physics.setPaused(true);
physics.stepOnce(world);
physics.drawDebug(engine.render());
```

## 6. Tests

`tests/test_new_features.cpp` covers:

- free fall with gravity,
- sphere resting on an infinite plane (no penetration / no NaN),
- a three-box stack settling,
- a hinge keeping the anchor distance while swinging,
- contact generation.

## 7. Known limitations

- No convex hull / triangle mesh colliders, no character controller.
- Box-box contacts use one contact point (stacks can rock slightly).
- Scalar inertia approximation per body; no full inertia tensor.
- O(n²) broadphase and single-threaded solver (fine for gameplay scale).
- Hinge limits are angle-velocity guards rather than full constraint rows;
  motor torque is an impulse budget (`motor_force * dt`).
