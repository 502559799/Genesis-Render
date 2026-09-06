/*
 * Genesis Render - Engine
 * scene.hpp : entity registry + component pools + hierarchy + systems.
 */
#pragma once

#include "entity.hpp"
#include "component_pool.hpp"
#include "components.hpp"
#include "math/math.hpp"

#include <typeindex>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace Genesis
{
    class Scene;

    class System
    {
    public:
        virtual ~System() = default;
        virtual void onAttach(Scene *) {}
        virtual void update(Scene *scene, float dt) = 0;
        std::string name;
    };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        Entity createEntity(const std::string &name = "Entity");
        bool isValid(Entity e) const;
        void destroyEntity(Entity e);
        size_t entityCount() const { return alive_count_; }

        const std::string &entityName(Entity e) const;
        void setEntityName(Entity e, const std::string &name);
        Entity findEntityByName(const std::string &name) const;
        std::vector<Entity> allEntities() const;

        template <typename T>
        T &addComponent(Entity e, T value = T{})
        {
            return pool<T>().add(e.index, std::move(value));
        }
        template <typename T>
        T *getComponent(Entity e)
        {
            return isValid(e) ? pool<T>().get(e.index) : nullptr;
        }
        template <typename T>
        const T *getComponent(Entity e) const
        {
            const auto *p = findPool<T>();
            return isValid(e) && p ? p->get(e.index) : nullptr;
        }
        template <typename T>
        bool hasComponent(Entity e) const
        {
            const auto *p = findPool<T>();
            return isValid(e) && p && p->has(e.index);
        }
        template <typename T>
        void removeComponent(Entity e)
        {
            if (auto *p = findPool<T>()) p->remove(e.index);
        }
        template <typename T, typename Fn>
        void view(Fn fn)
        {
            auto *p = findPool<T>();
            if (!p) return;
            for (uint32_t idx : p->order())
            {
                if (!records_[idx].alive) continue;
                Entity e{idx, records_[idx].generation};
                fn(e, *p->get(idx));
            }
        }
        // iterate entities that own all listed components
        template <typename T1, typename T2, typename... TRest, typename Fn>
        void view(Fn fn)
        {
            auto *p = findPool<T1>();
            if (!p) return;
            for (uint32_t idx : p->order())
            {
                if (!records_[idx].alive) continue;
                Entity e{idx, records_[idx].generation};
                bool all = hasComponent<T2>(e) &&
                           (hasComponent<TRest>(e) && ...);
                if (all)
                    fn(e, *p->get(idx), *getComponent<T2>(e),
                       *getComponent<TRest>(e)...);
            }
        }

        // hierarchy
        void attachChild(Entity parent, Entity child);
        void detachFromParent(Entity child);
        std::vector<Entity> roots() const;
        Vec3 worldPosition(Entity e) const;

        // systems
        void addSystem(std::unique_ptr<System> sys);
        void update(float dt);

        // transform world matrices (also invoked by TransformSystem each tick)
        void updateTransforms();

    private:
        template <typename T>
        ComponentPool<T> &pool()
        {
            auto ti = std::type_index(typeid(T));
            auto it = pools_.find(ti);
            if (it == pools_.end())
            {
                auto p = std::make_unique<ComponentPool<T>>();
                auto *raw = p.get();
                pools_.emplace(ti, std::move(p));
                return *raw;
            }
            return *static_cast<ComponentPool<T> *>(it->second.get());
        }
        template <typename T>
        ComponentPool<T> *findPool()
        {
            auto it = pools_.find(std::type_index(typeid(T)));
            return it == pools_.end() ? nullptr
                                      : static_cast<ComponentPool<T> *>(it->second.get());
        }
        template <typename T>
        const ComponentPool<T> *findPool() const
        {
            auto it = pools_.find(std::type_index(typeid(T)));
            return it == pools_.end() ? nullptr
                                      : static_cast<const ComponentPool<T> *>(it->second.get());
        }

        struct Record
        {
            uint32_t generation = 0;
            bool alive = false;
            std::string name;
        };
        std::vector<Record> records_;
        std::vector<uint32_t> free_indices_;
        size_t alive_count_ = 0;
        std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
        std::vector<std::unique_ptr<System>> systems_;
    };
}
