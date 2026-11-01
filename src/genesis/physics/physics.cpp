/*
 * Genesis Render - rigid body physics implementation
 */
#include "physics/physics.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Genesis
{
    namespace Physics
    {
        namespace
        {
            constexpr float kEpsilon = 1e-5f;
            constexpr float kPositionPercent = 0.75f;
            constexpr float kPositionSlop = 0.004f;

            using Math::cross;
            using Math::dot;
            using Math::length;
            using Math::normalize;
        } // namespace

    } // namespace Physics

    namespace ECS
    {
        const char *bodyTypeName(ECS::BodyType type)
        {
            switch (type)
            {
            case ECS::BodyType::Static: return "static";
            case ECS::BodyType::Dynamic: return "dynamic";
            case ECS::BodyType::Kinematic: return "kinematic";
            }
            return "?";
        }

        const char *colliderShapeName(ECS::ColliderShape shape)
        {
            switch (shape)
            {
            case ECS::ColliderShape::Sphere: return "sphere";
            case ECS::ColliderShape::Box: return "box";
            case ECS::ColliderShape::Plane: return "plane";
            case ECS::ColliderShape::Capsule: return "capsule";
            }
            return "?";
        }
    } // namespace ECS

    namespace Physics
    {
        //==================================================================
        PhysicsSystem::PhysicsSystem() {}
        PhysicsSystem::~PhysicsSystem() { shutdown(); }

        bool PhysicsSystem::initialize(ECS::World *world)
        {
            world_ = world;
            bodies_.clear();
            joints_.clear();
            contacts_.clear();
            GEN_LOG_INFO("[Physics] initialised (gravity %.2f, %.2f, %.2f; %u solver iterations)",
                         settings_.gravity.x, settings_.gravity.y, settings_.gravity.z, settings_.solver_iterations);
            return true;
        }

        void PhysicsSystem::shutdown()
        {
            bodies_.clear();
            joints_.clear();
            contacts_.clear();
        }

        size_t PhysicsSystem::dynamicBodyCount() const
        {
            size_t n = 0;
            for (const Body &b : bodies_)
                if (b.type == ECS::BodyType::Dynamic) ++n;
            return n;
        }

        //==================================================================
        // Body construction
        //==================================================================
        int PhysicsSystem::findBody(ECS::Entity entity) const
        {
            for (size_t i = 0; i < bodies_.size(); ++i)
                if (bodies_[i].entity == entity) return (int)i;
            return -1;
        }

        void PhysicsSystem::rebuildBodies(ECS::World &world)
        {
            bodies_.clear();
            world.each<ECS::RigidBodyComponent>([&](ECS::Entity entity, ECS::RigidBodyComponent &rb)
            {
                ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(entity);
                if (!t || !rb.active) return;
                Body body;
                body.entity = entity;
                body.type = rb.type;
                body.position = t->position;
                body.rotation = t->rotation;
                body.linear_velocity = rb.linear_velocity;
                body.angular_velocity = rb.angular_velocity;
                body.restitution = rb.restitution;
                body.friction = rb.friction;
                body.linear_damping = rb.linear_damping;
                body.angular_damping = rb.angular_damping;
                body.gravity_scale = rb.gravity_scale;
                body.use_gravity = rb.use_gravity;
                body.sleeping = rb.sleeping;
                body.sleep_timer = rb.sleep_timer;

                if (ECS::ColliderComponent *col = world.getComponent<ECS::ColliderComponent>(entity))
                {
                    body.shape = col->shape;
                    body.has_collider = true;
                    body.is_trigger = col->is_trigger;
                    const float sx = std::fabs(t->scale.x), sy = std::fabs(t->scale.y), sz = std::fabs(t->scale.z);
                    body.half_extents = Math::Vec3(col->half_extents.x * sx, col->half_extents.y * sy,
                                                   col->half_extents.z * sz);
                    body.radius = col->radius * std::max(sx, std::max(sy, sz));
                    body.height = col->height * sy;
                    body.center = col->center;
                }
                else
                {
                    // Bodies without a collider still integrate (useful for
                    // purely visual dynamic objects).
                    body.shape = ECS::ColliderShape::Sphere;
                    body.radius = 0.0f;
                    body.has_collider = false;
                    body.is_trigger = true;
                }

                if (body.type == ECS::BodyType::Dynamic)
                {
                    const float m = std::max(0.0001f, rb.mass);
                    body.inv_mass = 1.0f / m;
                    float I = 1.0f;
                    if (body.shape == ECS::ColliderShape::Sphere)
                    {
                        const float r = std::max(0.01f, body.radius);
                        I = 0.4f * m * r * r;
                    }
                    else if (body.shape == ECS::ColliderShape::Box)
                    {
                        const Math::Vec3 &e = body.half_extents;
                        I = m * (e.x * e.x + e.y * e.y + e.z * e.z) / 3.0f;
                    }
                    else if (body.shape == ECS::ColliderShape::Capsule)
                    {
                        const float r = std::max(0.01f, body.radius);
                        const float h = std::max(r * 2.0f, body.height);
                        I = m * (h * h / 12.0f + r * r / 2.0f);
                    }
                    body.inv_inertia = I > 1e-6f ? 1.0f / I : 0.0f;
                }
                else
                {
                    body.inv_mass = 0.0f;
                    body.inv_inertia = 0.0f;
                }
                body.force = Math::Vec3(0.0f);
                body.torque = Math::Vec3(0.0f);
                bodies_.push_back(body);
            });
            trigger_count_ = 0;
        }

        void PhysicsSystem::rebuildJoints(ECS::World &world)
        {
            // Preserve the accumulated hinge angles from the previous frame.
            std::vector<std::pair<ECS::Entity, float>> angles;
            for (const Hinge &j : joints_) angles.push_back({j.a, j.current_angle});
            joints_.clear();
            world.each<ECS::HingeJointComponent>([&](ECS::Entity entity, ECS::HingeJointComponent &jc)
            {
                if (findBody(entity) < 0) return;
                Hinge j;
                j.a = entity;
                j.b = jc.other != ECS::kInvalidEntityId ? world.entityFromId(jc.other) : ECS::nullEntity();
                ECS::TransformComponent *ta = world.getComponent<ECS::TransformComponent>(entity);
                j.anchor = jc.anchor;
                if (Math::lengthSquared(j.anchor) < 1e-12f && ta) j.anchor = ta->position;
                j.axis = Math::normalize(jc.axis);
                if (Math::lengthSquared(j.axis) < kEpsilon) j.axis = Math::Vec3(0, 1, 0);
                j.enable_limit = jc.enable_limit;
                j.min_angle = Math::toRadians(jc.min_angle_degrees);
                j.max_angle = Math::toRadians(jc.max_angle_degrees);
                j.enable_motor = jc.enable_motor;
                j.motor_speed = jc.motor_speed;
                j.motor_force = jc.motor_force;
                j.current_angle = jc.current_angle;
                for (const auto &prev : angles)
                    if (prev.first == entity) j.current_angle = prev.second;
                joints_.push_back(j);
            });
        }

        //==================================================================
        // Geometry helpers
        //==================================================================
        Math::AABB PhysicsSystem::worldAABB(const Body &body)
        {
            Math::AABB box;
            box.valid = true;
            switch (body.shape)
            {
            case ECS::ColliderShape::Plane:
                box.minPoint = Math::Vec3(-1e6f);
                box.maxPoint = Math::Vec3(1e6f);
                return box;
            case ECS::ColliderShape::Sphere:
            {
                Math::Vec3 c = body.position + body.rotation.rotate(body.center);
                box.minPoint = c - Math::Vec3(body.radius);
                box.maxPoint = c + Math::Vec3(body.radius);
                return box;
            }
            case ECS::ColliderShape::Capsule:
            {
                Math::Vec3 a, b;
                capsuleSegment(body, a, b);
                box.minPoint = Math::min(a, b) - Math::Vec3(body.radius);
                box.maxPoint = Math::max(a, b) + Math::Vec3(body.radius);
                return box;
            }
            case ECS::ColliderShape::Box:
            default:
            {
                Math::Vec3 axes[3];
                boxAxes(body, axes);
                Math::Vec3 e = axes[0] * body.half_extents.x + axes[1] * body.half_extents.y +
                               axes[2] * body.half_extents.z;
                Math::Vec3 c = body.position + body.rotation.rotate(body.center);
                box.minPoint = c - Math::abs(e);
                box.maxPoint = c + Math::abs(e);
                return box;
            }
            }
        }

        bool PhysicsSystem::aabbOverlap(const Body &a, const Body &b)
        {
            if (a.shape == ECS::ColliderShape::Plane || b.shape == ECS::ColliderShape::Plane) return true;
            Math::AABB ba = worldAABB(a), bb = worldAABB(b);
            return ba.minPoint.x <= bb.maxPoint.x && ba.maxPoint.x >= bb.minPoint.x &&
                   ba.minPoint.y <= bb.maxPoint.y && ba.maxPoint.y >= bb.minPoint.y &&
                   ba.minPoint.z <= bb.maxPoint.z && ba.maxPoint.z >= bb.minPoint.z;
        }

        void PhysicsSystem::boxAxes(const Body &body, Math::Vec3 axes[3])
        {
            axes[0] = body.rotation.rotate(Math::Vec3(1, 0, 0));
            axes[1] = body.rotation.rotate(Math::Vec3(0, 1, 0));
            axes[2] = body.rotation.rotate(Math::Vec3(0, 0, 1));
        }

        Math::Vec3 PhysicsSystem::support(const Body &body, const Math::Vec3 &dir)
        {
            if (body.shape == ECS::ColliderShape::Sphere)
                return body.position + body.rotation.rotate(body.center) + normalize(dir) * body.radius;
            if (body.shape == ECS::ColliderShape::Capsule)
            {
                Math::Vec3 a, b;
                capsuleSegment(body, a, b);
                return dot(a, dir) > dot(b, dir) ? a + normalize(dir) * body.radius : b + normalize(dir) * body.radius;
            }
            Math::Vec3 axes[3];
            boxAxes(body, axes);
            Math::Vec3 p = body.position + body.rotation.rotate(body.center);
            for (int i = 0; i < 3; ++i)
            {
                const float e = (i == 0 ? body.half_extents.x : i == 1 ? body.half_extents.y : body.half_extents.z);
                p += axes[i] * (dot(axes[i], dir) >= 0.0f ? e : -e);
            }
            return p;
        }

        void PhysicsSystem::closestSegmentPoints(const Math::Vec3 &p1, const Math::Vec3 &q1, const Math::Vec3 &p2,
                                                 const Math::Vec3 &q2, Math::Vec3 &c1, Math::Vec3 &c2)
        {
            const Math::Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
            const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
            float s = 0.0f, t = 0.0f;
            if (a <= kEpsilon && e <= kEpsilon)
            {
                c1 = p1;
                c2 = p2;
                return;
            }
            if (a <= kEpsilon)
            {
                s = 0.0f;
                t = Math::clampf(f / e, 0.0f, 1.0f);
            }
            else
            {
                const float c = dot(d1, r);
                if (e <= kEpsilon)
                {
                    t = 0.0f;
                    s = Math::clampf(-c / a, 0.0f, 1.0f);
                }
                else
                {
                    const float b = dot(d1, d2);
                    const float denom = a * e - b * b;
                    s = denom != 0.0f ? Math::clampf((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                    t = (b * s + f) / e;
                    if (t < 0.0f) { t = 0.0f; s = Math::clampf(-c / a, 0.0f, 1.0f); }
                    else if (t > 1.0f) { t = 1.0f; s = Math::clampf((b - c) / a, 0.0f, 1.0f); }
                }
            }
            c1 = p1 + d1 * s;
            c2 = p2 + d2 * t;
        }

        void PhysicsSystem::capsuleSegment(const Body &body, Math::Vec3 &a, Math::Vec3 &b)
        {
            const Math::Vec3 c = body.position + body.rotation.rotate(body.center);
            const Math::Vec3 axis = body.rotation.rotate(Math::Vec3(0, 1, 0));
            const float half = std::max(0.0f, body.height * 0.5f - body.radius);
            a = c + axis * half;
            b = c - axis * half;
        }

        bool PhysicsSystem::sphereSphere(const Math::Vec3 &ca, float ra, const Math::Vec3 &cb, float rb,
                                         Math::Vec3 &normal, float &penetration, Math::Vec3 &point)
        {
            const Math::Vec3 d = cb - ca;
            const float dist2 = Math::lengthSquared(d);
            const float sum = ra + rb;
            if (dist2 > sum * sum) return false;
            const float dist = std::sqrt(dist2);
            normal = dist > kEpsilon ? d / dist : Math::Vec3(0.0f, 1.0f, 0.0f);
            penetration = sum - dist;
            point = ca + normal * (ra - penetration * 0.5f);
            return true;
        }

        bool PhysicsSystem::sphereBox(const Math::Vec3 &center, float radius, const Body &box, Math::Vec3 &normal,
                                      float &penetration, Math::Vec3 &point)
        {
            const Math::Quat inv = box.rotation.inverse();
            const Math::Vec3 local = inv.rotate(center - (box.position + box.rotation.rotate(box.center)));
            const Math::Vec3 e = box.half_extents;
            const Math::Vec3 closest(Math::clampf(local.x, -e.x, e.x), Math::clampf(local.y, -e.y, e.y),
                                     Math::clampf(local.z, -e.z, e.z));
            const Math::Vec3 delta = local - closest;
            const float d2 = Math::lengthSquared(delta);
            if (d2 > radius * radius) return false;

            Math::Vec3 local_normal;
            if (d2 > kEpsilon * kEpsilon)
            {
                const float d = std::sqrt(d2);
                local_normal = delta / d;
                penetration = radius - d;
            }
            else
            {
                // Sphere centre is inside the box: pick the nearest face.
                const float gaps[3] = {e.x - std::fabs(local.x), e.y - std::fabs(local.y), e.z - std::fabs(local.z)};
                int axis = gaps[0] <= gaps[1] && gaps[0] <= gaps[2] ? 0 : (gaps[1] <= gaps[2] ? 1 : 2);
                local_normal = Math::Vec3(0.0f);
                local_normal[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
                penetration = radius + gaps[axis];
            }
            // Normal points from the sphere (A) to the box (B).
            normal = box.rotation.rotate(-local_normal);
            point = box.position + box.rotation.rotate(closest);
            return true;
        }

        bool PhysicsSystem::boxBox(const Body &a, const Body &b, Math::Vec3 &normal, float &penetration,
                                   Math::Vec3 &point)
        {
            Math::Vec3 ax[3], bx[3];
            boxAxes(a, ax);
            boxAxes(b, bx);
            const Math::Vec3 ac = a.position + a.rotation.rotate(a.center);
            const Math::Vec3 bc = b.position + b.rotation.rotate(b.center);
            const Math::Vec3 delta = bc - ac;
            const float ea[3] = {a.half_extents.x, a.half_extents.y, a.half_extents.z};
            const float eb[3] = {b.half_extents.x, b.half_extents.y, b.half_extents.z};

            float best = 1e30f;
            Math::Vec3 best_axis(1, 0, 0);
            auto testAxis = [&](const Math::Vec3 &axis_in)
            {
                const float len = length(axis_in);
                if (len < 1e-4f) return true;
                const Math::Vec3 axis = axis_in / len;
                float ra = 0.0f, rb = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    ra += ea[i] * std::fabs(dot(axis, ax[i]));
                    rb += eb[i] * std::fabs(dot(axis, bx[i]));
                }
                const float d = dot(axis, delta);
                const float overlap = ra + rb - std::fabs(d);
                if (overlap <= 0.0f) return false;
                if (overlap < best)
                {
                    best = overlap;
                    best_axis = d < 0.0f ? -axis : axis;
                }
                return true;
            };

            for (int i = 0; i < 3; ++i) if (!testAxis(ax[i])) return false;
            for (int i = 0; i < 3; ++i) if (!testAxis(bx[i])) return false;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (!testAxis(cross(ax[i], bx[j]))) return false;

            normal = best_axis;
            penetration = best;
            const Math::Vec3 pa = support(a, normal);
            const Math::Vec3 pb = support(b, -normal);
            point = (pa + pb) * 0.5f;
            return true;
        }

        bool PhysicsSystem::collidePlane(const Body &other, const Body &plane, int ai, int bi, Manifold &out) const
        {
            const Math::Vec3 pn = plane.rotation.rotate(Math::Vec3(0.0f, 1.0f, 0.0f));
            const Math::Vec3 origin = plane.position + plane.rotation.rotate(plane.center);
            float deepest = 1e30f;
            Math::Vec3 deepest_point = origin;
            float penetration = 0.0f;

            auto testPoint = [&](const Math::Vec3 &p)
            {
                const float d = dot(p - origin, pn);
                if (d < deepest) { deepest = d; deepest_point = p; }
            };

            switch (other.shape)
            {
            case ECS::ColliderShape::Sphere:
            {
                const Math::Vec3 c = other.position + other.rotation.rotate(other.center);
                deepest = dot(c - origin, pn) - other.radius;
                deepest_point = c - pn * other.radius;
                penetration = -deepest;
                break;
            }
            case ECS::ColliderShape::Capsule:
            {
                Math::Vec3 a, b;
                capsuleSegment(other, a, b);
                testPoint(a - pn * other.radius);
                testPoint(b - pn * other.radius);
                penetration = -deepest;
                break;
            }
            case ECS::ColliderShape::Box:
            default:
            {
                Math::Vec3 axes[3];
                boxAxes(other, axes);
                const Math::Vec3 c = other.position + other.rotation.rotate(other.center);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2)
                        for (int sz = -1; sz <= 1; sz += 2)
                            testPoint(c + axes[0] * (other.half_extents.x * sx) +
                                      axes[1] * (other.half_extents.y * sy) +
                                      axes[2] * (other.half_extents.z * sz));
                penetration = -deepest;
                break;
            }
            }

            if (penetration <= 0.0f) return false;
            // This helper is always called as (other, plane): the normal
            // points from the shape towards the plane.  Callers flip it when
            // the plane is the A side of the pair.
            (void)ai;
            (void)bi;
            out.a = ai;
            out.b = bi;
            out.normal = -pn;
            out.penetration = penetration;
            out.point = deepest_point;
            out.trigger = other.is_trigger || plane.is_trigger;
            return true;
        }

        bool PhysicsSystem::collidePair(int ai, int bi, Manifold &out) const
        {
            const Body &a = bodies_[(size_t)ai];
            const Body &b = bodies_[(size_t)bi];
            if (a.isStatic() && b.isStatic()) return false;
            if (a.type == ECS::BodyType::Kinematic && b.type == ECS::BodyType::Kinematic) return false;
            if (!a.has_collider || !b.has_collider) return false;
            if (!aabbOverlap(a, b)) return false;

            out.a = ai;
            out.b = bi;
            out.trigger = a.is_trigger || b.is_trigger;

            // Plane cases (a plane is always treated as B only when convenient,
            // but the normal direction is stored consistently).
            if (a.shape == ECS::ColliderShape::Plane && b.shape == ECS::ColliderShape::Plane) return false;
            if (b.shape == ECS::ColliderShape::Plane)
            {
                Manifold m;
                if (!collidePlane(a, b, ai, bi, m)) return false;
                out = m;
                return true;
            }
            if (a.shape == ECS::ColliderShape::Plane)
            {
                Manifold m;
                if (!collidePlane(b, a, bi, ai, m)) return false;
                // m.normal points from b (other) to a (plane); flip so it goes
                // from a to b.
                m.normal = -m.normal;
                std::swap(m.a, m.b);
                m.a = ai;
                m.b = bi;
                out = m;
                return true;
            }

            // Sphere / capsule / box dispatch.
            const ECS::ColliderShape sa = a.shape, sb = b.shape;
            if (sa == ECS::ColliderShape::Sphere && sb == ECS::ColliderShape::Sphere)
            {
                const Math::Vec3 ca = a.position + a.rotation.rotate(a.center);
                const Math::Vec3 cb = b.position + b.rotation.rotate(b.center);
                return sphereSphere(ca, a.radius, cb, b.radius, out.normal, out.penetration, out.point);
            }
            if (sa == ECS::ColliderShape::Sphere && sb == ECS::ColliderShape::Box)
            {
                const Math::Vec3 ca = a.position + a.rotation.rotate(a.center);
                return sphereBox(ca, a.radius, b, out.normal, out.penetration, out.point);
            }
            if (sa == ECS::ColliderShape::Box && sb == ECS::ColliderShape::Sphere)
            {
                const Math::Vec3 cb = b.position + b.rotation.rotate(b.center);
                if (!sphereBox(cb, b.radius, a, out.normal, out.penetration, out.point)) return false;
                out.normal = -out.normal; // sphere(A=B) -> box(A)
                return true;
            }
            if (sa == ECS::ColliderShape::Box && sb == ECS::ColliderShape::Box)
                return boxBox(a, b, out.normal, out.penetration, out.point);

            // Capsule combinations: represent the capsule by three spheres.
            auto capsuleSpheres = [](const Body &body, Math::Vec3 centers[3])
            {
                Math::Vec3 p, q;
                PhysicsSystem::capsuleSegment(body, p, q);
                centers[0] = p;
                centers[1] = (p + q) * 0.5f;
                centers[2] = q;
            };

            if (sa == ECS::ColliderShape::Capsule && sb == ECS::ColliderShape::Sphere)
            {
                Math::Vec3 centers[3];
                capsuleSpheres(a, centers);
                const Math::Vec3 cb = b.position + b.rotation.rotate(b.center);
                float best_pen = -1.0f;
                Manifold best;
                for (int i = 0; i < 3; ++i)
                {
                    Manifold m;
                    if (sphereSphere(centers[i], a.radius, cb, b.radius, m.normal, m.penetration, m.point) &&
                        m.penetration > best_pen)
                    {
                        best_pen = m.penetration;
                        best = m;
                    }
                }
                if (best_pen < 0.0f) return false;
                out.normal = best.normal;
                out.penetration = best.penetration;
                out.point = best.point;
                return true;
            }
            if (sa == ECS::ColliderShape::Sphere && sb == ECS::ColliderShape::Capsule)
            {
                // Reuse the capsule/sphere path with swapped arguments.
                Math::Vec3 centers[3];
                capsuleSpheres(b, centers);
                const Math::Vec3 ca = a.position + a.rotation.rotate(a.center);
                float best_pen = -1.0f;
                Manifold best;
                for (int i = 0; i < 3; ++i)
                {
                    Manifold m;
                    if (sphereSphere(ca, a.radius, centers[i], b.radius, m.normal, m.penetration, m.point) &&
                        m.penetration > best_pen)
                    {
                        best_pen = m.penetration;
                        best = m;
                    }
                }
                if (best_pen < 0.0f) return false;
                out.normal = best.normal;
                out.penetration = best.penetration;
                out.point = best.point;
                return true;
            }
            if (sa == ECS::ColliderShape::Capsule && sb == ECS::ColliderShape::Capsule)
            {
                Math::Vec3 ap, aq, bp, bq, c1, c2;
                capsuleSegment(a, ap, aq);
                capsuleSegment(b, bp, bq);
                closestSegmentPoints(ap, aq, bp, bq, c1, c2);
                return sphereSphere(c1, a.radius, c2, b.radius, out.normal, out.penetration, out.point);
            }
            if (sa == ECS::ColliderShape::Capsule && sb == ECS::ColliderShape::Box)
            {
                Math::Vec3 centers[3];
                capsuleSpheres(a, centers);
                float best_pen = -1.0f;
                Manifold best;
                for (int i = 0; i < 3; ++i)
                {
                    Manifold m;
                    if (sphereBox(centers[i], a.radius, b, m.normal, m.penetration, m.point) &&
                        m.penetration > best_pen)
                    {
                        best_pen = m.penetration;
                        best = m;
                    }
                }
                if (best_pen < 0.0f) return false;
                out.normal = best.normal;
                out.penetration = best.penetration;
                out.point = best.point;
                return true;
            }
            if (sa == ECS::ColliderShape::Box && sb == ECS::ColliderShape::Capsule)
            {
                Math::Vec3 centers[3];
                capsuleSpheres(b, centers);
                float best_pen = -1.0f;
                Manifold best;
                for (int i = 0; i < 3; ++i)
                {
                    Manifold m;
                    if (sphereBox(centers[i], b.radius, a, m.normal, m.penetration, m.point) &&
                        m.penetration > best_pen)
                    {
                        best_pen = m.penetration;
                        best = m;
                    }
                }
                if (best_pen < 0.0f) return false;
                out.normal = -best.normal; // sphere(capsule) -> box, flip to box -> capsule
                out.penetration = best.penetration;
                out.point = best.point;
                return true;
            }
            return false;
        }

        //==================================================================
        // Solver
        //==================================================================
        void PhysicsSystem::integrate(Body &body, float dt)
        {
            if (body.type == ECS::BodyType::Static) return;
            if (body.sleeping && body.type == ECS::BodyType::Dynamic) return;

            if (body.type == ECS::BodyType::Dynamic)
            {
                if (body.use_gravity) body.linear_velocity += settings_.gravity * (body.gravity_scale * dt);
                body.linear_velocity += body.force * (body.inv_mass * dt);
                body.angular_velocity += body.torque * (body.inv_inertia * dt);
                body.linear_velocity *= std::exp(-body.linear_damping * dt);
                body.angular_velocity *= std::exp(-body.angular_damping * dt);
            }

            body.position += body.linear_velocity * dt;
            const Math::Vec3 w = body.angular_velocity;
            if (Math::lengthSquared(w) > 1e-12f)
            {
                const Math::Quat spin(w.x, w.y, w.z, 0.0f);
                Math::Quat q = body.rotation;
                q = Math::Quat(q.x + 0.5f * dt * (spin * q).x,
                               q.y + 0.5f * dt * (spin * q).y,
                               q.z + 0.5f * dt * (spin * q).z,
                               q.w + 0.5f * dt * (spin * q).w).normalized();
                body.rotation = q;
            }
            body.force = Math::Vec3(0.0f);
            body.torque = Math::Vec3(0.0f);
        }

        void PhysicsSystem::updateSleep(Body &body, float dt)
        {
            if (body.type != ECS::BodyType::Dynamic || !settings_.sleeping) return;
            const float lv = Math::length(body.linear_velocity);
            const float av = Math::length(body.angular_velocity);
            if (lv < settings_.sleep_linear && av < settings_.sleep_angular)
            {
                body.sleep_timer += dt;
                if (body.sleep_timer > settings_.sleep_time)
                {
                    body.sleeping = true;
                    body.linear_velocity = Math::Vec3(0.0f);
                    body.angular_velocity = Math::Vec3(0.0f);
                }
            }
            else
            {
                body.sleep_timer = 0.0f;
                body.sleeping = false;
            }
        }

        void PhysicsSystem::collide(std::vector<Manifold> &out)
        {
            out.clear();
            for (size_t i = 0; i < bodies_.size(); ++i)
                for (size_t j = i + 1; j < bodies_.size(); ++j)
                {
                    Manifold m;
                    if (collidePair((int)i, (int)j, m)) out.push_back(m);
                }
        }

        void PhysicsSystem::solveContacts(std::vector<Manifold> &manifolds, float dt)
        {
            (void)dt;
            for (uint32_t it = 0; it < settings_.solver_iterations; ++it)
            {
                for (Manifold &m : manifolds)
                {
                    Body &a = bodies_[(size_t)m.a];
                    Body &b = bodies_[(size_t)m.b];
                    if (m.trigger) continue;
                    if (!a.isDynamic() && !b.isDynamic()) continue;

                    const Math::Vec3 ra = m.point - a.position;
                    const Math::Vec3 rb = m.point - b.position;
                    const Math::Vec3 va = a.linear_velocity + cross(a.angular_velocity, ra);
                    const Math::Vec3 vb = b.linear_velocity + cross(b.angular_velocity, rb);
                    const Math::Vec3 rel = vb - va;
                    const float vn = dot(rel, m.normal);
                    if (vn > 0.0f) continue;

                    const Math::Vec3 rna = cross(ra, m.normal);
                    const Math::Vec3 rnb = cross(rb, m.normal);
                    const float denom = a.inv_mass + b.inv_mass + a.inv_inertia * Math::lengthSquared(rna) +
                                        b.inv_inertia * Math::lengthSquared(rnb);
                    if (denom <= 1e-9f) continue;

                    const float e = std::fabs(vn) < 1.0f ? 0.0f : std::max(a.restitution, b.restitution);
                    const float j = -(1.0f + e) * vn / denom;
                    const Math::Vec3 impulse = m.normal * j;
                    a.linear_velocity -= impulse * a.inv_mass;
                    a.angular_velocity -= cross(ra, impulse) * a.inv_inertia;
                    b.linear_velocity += impulse * b.inv_mass;
                    b.angular_velocity += cross(rb, impulse) * b.inv_inertia;
                    m.impulse += j;

                    // Coulomb friction.
                    const Math::Vec3 va2 = a.linear_velocity + cross(a.angular_velocity, ra);
                    const Math::Vec3 vb2 = b.linear_velocity + cross(b.angular_velocity, rb);
                    const Math::Vec3 rel2 = vb2 - va2;
                    Math::Vec3 tangent = rel2 - m.normal * dot(rel2, m.normal);
                    const float tl = length(tangent);
                    if (tl > 1e-4f)
                    {
                        tangent = tangent / tl;
                        const Math::Vec3 rta = cross(ra, tangent);
                        const Math::Vec3 rtb = cross(rb, tangent);
                        const float denom_t = a.inv_mass + b.inv_mass +
                                              a.inv_inertia * Math::lengthSquared(rta) +
                                              b.inv_inertia * Math::lengthSquared(rtb);
                        if (denom_t > 1e-9f)
                        {
                            float jt = -dot(rel2, tangent) / denom_t;
                            const float mu = std::sqrt(std::max(0.0f, a.friction * b.friction));
                            jt = Math::clampf(jt, -j * mu, j * mu);
                            const Math::Vec3 ft = tangent * jt;
                            a.linear_velocity -= ft * a.inv_mass;
                            a.angular_velocity -= cross(ra, ft) * a.inv_inertia;
                            b.linear_velocity += ft * b.inv_mass;
                            b.angular_velocity += cross(rb, ft) * b.inv_inertia;
                        }
                    }

                    if (a.isDynamic()) { a.sleeping = false; a.sleep_timer = 0.0f; }
                    if (b.isDynamic()) { b.sleeping = false; b.sleep_timer = 0.0f; }
                }
            }
        }

        void PhysicsSystem::correctPositions(const std::vector<Manifold> &manifolds)
        {
            for (const Manifold &m : manifolds)
            {
                if (m.trigger) continue;
                Body &a = bodies_[(size_t)m.a];
                Body &b = bodies_[(size_t)m.b];
                const float inv_sum = a.inv_mass + b.inv_mass;
                if (inv_sum <= 1e-9f) continue;
                const float correction = std::max(m.penetration - kPositionSlop, 0.0f) * kPositionPercent / inv_sum;
                a.position -= m.normal * (correction * a.inv_mass);
                b.position += m.normal * (correction * b.inv_mass);
            }
        }

        void PhysicsSystem::solveJoints(float dt)
        {
            for (uint32_t it = 0; it < 4; ++it)
            {
                for (Hinge &j : joints_)
                {
                    const int ia = findBody(j.a);
                    if (ia < 0) continue;
                    Body &a = bodies_[(size_t)ia];
                    Body *b = nullptr;
                    if (j.b.valid())
                    {
                        const int ib = findBody(j.b);
                        if (ib >= 0) b = &bodies_[(size_t)ib];
                    }
                    if (!a.isDynamic() && (!b || !b->isDynamic())) continue;

                    const Math::Vec3 ra = j.anchor - a.position;
                    const Math::Vec3 rb = b ? j.anchor - b->position : Math::Vec3(0.0f);
                    const Math::Vec3 va = a.linear_velocity + cross(a.angular_velocity, ra);
                    const Math::Vec3 vb = b ? b->linear_velocity + cross(b->angular_velocity, rb) : Math::Vec3(0.0f);

                    // Point constraint: remove relative velocity at the anchor.
                    for (int axis_index = 0; axis_index < 3; ++axis_index)
                    {
                        const Math::Vec3 axis = axis_index == 0 ? Math::Vec3(1, 0, 0)
                                               : axis_index == 1 ? Math::Vec3(0, 1, 0)
                                                                 : Math::Vec3(0, 0, 1);
                        const float vn = dot(vb - va, axis);
                        const Math::Vec3 rxa = cross(ra, axis);
                        const Math::Vec3 rxb = cross(rb, axis);
                        const float denom = a.inv_mass + (b ? b->inv_mass : 0.0f) +
                                            a.inv_inertia * Math::lengthSquared(rxa) +
                                            (b ? b->inv_inertia * Math::lengthSquared(rxb) : 0.0f);
                        if (denom <= 1e-9f) continue;
                        const Math::Vec3 impulse = axis * (-vn / denom);
                        a.linear_velocity -= impulse * a.inv_mass;
                        a.angular_velocity -= cross(ra, impulse) * a.inv_inertia;
                        if (b)
                        {
                            b->linear_velocity += impulse * b->inv_mass;
                            b->angular_velocity += cross(rb, impulse) * b->inv_inertia;
                        }
                    }

                    // Hinge: only rotation around the axis survives.
                    Math::Vec3 rel_w = a.angular_velocity - (b ? b->angular_velocity : Math::Vec3(0.0f));
                    const Math::Vec3 radial = j.axis * dot(rel_w, j.axis);
                    const Math::Vec3 perp = rel_w - radial;
                    a.angular_velocity -= perp * 0.85f;
                    if (b) b->angular_velocity += perp * 0.85f;

                    // Angle limits and motor (evaluated once per step).
                    if (it == 0)
                    {
                        const float rate = dot(rel_w, j.axis);
                        j.current_angle += rate * dt;
                        if (j.enable_limit)
                        {
                            if (j.current_angle > j.max_angle)
                            {
                                a.angular_velocity -= j.axis * std::max(0.0f, rate);
                                j.current_angle = j.max_angle;
                            }
                            else if (j.current_angle < j.min_angle)
                            {
                                a.angular_velocity -= j.axis * std::min(0.0f, rate);
                                j.current_angle = j.min_angle;
                            }
                        }
                        if (j.enable_motor)
                        {
                            const float delta = Math::clampf(j.motor_speed - rate, -j.motor_force * dt, j.motor_force * dt);
                            a.angular_velocity += j.axis * delta;
                        }
                    }
                }
            }

        }

        void PhysicsSystem::step(ECS::World &world, float dt)
        {
            (void)world;
            const auto start = std::chrono::steady_clock::now();

            for (Body &body : bodies_) integrate(body, dt);
            std::vector<Manifold> manifolds;
            collide(manifolds);
            solveContacts(manifolds, dt);
            solveJoints(dt);
            correctPositions(manifolds);

            // Numerical safety net: never let a solver explosion poison the
            // transform hierarchy.
            for (Body &b : bodies_)
            {
                const bool bad_position = !std::isfinite(b.position.x) || !std::isfinite(b.position.y) ||
                                          !std::isfinite(b.position.z);
                const bool bad_velocity = !std::isfinite(b.linear_velocity.x) ||
                                          !std::isfinite(b.linear_velocity.y) ||
                                          !std::isfinite(b.linear_velocity.z) ||
                                          !std::isfinite(b.angular_velocity.x) ||
                                          !std::isfinite(b.angular_velocity.y) ||
                                          !std::isfinite(b.angular_velocity.z) ||
                                          !std::isfinite(b.rotation.x) || !std::isfinite(b.rotation.y) ||
                                          !std::isfinite(b.rotation.z) || !std::isfinite(b.rotation.w);
                if (bad_position || bad_velocity)
                {
                    GEN_LOG_ERROR("[Physics] recovered non-finite body '%s'",
                                  ("entity " + std::to_string(b.entity.id)).c_str());
                    b.linear_velocity = Math::Vec3(0.0f);
                    b.angular_velocity = Math::Vec3(0.0f);
                    if (bad_position || !std::isfinite(b.position.x)) b.position = Math::Vec3(0.0f);
                    b.rotation = Math::Quat::identity();
                    b.sleeping = true;
                }
            }

            contacts_.clear();
            trigger_count_ = 0;
            for (const Manifold &m : manifolds)
            {
                Contact c;
                c.a = bodies_[(size_t)m.a].entity;
                c.b = bodies_[(size_t)m.b].entity;
                c.point = m.point;
                c.normal = m.normal;
                c.penetration = m.penetration;
                c.impulse = m.impulse;
                c.trigger = m.trigger;
                if (m.trigger) ++trigger_count_;
                contacts_.push_back(c);
            }

            for (Body &body : bodies_) updateSleep(body, dt);

            const auto end = std::chrono::steady_clock::now();
            step_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
            ++step_count_;
        }

        void PhysicsSystem::writeBack(ECS::World &world)
        {
            for (const Body &body : bodies_)
            {
                if (!body.isDynamic() && body.type != ECS::BodyType::Kinematic) continue;
                ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(body.entity);
                if (!t) continue;
                t->position = body.position;
                t->rotation = body.rotation;
                t->local_dirty = true;
                if (ECS::RigidBodyComponent *rb = world.getComponent<ECS::RigidBodyComponent>(body.entity))
                {
                    rb->linear_velocity = body.linear_velocity;
                    rb->angular_velocity = body.angular_velocity;
                    rb->sleeping = body.sleeping;
                    rb->sleep_timer = body.sleep_timer;
                }
            }
            // Hinge runtime angles are visible to scripts / the inspector.
            for (const Hinge &j : joints_)
                if (ECS::HingeJointComponent *jc = world.getComponent<ECS::HingeJointComponent>(j.a))
                    jc->current_angle = j.current_angle;
            (void)world;
        }

        void PhysicsSystem::update(ECS::World &world, float delta_time)
        {
            if (!settings_.enabled) return;
            rebuildBodies(world);
            rebuildJoints(world);

            if (!paused_)
            {
                accumulator_ += std::min(delta_time, 0.25f);
                uint32_t steps = 0;
                while (accumulator_ >= settings_.fixed_timestep && steps < settings_.max_substeps)
                {
                    step(world, settings_.fixed_timestep);
                    accumulator_ -= settings_.fixed_timestep;
                    ++steps;
                }
                // Do not let a long stall build an unbounded backlog.
                if (steps == settings_.max_substeps) accumulator_ = 0.0f;
            }

            writeBack(world);
        }

        //==================================================================
        bool PhysicsSystem::applyImpulse(ECS::World &world, ECS::Entity entity, const Math::Vec3 &impulse,
                                         const Math::Vec3 &world_point)
        {
            rebuildBodies(world);
            const int index = findBody(entity);
            if (index < 0) return false;
            Body &body = bodies_[(size_t)index];
            if (!body.isDynamic()) return false;
            body.sleeping = false;
            body.sleep_timer = 0.0f;
            body.linear_velocity += impulse * body.inv_mass;
            const Math::Vec3 r = world_point - body.position;
            body.angular_velocity += cross(r, impulse) * body.inv_inertia;
            if (ECS::RigidBodyComponent *rb = world.getComponent<ECS::RigidBodyComponent>(entity))
            {
                rb->linear_velocity = body.linear_velocity;
                rb->angular_velocity = body.angular_velocity;
                rb->sleeping = false;
            }
            return true;
        }

        bool PhysicsSystem::rayCast(ECS::World &world, const Math::Vec3 &origin, const Math::Vec3 &direction,
                                    float max_distance, RayHit &out) const
        {
            (void)world;
            const Math::Vec3 dir = Math::normalize(direction);
            float best = max_distance;
            bool hit = false;
            for (const Body &body : bodies_)
            {
                if (body.is_trigger || body.shape == ECS::ColliderShape::Plane) continue;
                if (body.shape == ECS::ColliderShape::Sphere)
                {
                    const Math::Vec3 c = body.position + body.rotation.rotate(body.center);
                    const Math::Vec3 m = origin - c;
                    const float b = dot(m, dir);
                    const float cc = dot(m, m) - body.radius * body.radius;
                    const float disc = b * b - cc;
                    if (disc < 0.0f) continue;
                    const float t = -b - std::sqrt(disc);
                    if (t >= 0.0f && t < best)
                    {
                        best = t;
                        hit = true;
                        out.entity = body.entity;
                        out.point = origin + dir * t;
                        out.normal = normalize(out.point - c);
                        out.distance = t;
                    }
                }
                else if (body.shape == ECS::ColliderShape::Box)
                {
                    const Math::Vec3 local_o = body.rotation.inverse().rotate(origin - body.position);
                    const Math::Vec3 local_d = body.rotation.inverse().rotate(dir);
                    const Math::Vec3 e = body.half_extents + body.center;
                    float tmin = 0.0f, tmax = best;
                    int axis = -1;
                    float sign = 1.0f;
                    bool ok = true;
                    for (int i = 0; i < 3; ++i)
                    {
                        const float d = local_d[i];
                        const float o = local_o[i];
                        if (std::fabs(d) < 1e-6f)
                        {
                            if (o < -e[i] || o > e[i]) { ok = false; break; }
                        }
                        else
                        {
                            float t1 = (-e[i] - o) / d;
                            float t2 = (e[i] - o) / d;
                            float s = -1.0f;
                            if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
                            if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
                            if (t2 < tmax) tmax = t2;
                            if (tmin > tmax) { ok = false; break; }
                        }
                    }
                    if (ok && tmin < best)
                    {
                        best = tmin;
                        hit = true;
                        out.entity = body.entity;
                        out.point = origin + dir * tmin;
                        Math::Vec3 ln(0.0f);
                        if (axis >= 0) ln[axis] = sign;
                        out.normal = body.rotation.rotate(ln);
                        out.distance = tmin;
                    }
                }
            }
            return hit;
        }

        void PhysicsSystem::drawDebug(Render::RenderSystem &render) const
        {
            for (const Body &body : bodies_)
            {
                const Math::Color color = body.type == ECS::BodyType::Static ? Math::Color(0.35f, 0.8f, 1.0f, 1.0f)
                                           : body.type == ECS::BodyType::Kinematic ? Math::Color(1.0f, 0.8f, 0.2f, 1.0f)
                                                                                 : Math::Color(0.3f, 1.0f, 0.55f, 1.0f);
                switch (body.shape)
                {
                case ECS::ColliderShape::Sphere:
                {
                    const Math::Vec3 c = body.position + body.rotation.rotate(body.center);
                    render.drawSphere(c, body.radius, color, 20);
                    break;
                }
                case ECS::ColliderShape::Capsule:
                {
                    Math::Vec3 a, b;
                    capsuleSegment(body, a, b);
                    render.drawLine(a, b, color);
                    render.drawSphere(a, body.radius, color, 12);
                    render.drawSphere(b, body.radius, color, 12);
                    break;
                }
                case ECS::ColliderShape::Plane:
                {
                    const Math::Vec3 n = body.rotation.rotate(Math::Vec3(0, 1, 0));
                    const Math::Vec3 o = body.position;
                    Math::Vec3 t1 = normalize(cross(std::fabs(n.y) < 0.9f ? Math::Vec3(0, 1, 0) : Math::Vec3(1, 0, 0), n));
                    Math::Vec3 t2 = cross(n, t1);
                    const float s = 6.0f;
                    for (int i = -5; i <= 5; ++i)
                    {
                        const float f = (float)i / 5.0f * s;
                        render.drawLine(o + t1 * f - t2 * s, o + t1 * f + t2 * s, Math::Color(0.2f, 0.45f, 0.8f, 0.7f));
                        render.drawLine(o + t2 * f - t1 * s, o + t2 * f + t1 * s, Math::Color(0.2f, 0.45f, 0.8f, 0.7f));
                    }
                    break;
                }
                case ECS::ColliderShape::Box:
                default:
                {
                    Math::Vec3 axes[3];
                    boxAxes(body, axes);
                    const Math::Vec3 c = body.position + body.rotation.rotate(body.center);
                    Math::Vec3 p[8];
                    for (int i = 0; i < 8; ++i)
                    {
                        p[i] = c + axes[0] * ((i & 1) ? body.half_extents.x : -body.half_extents.x) +
                               axes[1] * ((i & 2) ? body.half_extents.y : -body.half_extents.y) +
                               axes[2] * ((i & 4) ? body.half_extents.z : -body.half_extents.z);
                    }
                    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                    for (const auto &e : edges) render.drawLine(p[e[0]], p[e[1]], color);
                    break;
                }
                }
            }
            for (const Hinge &j : joints_)
                render.drawLine(j.anchor - j.axis * 0.6f, j.anchor + j.axis * 0.6f,
                                Math::Color(1.0f, 0.5f, 0.15f, 1.0f));
        }

    } // namespace Physics
} // namespace Genesis
