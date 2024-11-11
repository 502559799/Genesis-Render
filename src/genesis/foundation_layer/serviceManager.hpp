#ifndef __VULKAN_RENDERER_SERVICEMANAGER_H__
#define __VULKAN_RENDERER_SERVICEMANAGER_H__

#include "foundation_layer/array.hpp"
#include <unordered_map>
namespace Genesis
{
    class Service;

    class ServiceManager
    {
    public:
        static ServiceManager *instance();

        void init(Allocator *allocator);
        void shutdown();

        void add(Service *service, const char *name);
        void remove(const char *name);

    private:
        static ServiceManager instance_;
        Allocator *allocator_{nullptr};
        std::unordered_map< const char *, Service*> services_;
    };
}

#endif /* __VULKAN_RENDERER_SERVICEMANAGER_H__ */