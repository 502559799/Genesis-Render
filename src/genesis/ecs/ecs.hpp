/*
 * Genesis Render - ECS (Entity Component System)
 *
 * Design
 * ------
 * A classic *sparse-set* ECS (not the GameObject/Component tree that Piccolo
 * uses, and not a full archetype engine either).  Rationale:
 *
 *   - Each component type owns a `ComponentPool<T>` which stores components
 *     densely (cache friendly iteration) and maps EntityId -> dense index
 *     through a sparse array (O(1) add/get/remove).
 *   - Removal is O(1): the last dense element is swapped into the hole.
 *   - Entities are (id, generation) handles so stale handles can be detected.
 *   - Hierarchies (parent/child) live in the World, not in the transform
 *     component, so the TransformSystem can do a single top-down pass.
 *   - Systems are plain objects with `update(World&, float dt)` and a stable
 *     integer order; the scheduler sorts them once.
 *
 * Compared with Piccolo:
 *   - Piccolo: GameObject holds `std::vector<ReflectionPtr<Component>>` and
 *     lookups are O(n) string comparisons; there is no system scheduler.
 *   - Genesis: typed pools, O(1) lookup, explicit ordered systems, and
 *     `world.each<T>()` / `world.view<A,B>()` iteration helpers.
 */
#ifndef __GENESIS_ECS_HPP__
#define __GENESIS_ECS_HPP__

