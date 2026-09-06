/*
 * @Descripttion: 着色器库: 按名字加载双后端着色器
 *               Vulkan -> <dir>/<name>.<stage>.spv
 *               OpenGL -> <dir>/<name>.<stage>.glsl (构建期已展开 #include)
 * @version: v1.0
 */
#ifndef __GENESIS_SHADER_LIBRARY_H__
#define __GENESIS_SHADER_LIBRARY_H__

#include "rhi_layer/rhi_device.hpp"

#include <string>

namespace Genesis
{
    namespace render
    {
        struct ShaderPair
        {
            rhi::ShaderHandle m_vs;
            rhi::ShaderHandle m_fs;
        };

        class ShaderLibrary
        {
        public:
            ShaderLibrary(rhi::RHIDevice *device, const std::string &shaderDir)
                : m_device(device), m_shader_dir(shaderDir) {}

            // 加载 <name>.vert + <name>.frag
            ShaderPair load(const std::string &name);

        private:
            std::string readFile(const std::string &path);

        private:
            rhi::RHIDevice *m_device;
            std::string m_shader_dir;
        };

    } // namespace render
} // namespace Genesis

#endif // __GENESIS_SHADER_LIBRARY_H__
