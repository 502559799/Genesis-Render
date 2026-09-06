/*
 * Genesis Render - Engine
 * scene.cpp
 */
#include "scene.hpp"
#include "foundation_layer/log.hpp"
#include <algorithm>
#include <stack>

namespace Genesis
{
Scene::Scene() {}
Scene::~Scene() = default;

Entity Scene::createEntity(const std::string &name)
{
    uint32_t idx;
    if (!free_indices_.empty())
    {
        idx = free_indices_.back();
        free_indices_.pop_back();
    }
    else
    {
        idx = (uint32_t)records_.size();
        records_.emplace_back();
    }
    Record &r = records_[idx];
    r.alive = true;
    r.name = name;
    ++alive_count_;
    Entity e{idx, r.generation};
    addComponent<NameComponent>(e, NameComponent{name});
    addComponent<TransformComponent>(e, TransformComponent{});
    getComponent<TransformComponent>(e)->world = Mat4::identity();
    return e;
}

bool Scene::isValid(Entity e) const
{
    return e.valid() && e.index < records_.size() && records_[e.index].alive &&
           records_[e.index].generation == e.generation;
}

void Scene::destroyEntity(Entity e)
{
    if (!isValid(e)) return;
    detachFromParent(e);
    if (auto *t = getComponent<TransformComponent>(e))
    {
        // reparent children to grandparent then destroy recursively
        auto kids = t->children;
        for (auto c : kids)
            if (isValid(c)) destroyEntity(c);
    }
    for (auto &[ti, pool] : pools_) pool->remove(e.index);
    records_[e.index].alive = false;
    records_[e.index].generation++;
    free_indices_.push_back(e.index);
    --alive_count_;
}

const std::string &Scene::entityName(Entity e) const
{
    static const std::string empty;
    if (!isValid(e)) return empty;
    return records_[e.index].name;
}
void Scene::setEntityName(Entity e, const std::string &name)
{
    if (!isValid(e)) return;
    records_[e.index].name = name;
    if (auto *n = getComponent<NameComponent>(e)) n->name = name;
}
Entity Scene::findEntityByName(const std::string &name) const
{
    for (uint32_t i = 0; i < records_.size(); ++i)
        if (records_[i].alive && records_[i].name == name)
            return {i, records_[i].generation};
    return kInvalidEntity;
}
std::vector<Entity> Scene::allEntities() const
{
    std::vector<Entity> out;
    for (uint32_t i = 0; i < records_.size(); ++i)
        if (records_[i].alive) out.push_back({i, records_[i].generation});
    return out;
}

void Scene::attachChild(Entity parent, Entity child)
{
    if (!isValid(parent) || !isValid(child)) return;
    detachFromParent(child);
    auto *pt = getComponent<TransformComponent>(parent);
    auto *ct = getComponent<TransformComponent>(child);
    ct->parent = parent;
    pt->children.push_back(child);
    ct->dirty = true;
}
void Scene::detachFromParent(Entity child)
{
    auto *ct = getComponent<TransformComponent>(child);
    if (!ct || !ct->parent.valid()) return;
    auto *pt = getComponent<TransformComponent>(ct->parent);
    if (pt)
    {
        auto &v = pt->children;
        v.erase(std::remove(v.begin(), v.end(), child), v.end());
    }
    ct->parent = kInvalidEntity;
}

std::vector<Entity> Scene::roots() const
{
    std::vector<Entity> out;
    for (uint32_t i = 0; i < records_.size(); ++i)
        if (records_[i].alive)
        {
            const auto *t = getComponent<TransformComponent>(Entity{i, records_[i].generation});
            if (!t || !t->parent.valid()) out.push_back({i, records_[i].generation});
        }
    return out;
}

Vec3 Scene::worldPosition(Entity e) const
{
    auto *t = getComponent<TransformComponent>(e);
    if (!t) return Vec3(0);
    return Vec3(t->world.m[12], t->world.m[13], t->world.m[14]);
}

void Scene::addSystem(std::unique_ptr<System> sys)
{
    sys->onAttach(this);
    systems_.push_back(std::move(sys));
}

void Scene::update(float dt)
{
    updateTransforms();
    for (auto &s : systems_) s->update(this, dt);
}

void Scene::updateTransforms()
{
    // iterative depth walk from roots, composing parent world matrices
    struct Frame { Entity e; Mat4 parent; };
    std::vector<Frame> stack;
    for (Entity r : roots())
        stack.push_back({r, Mat4::identity()});
    while (!stack.empty())
    {
        Frame f = stack.back();
        stack.pop_back();
        if (!isValid(f.e)) continue;
        TransformComponent *t = getComponent<TransformComponent>(f.e);
        if (!t) continue;
        Mat4 local = composeTRS(t->position, t->rotation, t->scale);
        t->local = local;
        t->world = f.parent * local;
        t->normal_matrix = normalMat3(t->world);
        t->dirty = false;
        for (Entity c : t->children) stack.push_back({c, t->world});
    }
}
}