#include "foundation_layer/genesis_log.hpp"
#include "foundation_layer/math/math.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace ECS
    {
        using EntityId = uint32_t;
        static constexpr EntityId kInvalidEntityId = 0xFFFFFFFFu;
        static constexpr uint32_t kInvalidDenseIndex = 0xFFFFFFFFu;

        //------------------------------------------------------------------
        // Entity handle: id + generation, cheap to copy and compare.
        //------------------------------------------------------------------
        struct Entity
        {
            EntityId id = kInvalidEntityId;
            uint32_t generation = 0;

            bool valid() const { return id != kInvalidEntityId; }
            bool operator==(const Entity &o) const { return id == o.id && generation == o.generation; }
            bool operator!=(const Entity &o) const { return !(*this == o); }
            bool operator<(const Entity &o) const { return id < o.id; }
        };

        static inline Entity nullEntity() { return Entity{kInvalidEntityId, 0}; }

        //------------------------------------------------------------------
        // Component pool interface (type erased, used by World for cleanup)
        //------------------------------------------------------------------
        class IComponentPool
        {
        public:
            virtual ~IComponentPool() {}
            virtual const char *typeName() const = 0;
            virtual size_t count() const = 0;
            virtual void removeEntity(EntityId id) = 0;
            virtual void clear() = 0;
            virtual bool has(EntityId id) const = 0;
        };

        //------------------------------------------------------------------
        // Sparse-set component pool
        //------------------------------------------------------------------
        template <typename T>
        class ComponentPool : public IComponentPool
        {
        public:
            explicit ComponentPool(size_t capacity = 256)
            {
                sparse_.resize(capacity, kInvalidDenseIndex);
            }

            const char *typeName() const override { return T::kComponentName ? T::kComponentName : "component"; }
            size_t count() const override { return dense_.size(); }

            bool has(EntityId id) const override
            {
                return id < (EntityId)sparse_.size() && sparse_[id] != kInvalidDenseIndex;
            }

            T *get(EntityId id)
            {
                if (id >= (EntityId)sparse_.size()) return nullptr;
                uint32_t index = sparse_[id];
                return index == kInvalidDenseIndex ? nullptr : &dense_[index];
            }

            const T *get(EntityId id) const
            {
                if (id >= (EntityId)sparse_.size()) return nullptr;
                uint32_t index = sparse_[id];
                return index == kInvalidDenseIndex ? nullptr : &dense_[index];
            }

            T *add(EntityId id, const T &value = T())
            {
                if (has(id)) return get(id);
                if (id >= (EntityId)sparse_.size()) sparse_.resize((size_t)id + 1, kInvalidDenseIndex);
                dense_.push_back(value);
                entities_.push_back(id);
                sparse_[id] = (uint32_t)(dense_.size() - 1);
                return &dense_.back();
            }

            void removeEntity(EntityId id) override
            {
                if (!has(id)) return;
                uint32_t index = sparse_[id];
                uint32_t last = (uint32_t)dense_.size() - 1;
                if (index != last)
                {
                    dense_[index] = std::move(dense_[last]);
                    entities_[index] = entities_[last];
                    sparse_[entities_[index]] = index;
                }
                dense_.pop_back();
                entities_.pop_back();
                sparse_[id] = kInvalidDenseIndex;
            }

            void clear() override
            {
                for (EntityId e : entities_)
                    if (e < (EntityId)sparse_.size()) sparse_[e] = kInvalidDenseIndex;
                dense_.clear();
                entities_.clear();
            }

            // Iteration accessors
            T *data() { return dense_.empty() ? nullptr : dense_.data(); }
            const T *data() const { return dense_.empty() ? nullptr : dense_.data(); }
            const std::vector<EntityId> &entities() const { return entities_; }
            std::vector<T> &components() { return dense_; }
            const std::vector<T> &components() const { return dense_; }

        private:
            std::vector<T> dense_;
            std::vector<EntityId> entities_;
            std::vector<uint32_t> sparse_;
        };

        //------------------------------------------------------------------
        // Systems
        //------------------------------------------------------------------
        class World;

        class System
        {
        public:
            virtual ~System() {}
            virtual const char *name() const = 0;
            // Lower order runs earlier.  Equal orders keep insertion order.
            virtual int32_t order() const { return 0; }
            virtual void update(World &world, float delta_time) = 0;
            bool enabled = true;
            double last_duration_ms = 0.0;
        };

        class SystemScheduler
        {
        public:
            void add(std::unique_ptr<System> system)
            {
                systems_.push_back(std::move(system));
                sorted_ = false;
            }

            template <typename S, typename... Args>
            S *addSystem(Args &&...args)
            {
                auto owned = std::make_unique<S>(std::forward<Args>(args)...);
                S *raw = owned.get();
                add(std::move(owned));
                return raw;
            }

            System *find(const char *system_name) const
            {
                for (const auto &s : systems_)
                    if (std::string(s->name()) == system_name) return s.get();
                return nullptr;
            }

            void sortSystems()
            {
                if (sorted_) return;
                std::stable_sort(systems_.begin(), systems_.end(),
                                 [](const std::unique_ptr<System> &a, const std::unique_ptr<System> &b)
                                 { return a->order() < b->order(); });
                sorted_ = true;
            }

            void update(World &world, float delta_time);

            size_t count() const { return systems_.size(); }
            const std::vector<std::unique_ptr<System>> &systems() const { return systems_; }

        private:
            std::vector<std::unique_ptr<System>> systems_;
            bool sorted_ = false;
        };

        //------------------------------------------------------------------
        // Hierarchy record kept per entity
        //------------------------------------------------------------------
        struct EntityRecord
        {
            uint32_t generation = 0;
            bool alive = false;
            std::string name;
            EntityId parent = kInvalidEntityId;
            std::vector<EntityId> children;
            uint32_t flags = 0;
        };

        //------------------------------------------------------------------
        // World
        //------------------------------------------------------------------
        class World
        {
        public:
            explicit World(const std::string &name = "world") : name_(name) {}
            ~World() { clear(); }

            const std::string &name() const { return name_; }

            // ---- entity lifecycle -------------------------------------
            Entity createEntity(const std::string &entity_name = "entity")
            {
                EntityId id;
                if (!free_ids_.empty())
                {
                    id = free_ids_.back();
                    free_ids_.pop_back();
                }
                else
                {
                    id = (EntityId)records_.size();
                    records_.push_back(EntityRecord());
                }
                EntityRecord &rec = records_[id];
                rec.alive = true;
                rec.name = entity_name;
                rec.parent = kInvalidEntityId;
                rec.children.clear();
                rec.flags = 0;
                ++alive_count_;
                return Entity{id, rec.generation};
            }

            void destroyEntity(Entity e)
            {
                if (!isAlive(e)) return;
                EntityRecord &rec = records_[e.id];

                // Destroy children first (depth first).
                std::vector<EntityId> children = rec.children;
                for (EntityId child : children)
                {
                    if (child < (EntityId)records_.size() && records_[child].alive)
                        destroyEntity(Entity{child, records_[child].generation});
                }
                rec.children.clear();

                // Detach from parent.
                if (rec.parent != kInvalidEntityId && rec.parent < (EntityId)records_.size())
                {
                    auto &siblings = records_[rec.parent].children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), e.id), siblings.end());
                }
                rec.parent = kInvalidEntityId;

                for (auto &kv : pools_) kv.second->removeEntity(e.id);

                rec.alive = false;
                ++rec.generation;
                free_ids_.push_back(e.id);
                --alive_count_;
            }

            bool isAlive(Entity e) const
            {
                if (e.id >= (EntityId)records_.size()) return false;
                const EntityRecord &rec = records_[e.id];
                return rec.alive && rec.generation == e.generation;
            }

            size_t entityCount() const { return alive_count_; }

            const std::string &entityName(Entity e) const
            {
                static const std::string k_empty;
                return isAlive(e) ? records_[e.id].name : k_empty;
            }
            void setEntityName(Entity e, const std::string &n)
            {
                if (isAlive(e)) records_[e.id].name = n;
            }
            // First alive entity with the given name (nullEntity() if none).
            Entity findEntity(const std::string &n) const
            {
                for (size_t i = 0; i < records_.size(); ++i)
                    if (records_[i].alive && records_[i].name == n) return Entity{(EntityId)i, records_[i].generation};
                return nullEntity();
            }

            // Rebuild a handle from a raw id (used by the scripting layer).
            Entity entityFromId(EntityId id) const
            {
                if (id >= (EntityId)records_.size() || !records_[id].alive) return nullEntity();
                return Entity{id, records_[id].generation};
            }

            // Enumerate all alive entities (ids in ascending order).
            std::vector<Entity> entities() const
            {
                std::vector<Entity> out;
                out.reserve(alive_count_);
                for (size_t i = 0; i < records_.size(); ++i)
                    if (records_[i].alive) out.push_back(Entity{(EntityId)i, records_[i].generation});
                return out;
            }

            // ---- hierarchy --------------------------------------------
            void setParent(Entity child, Entity parent, bool keep_world_transform = false)
            {
                (void)keep_world_transform;
                if (!isAlive(child)) return;
                if (parent.valid() && !isAlive(parent)) return;
                if (child == parent) return;
                // Prevent cycles
                for (EntityId p = records_[child.id].parent; p != kInvalidEntityId;)
                {
                    if (p == child.id) return;
                    p = (p < (EntityId)records_.size()) ? records_[p].parent : kInvalidEntityId;
                }
                EntityId old_parent = records_[child.id].parent;
                if (old_parent != kInvalidEntityId && old_parent < (EntityId)records_.size())
                {
                    auto &siblings = records_[old_parent].children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), child.id), siblings.end());
                }
                records_[child.id].parent = parent.valid() ? parent.id : kInvalidEntityId;
                if (parent.valid()) records_[parent.id].children.push_back(child.id);
            }

            Entity parentOf(Entity e) const
            {
                if (!isAlive(e)) return nullEntity();
                EntityId p = records_[e.id].parent;
                if (p == kInvalidEntityId || p >= (EntityId)records_.size()) return nullEntity();
                return Entity{p, records_[p].generation};
            }

            std::vector<Entity> childrenOf(Entity e) const
            {
                std::vector<Entity> out;
                if (!isAlive(e)) return out;
                for (EntityId c : records_[e.id].children)
                    if (c < (EntityId)records_.size() && records_[c].alive)
                        out.push_back(Entity{c, records_[c].generation});
                return out;
            }

            // Depth of an entity in the hierarchy (root == 0).
            uint32_t depth(Entity e) const
            {
                uint32_t d = 0;
                if (!isAlive(e)) return 0;
                EntityId p = records_[e.id].parent;
                while (p != kInvalidEntityId && p < (EntityId)records_.size() && d < 1024)
                {
                    ++d;
                    p = records_[p].parent;
                }
                return d;
            }

            // ---- components -------------------------------------------
            template <typename T>
            ComponentPool<T> *pool()
            {
                std::type_index key(typeid(T));
                auto it = pools_.find(key);
                if (it != pools_.end()) return static_cast<ComponentPool<T> *>(it->second.get());
                auto created = std::make_unique<ComponentPool<T>>();
                ComponentPool<T> *raw = created.get();
                pools_.emplace(key, std::move(created));
                pool_order_.push_back(key);
                return raw;
            }

            template <typename T>
            const ComponentPool<T> *pool() const
            {
                std::type_index key(typeid(T));
                auto it = pools_.find(key);
                if (it == pools_.end()) return nullptr;
                return static_cast<const ComponentPool<T> *>(it->second.get());
            }

            template <typename T>
            T &addComponent(Entity e, const T &value = T())
            {
                T *existing = getComponent<T>(e);
                if (existing)
                {
                    *existing = value;
                    return *existing;
                }
                if (!isAlive(e))
                {
                    GEN_LOG_ERROR("[ECS] addComponent<%s> on a dead entity", T::kComponentName);
                    static T fallback = value;
                    return fallback;
                }
                return *pool<T>()->add(e.id, value);
            }

            template <typename T>
            T *getComponent(Entity e)
            {
                if (!isAlive(e)) return nullptr;
                return pool<T>()->get(e.id);
            }

            template <typename T>
            const T *getComponent(Entity e) const
            {
                if (!isAlive(e)) return nullptr;
                const ComponentPool<T> *p = pool<T>();
                return p ? p->get(e.id) : nullptr;
            }

            template <typename T>
            bool hasComponent(Entity e) const
            {
                if (!isAlive(e)) return false;
                const ComponentPool<T> *p = pool<T>();
                return p && p->has(e.id);
            }

            template <typename T>
            void removeComponent(Entity e)
            {
                if (!isAlive(e)) return;
                ComponentPool<T> *p = pool<T>();
                if (p) p->removeEntity(e.id);
            }

            // Iterate every entity that has T.
            template <typename T, typename Fn>
            void each(Fn &&fn)
            {
                ComponentPool<T> *p = pool<T>();
                if (!p) return;
                const std::vector<EntityId> &ids = p->entities();
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    EntityId id = ids[i];
                    Entity e{id, id < (EntityId)records_.size() ? records_[id].generation : 0};
                    fn(e, p->components()[i]);
                }
            }

            // Iterate the intersection of A and B (driven by the smaller pool).
            template <typename A, typename B, typename Fn>
            void view(Fn &&fn)
            {
                ComponentPool<A> *pa = pool<A>();
                ComponentPool<B> *pb = pool<B>();
                if (!pa || !pb) return;
                if (pa->count() <= pb->count())
                {
                    const auto &ids = pa->entities();
                    for (size_t i = 0; i < ids.size(); ++i)
                    {
                        B *b = pb->get(ids[i]);
                        if (!b) continue;
                        Entity e{ids[i], ids[i] < (EntityId)records_.size() ? records_[ids[i]].generation : 0};
                        fn(e, pa->components()[i], *b);
                    }
                }
                else
                {
                    const auto &ids = pb->entities();
                    for (size_t i = 0; i < ids.size(); ++i)
                    {
                        A *a = pa->get(ids[i]);
                        if (!a) continue;
                        Entity e{ids[i], ids[i] < (EntityId)records_.size() ? records_[ids[i]].generation : 0};
                        fn(e, *a, pb->components()[i]);
                    }
                }
            }

            template <typename A, typename B, typename C, typename Fn>
            void view(Fn &&fn)
            {
                ComponentPool<A> *pa = pool<A>();
                if (!pa) return;
                const auto &ids = pa->entities();
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    B *b = getPoolIfExists<B>()->get(ids[i]);
                    if (!b) continue;
                    C *c = getPoolIfExists<C>()->get(ids[i]);
                    if (!c) continue;
                    Entity e{ids[i], ids[i] < (EntityId)records_.size() ? records_[ids[i]].generation : 0};
                    fn(e, pa->components()[i], *b, *c);
                }
            }

            // ---- systems ----------------------------------------------
            SystemScheduler &scheduler() { return scheduler_; }
            void update(float delta_time) { scheduler_.update(*this, delta_time); }

            // ---- misc --------------------------------------------------
            void clear()
            {
                for (auto &kv : pools_) kv.second->clear();
                pools_.clear();
                pool_order_.clear();
                records_.clear();
                free_ids_.clear();
                alive_count_ = 0;
            }

            size_t poolCount() const { return pools_.size(); }
            std::vector<std::pair<std::string, size_t>> poolStats() const
            {
                std::vector<std::pair<std::string, size_t>> out;
                for (const auto &key : pool_order_)
                {
                    auto it = pools_.find(key);
                    if (it == pools_.end()) continue;
                    out.push_back({it->second->typeName(), it->second->count()});
                }
                return out;
            }

        private:
            template <typename T>
            ComponentPool<T> *getPoolIfExists()
            {
                std::type_index key(typeid(T));
                auto it = pools_.find(key);
                if (it != pools_.end()) return static_cast<ComponentPool<T> *>(it->second.get());
                return pool<T>();
            }

            std::string name_;
            std::vector<EntityRecord> records_;
            std::vector<EntityId> free_ids_;
            size_t alive_count_ = 0;
            std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
            std::vector<std::type_index> pool_order_;
            SystemScheduler scheduler_;
        };

        //------------------------------------------------------------------
        // Inline scheduler update (needs the complete World type)
        //------------------------------------------------------------------
        inline void SystemScheduler::update(World &world, float delta_time)
        {
            sortSystems();
            for (auto &system : systems_)
            {
                if (!system->enabled) continue;
                auto start = std::chrono::steady_clock::now();
                system->update(world, delta_time);
                auto end = std::chrono::steady_clock::now();
                system->last_duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            }
        }

    } // namespace ECS

    using ECS::Entity;
    using ECS::World;
} // namespace Genesis

#endif /* __GENESIS_ECS_HPP__ */
