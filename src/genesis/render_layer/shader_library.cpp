/*
 * @Descripttion: 着色器库实现
 */
#include "render_layer/shader_library.hpp"

#include "foundation_layer/log.hpp"

#include <fstream>

namespace Genesis
{
    namespace render
    {
        std::string ShaderLibrary::readFile(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                GenesisPrint("[Shader] failed to open: %s\n", path.c_str());
                return {};
            }
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        ShaderPair ShaderLibrary::load(const std::string &name)
        {
            ShaderPair pair{};
            bool is_vulkan = m_device->getBackend() == rhi::Backend::Vulkan;

            for (int stage = 0; stage < 2; ++stage)
            {
                const char *stage_name = stage == 0 ? "vert" : "frag";
                rhi::ShaderCreateInfo info{};
                info.m_stage = stage == 0 ? rhi::ShaderStage::Vertex : rhi::ShaderStage::Fragment;
                info.m_name = name.c_str();

                std::string path;
                std::string source;
                if (is_vulkan)
                {
                    path = m_shader_dir + "/" + name + "." + stage_name + ".spv";
                    source = readFile(path);
                    if (source.empty())
                        return {};
                    info.m_spirv = (const uint32_t *)source.data();
                    info.m_spirv_size = source.size();
                }
                else
                {
                    path = m_shader_dir + "/" + name + "." + stage_name + ".glsl";
                    source = readFile(path);
                    if (source.empty())
                        return {};
                    info.m_glsl = source.c_str();
                }

                rhi::ShaderHandle handle = m_device->createShader(info);
                if (!handle.isValid())
                {
                    GenesisPrint("[Shader] create failed: %s\n", path.c_str());
                    return {};
                }
                if (stage == 0)
                    pair.m_vs = handle;
                else
                    pair.m_fs = handle;
            }

            return pair;
        }

    } // namespace render
} // namespace Genesis
