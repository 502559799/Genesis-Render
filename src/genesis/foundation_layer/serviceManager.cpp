/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-31 17:35:01
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-31 19:31:19
 * @version:
 */
#include "foundation_layer/serviceManager.hpp"
#include "foundation_layer/assert.hpp"

namespace Genesis
{
    ServiceManager ServiceManager::instance_;

    ServiceManager *ServiceManager::instance()
    {
        return &instance_;
    }

    void ServiceManager::init(Allocator *allocator)
    {
        GenesisPrint("ServieManager init\n");
        allocator_ = allocator;
    }

    void ServiceManager::shutdown()
    {
        GenesisPrint("ServieManager shutdown\n");
    }

    void ServiceManager::add(Service *service, const char *name)
    {
    }

    void ServiceManager::remove(const char *name)
    {
    }
}