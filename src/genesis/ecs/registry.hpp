/*
 * Genesis Render - Entity-Component-System registry.
 *
 * A real (data-oriented) ECS:
 *   - Entity is a 32-bit id with an 8-bit generation for safe recycling.
 *   - Each component type owns a dense Pool<T> (sparse index map + dense data),
 *     so views over component sets are O(matching entities).
 *   - No inheritance / virtual base for components (unlike Piccolo's
 *     GObject+Component object model; this is the requested explicit ECS).
 */
#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <typeindex>
#include <typeinfo>
#include <memory>
#include <functional>
#include <cassert>
#include <utility>

namespace Genesis {

struct Entity {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const Entity& o) const { return id == o.id; }
    bool operator!=(const Entity& o) const { return id != o.id; }
};
static constexpr Entity kInvalidEntity{0};
struct EntityHash { size_t operator()(const Entity& e) const { return std::hash<uint32_t>{}(e.id); } };

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(uint32_t index) = 0;
    virtual bool has(uint32_t index) const = 0;
    virtual void cloneFrom(uint32_t src, uint32_t dst) = 0;
};

template <typename T>
class ComponentPool : public IComponentPool {
public:
    T& add(uint32_t e) {
        auto it = sparse_.find(e);
        if (it != sparse_.end()) return dense_[it->second];
        sparse_[e] = (uint32_t)dense_.size();
        entities_.push_back(Entity{e});
        dense_.push_back(T{});
        return dense_.back();
    }
    T* get(uint32_t e) {
        auto it = sparse_.find(e);
        return it == sparse_.end() ? nullptr : &dense_[it->second];
    }
    const T* get(uint32_t e) const {
        auto it = sparse_.find(e);
        return it == sparse_.end() ? nullptr : &dense_[it->second];
    }
    bool has(uint32_t e) const override { return sparse_.count(e) != 0; }
    void remove(uint32_t e) override {
        auto it = sparse_.find(e);
        if (it == sparse_.end()) return;
        uint32_t idx = it->second;
        uint32_t last = (uint32_t)dense_.size() - 1;
        if (idx != last) {
            dense_[idx] = std::move(dense_[last]);
            entities_[idx] = entities_[last];
            sparse_[entities_[idx].id] = idx;
        }
        dense_.pop_back();
        entities_.pop_back();
        sparse_.erase(it);
    }
    void cloneFrom(uint32_t src, uint32_t dst) override {
        const T* p = get(src);
        if (p) add(dst) = *p;
    }
    const std::vector<Entity>& entities() const { return entities_; }
    std::vector<T>& data() { return dense_; }
    size_t size() const { return dense_.size(); }

private:
    std::vector<T> dense_;
    std::vector<Entity> entities_;
    std::unordered_map<uint32_t, uint32_t> sparse_;
};

class Registry {
public:
    Entity create() {
        uint32_t idx;
        if (!free_.empty()) { idx = free_.back(); free_.pop_back(); }
        else {
            idx = ++entity_max_;
            generations_.push_back(0);
        }
        alive_.insert(idx);
        return Entity{(generations_[idx] << 24) | (idx & 0x00FFFFFF)};
    }
    void destroy(Entity e) {
        uint32_t idx = index(e);
        if (!alive_.count(idx)) return;
        for (auto& kv : pools_) kv.second->remove(idx);
        alive_.erase(idx);
        generations_[idx] = (uint8_t)((generations_[idx] + 1) & 0x7F);
        free_.push_back(idx);
    }
    bool alive(Entity e) const {
        uint32_t i = index(e);
        return alive_.count(i) != 0 && gen(e) == generations_[i];
    }

    template <typename T, typename... Args>
    T& add(Entity e, Args&&... args) {
        T& c = pool<T>().add(index(e));
        c = T(std::forward<Args>(args)...);
        return c;
    }
    template <typename T>
    T* get(Entity e) { return pool<T>().get(index(e)); }
    template <typename T>
    const T* get(Entity e) const {
        auto* p = poolPtr<T>();
        return p ? p->get(index(e)) : nullptr;
    }
    template <typename T>
    bool has(Entity e) const {
        auto* p = poolPtr<T>();
        return p && p->has(index(e));
    }
    template <typename T>
    void remove(Entity e) {
        if (auto* p = poolPtr<T>()) p->remove(index(e));
    }
    template <typename T>
    ComponentPool<T>& pool() {
        auto key = std::type_index(typeid(T));
        auto it = pools_.find(key);
        if (it == pools_.end()) {
            auto up = std::make_unique<ComponentPool<T>>();
            auto* raw = up.get();
            pools_[key] = std::move(up);
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }
    template <typename T>
    const ComponentPool<T>* poolConst() const { return poolPtr<T>(); }

    // Iterate entities owning the listed components.
    template <typename C, typename... Cs, typename F>
    void view(F&& fn) {
        auto& p = pool<C>();
        for (auto e : p.entities()) {
            if (hasAll<C, Cs...>(e.id)) fn(e);
        }
    }

    size_t entityCount() const { return alive_.size(); }
    const std::unordered_set<uint32_t>& aliveIndices() const { return alive_; }

    void cloneEntityComponents(Entity src, Entity dst) {
        for (auto& kv : pools_) kv.second->cloneFrom(index(src), index(dst));
    }

private:
    template <typename T>
    ComponentPool<T>* poolPtr() const {
        auto it = pools_.find(std::type_index(typeid(T)));
        return it == pools_.end() ? nullptr : static_cast<ComponentPool<T>*>(it->second.get());
    }
    template <typename C1, typename C2, typename... Cs>
    bool hasAll(uint32_t e) const {
        auto* p = poolPtr<C1>();
        if (!p || !p->has(e)) return false;
        return hasAll<C2, Cs...>(e);
    }
    template <typename C>
    bool hasAll(uint32_t e) const {
        auto* p = poolPtr<C>();
        return p && p->has(e);
    }
    static uint32_t index(Entity e) { return e.id & 0x00FFFFFF; }
    static uint32_t gen(Entity e) { return e.id >> 24; }

    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
    std::unordered_set<uint32_t> alive_;
    std::vector<uint32_t> free_;
    std::vector<uint8_t> generations_;
    uint32_t entity_max_ = 0;
};

} // namespace Genesis
