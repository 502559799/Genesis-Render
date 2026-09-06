/*
 * @Descripttion: ECS 核心: Entity / ComponentPool(稀疏集) / World / System
 *               对齐 Piccolo 的 World-GObject-Component 概念,
 *               采用数据导向的稀疏集(sparse set)存储替代其对象组合模式
 * @version: v1.0
 */
#ifndef __GENESIS_ECS_H__
#define __GENESIS_ECS_H__

#include "foundation_layer/platform.hpp"
#include "foundation_layer/log.hpp"

#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace ecs
    {
        typedef uint32_t Entity;
        static const Entity kInvalidEntity = 0xffffffff;

        //-----------------------------------------------------------
        // Component 存储: 稀疏集
        //   dense: 组件数据紧密排列 (遍历缓存友好)
        //   sparse: entity id -> dense 下标
        //-----------------------------------------------------------
        class ComponentPoolBase
        {
        public:
            virtual ~ComponentPoolBase() = default;
            virtual bool has(Entity e) const = 0;
            virtual void remove(Entity e) = 0;
            virtual size_t size() const = 0;
        };

        template <typename T>
        class ComponentPool : public ComponentPoolBase
        {
        public:
            T &add(Entity e, const T &component = T{})
            {
                auto it = m_sparse.find(e);
                if (it != m_sparse.end())
                {
                    m_dense[it->second] = component;
                    return m_dense[it->second];
                }
                uint32_t idx = (uint32_t)m_dense.size();
                m_dense.push_back(component);
                m_entities.push_back(e);
                m_sparse[e] = idx;
                return m_dense[idx];
            }

            bool has(Entity e) const override
            {
                return m_sparse.find(e) != m_sparse.end();
            }

            T *get(Entity e)
            {
                auto it = m_sparse.find(e);
                return it != m_sparse.end() ? &m_dense[it->second] : nullptr;
            }

            void remove(Entity e) override
            {
                auto it = m_sparse.find(e);
                if (it == m_sparse.end())
                    return;
                uint32_t idx = it->second;
                uint32_t last = (uint32_t)m_dense.size() - 1;
                // swap-remove 保持紧密
                m_dense[idx] = m_dense[last];
                m_entities[idx] = m_entities[last];
                m_sparse[m_entities[idx]] = idx;
                m_dense.pop_back();
                m_entities.pop_back();
                m_sparse.erase(it);
            }

            size_t size() const override { return m_dense.size(); }

            std::vector<T> m_dense;
            std::vector<Entity> m_entities;
            std::unordered_map<Entity, uint32_t> m_sparse;
        };

        //-----------------------------------------------------------
        // System
        //-----------------------------------------------------------
        class World;
        class System
        {
        public:
            virtual ~System() = default;
            virtual void onUpdate(World &world, float dt) { (void)world; (void)dt; }
            virtual void onEvent(World &world, const char *event) { (void)world; (void)event; }
        };

        //-----------------------------------------------------------
        // World
        //-----------------------------------------------------------
        class World
        {
        public:
            Entity createEntity()
            {
                Entity e;
                if (!m_free.empty())
                {
                    e = m_free.back();
                    m_free.pop_back();
                    m_alive[e] = true;
                }
                else
                {
                    e = (Entity)m_alive.size();
                    m_alive.push_back(true);
                }
                return e;
            }

            void destroyEntity(Entity e)
            {
                if (!isAlive(e))
                    return;
                for (auto &pair : m_pools)
                    pair.second->remove(e);
                m_alive[e] = false;
                m_free.push_back(e);
            }

            bool isAlive(Entity e) const
            {
                return e < m_alive.size() && m_alive[e];
            }

            template <typename T>
            T &addComponent(Entity e, const T &component = T{})
            {
                return pool<T>().add(e, component);
            }

            template <typename T>
            T *getComponent(Entity e)
            {
                return pool<T>().get(e);
            }

            template <typename T>
            bool hasComponent(Entity e)
            {
                return pool<T>().has(e);
            }

            template <typename T>
            void removeComponent(Entity e)
            {
                pool<T>().remove(e);
            }

            template <typename T>
            ComponentPool<T> &pool()
            {
                std::type_index ti = typeid(T);
                auto it = m_pools.find(ti);
                if (it == m_pools.end())
                {
                    auto p = std::make_unique<ComponentPool<T>>();
                    ComponentPool<T> *raw = p.get();
                    m_pools[ti] = std::move(p);
                    return *raw;
                }
                return *static_cast<ComponentPool<T> *>(it->second.get());
            }

            // 遍历: 持有全部指定组件的实体
            template <typename T1, typename T2, typename Fn>
            void each(Fn &&fn)
            {
                auto &p1 = pool<T1>();
                auto &p2 = pool<T2>();
                // 从小的池开始扫
                if (p1.size() <= p2.size())
                {
                    for (size_t i = 0; i < p1.m_dense.size(); ++i)
                    {
                        Entity e = p1.m_entities[i];
                        T2 *c2 = p2.get(e);
                        if (c2)
                            fn(e, p1.m_dense[i], *c2);
                    }
                }
                else
                {
                    for (size_t i = 0; i < p2.m_dense.size(); ++i)
                    {
                        Entity e = p2.m_entities[i];
                        T1 *c1 = p1.get(e);
                        if (c1)
                            fn(e, *c1, p2.m_dense[i]);
                    }
                }
            }

            void addSystem(std::shared_ptr<System> system)
            {
                m_systems.push_back(std::move(system));
            }

            void update(float dt)
            {
                for (auto &system : m_systems)
                    system->onUpdate(*this, dt);
            }

            void broadcast(const char *event)
            {
                for (auto &system : m_systems)
                    system->onEvent(*this, event);
            }

            void clear()
            {
                m_pools.clear();
                m_alive.clear();
                m_free.clear();
            }

        private:
            std::unordered_map<std::type_index, std::unique_ptr<ComponentPoolBase>> m_pools;
            std::vector<bool> m_alive;
            std::vector<Entity> m_free;
            std::vector<std::shared_ptr<System>> m_systems;
        };

    } // namespace ecs
} // namespace Genesis

#endif // __GENESIS_ECS_H__
