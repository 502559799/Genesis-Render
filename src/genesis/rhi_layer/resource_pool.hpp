/*
 * @Descripttion: 通用句柄资源池 (handle = 索引, 空闲链表复用)
 */
#ifndef __GENESIS_RHI_RESOURCE_POOL_H__
#define __GENESIS_RHI_RESOURCE_POOL_H__

#include "rhi_layer/rhi_types.hpp"

#include <vector>

namespace Genesis
{
    namespace rhi
    {
        template <typename T>
        struct ResourcePool
        {
            std::vector<T> m_items;
            std::vector<uint32_t> m_free;

            uint32_t allocate()
            {
                if (!m_free.empty())
                {
                    uint32_t idx = m_free.back();
                    m_free.pop_back();
                    return idx;
                }
                m_items.emplace_back();
                return (uint32_t)m_items.size() - 1;
            }

            void release(uint32_t idx)
            {
                if (idx != kInvalidHandle)
                    m_free.push_back(idx);
            }

            T &get(uint32_t idx) { return m_items[idx]; }
            const T &get(uint32_t idx) const { return m_items[idx]; }
        };
    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_RHI_RESOURCE_POOL_H__
