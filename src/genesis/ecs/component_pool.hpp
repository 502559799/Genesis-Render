/*
 * Genesis Render - Engine
 * component_pool.hpp : per-type dense-ish component storage (SoA pools).
 */
#pragma once
#include "entity.hpp"
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>
#include <functional>

namespace Genesis
{
    class IComponentPool
    {
    public:
        virtual ~IComponentPool() = default;
        virtual void remove(uint32_t entity_index) = 0;
        virtual void cloneFrom(uint32_t src, uint32_t dst) = 0;
        virtual void clear() = 0;
    };

    template <typename T>
    class ComponentPool : public IComponentPool
    {
    public:
        T &add(uint32_t idx, T value = T{})
        {
            if (data_.find(idx) == data_.end()) order_.push_back(idx);
            data_[idx] = std::move(value);
            return data_[idx];
        }
        T *get(uint32_t idx)
        {
            auto it = data_.find(idx);
            return it == data_.end() ? nullptr : &it->second;
        }
        const T *get(uint32_t idx) const
        {
            auto it = data_.find(idx);
            return it == data_.end() ? nullptr : &it->second;
        }
        bool has(uint32_t idx) const { return data_.count(idx) != 0; }
        void remove(uint32_t idx) override
        {
            auto it = data_.find(idx);
            if (it != data_.end())
            {
                data_.erase(it);
                for (auto q = order_.begin(); q != order_.end(); ++q)
                    if (*q == idx) { order_.erase(q); break; }
            }
        }
        void cloneFrom(uint32_t src, uint32_t dst) override
        {
            if (auto *p = get(src)) add(dst, *p);
        }
        void clear() override { data_.clear(); order_.clear(); }
        size_t size() const { return data_.size(); }
        std::unordered_map<uint32_t, T> &data() { return data_; }
        const std::vector<uint32_t> &order() const { return order_; }

    private:
        std::unordered_map<uint32_t, T> data_;
        std::vector<uint32_t> order_;
    };
}
