/*
 * Genesis Render - Engine
 * entity.hpp : entity = index + generation (safe dangling references).
 */
#pragma once
#include <cstdint>
#include <functional>

namespace Genesis
{
    struct Entity
    {
        uint32_t index = 0xffffffffu;
        uint32_t generation = 0;
        bool valid() const { return index != 0xffffffffu; }
        bool operator==(const Entity &o) const
        {
            return index == o.index && generation == o.generation;
        }
        bool operator!=(const Entity &o) const { return !(*this == o); }
    };
    static constexpr Entity kInvalidEntity{};

    struct EntityHash
    {
        size_t operator()(const Entity &e) const
        {
            return std::hash<uint64_t>()((uint64_t)e.generation << 32 | e.index);
        }
    };
}
