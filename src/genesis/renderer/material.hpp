/*
 * Genesis Render - material system.
 *
 * A Material is a live, editable object (UI inspector, Lua via RTTI, C++):
 *
 *   parameters  : base colour, emissive colour + strength, metallic,
 *                 roughness, normal scale, occlusion strength, alpha cutoff,
 *                 uv scale / offset
 *   textures    : five slots (base colour, metallic-roughness, normal,
 *                 occlusion, emissive).  A slot references either an image
 *                 file (through the texture cache) or a RenderTexture, and
 *                 can be toggled without losing the reference
 *   render state: shading model (renderer default / Cook-Torrance /
 *                 Blinn-Phong / unlit), blend mode (opaque / masked /
 *                 transparent / additive), cull mode, depth test / write
 *
 * GPU side, parameters are uploaded every frame into the transient uniform
 * ring (so edits are visible on the very next frame and never race a frame
 * in flight) and the descriptor set (set 1) is rebuilt only when a texture
 * reference changes; the previous set is released through the frame
 * allocator.
 */
#ifndef __GENESIS_MATERIAL_HPP__
#define __GENESIS_MATERIAL_HPP__

#include "foundation_layer/math/math.hpp"
#include "renderer/render_texture.hpp"
#include "renderer/render_ubo.hpp"
#include "rhi/rhi.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Genesis
{
    namespace Render
    {
        class RenderScene;

        struct GpuTexture
        {
            RHI::RHITexture *texture = nullptr;
            RHI::RHISampler *sampler = nullptr;
            std::string key;
            uint32_t width = 0, height = 0;
            bool srgb = false;
            bool owns = true;
            bool valid() const { return texture != nullptr; }
        };

        enum class MaterialShading : uint32_t
        {
            Default = 0, // follow RenderSettings::shading_mode
            CookTorrance = 1,
            BlinnPhong = 2,
            Unlit = 3,
        };

        enum class MaterialBlend : uint32_t
        {
            Opaque = 0,
            Masked = 1,      // alpha test against alpha_cutoff
            Transparent = 2, // sorted back to front, alpha blended
            Additive = 3,
        };

        enum class MaterialCull : uint32_t
        {
            Back = 0,
            Front = 1,
            None = 2,
        };

        enum class TextureSlot : uint32_t
        {
            BaseColor = 0,
            MetallicRoughness = 1,
            Normal = 2,
            Occlusion = 3,
            Emissive = 4,
            Count = 5,
        };
        const char *textureSlotName(TextureSlot slot);
        bool textureSlotFromName(const std::string &name, TextureSlot &out);
        const char *materialShadingName(MaterialShading s);
        const char *materialBlendName(MaterialBlend b);
        const char *materialCullName(MaterialCull c);

        class Material
        {
        public:
            explicit Material(const std::string &material_name = "material") : name(material_name) {}

            std::string name;

            // ---- parameters -------------------------------------------
            Math::Color base_color = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            Math::Color emissive = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
            float emissive_strength = 1.0f;
            float metallic = 0.0f;
            float roughness = 0.5f;
            float normal_scale = 1.0f;
            float occlusion_strength = 1.0f;
            float alpha_cutoff = 0.5f;
            Math::Vec2 uv_scale = Math::Vec2(1.0f, 1.0f);
            Math::Vec2 uv_offset = Math::Vec2(0.0f, 0.0f);

            // ---- render state -----------------------------------------
            MaterialShading shading = MaterialShading::Default;
            MaterialBlend blend = MaterialBlend::Opaque;
            MaterialCull cull = MaterialCull::Back;
            bool double_sided = false; // disables culling and flips back-facing normals
            bool depth_test = true;
            bool depth_write = true;

            // ---- textures ---------------------------------------------
            struct Slot
            {
                GpuTexture texture;           // resolved texture (file) - may be empty
                std::string source;           // file path, or "rt:<name>" for render textures
                RenderTexturePtr render_texture;
                bool enabled = true;
                uint32_t rt_generation = 0;
            };
            Slot slots[(size_t)TextureSlot::Count];

            // Assigns an image file (resolved through the owning scene's
            // texture cache).  An empty path clears the slot.
            bool setTexture(TextureSlot slot, const std::string &path);
            // Assigns a render texture.  Colour slots receive a v-flipped uv
            // transform so the rendered image appears upright on meshes.
            void setRenderTexture(TextureSlot slot, const RenderTexturePtr &rt);
            void clearTexture(TextureSlot slot);
            void setTextureEnabled(TextureSlot slot, bool enabled);
            bool hasTexture(TextureSlot slot) const;
            std::string textureSource(TextureSlot slot) const { return slots[(size_t)slot].source; }

            // ---- queries ----------------------------------------------
            bool isTransparent() const { return blend == MaterialBlend::Transparent || blend == MaterialBlend::Additive; }
            RHI::CullMode rhiCull() const
            {
                if (double_sided || cull == MaterialCull::None) return RHI::CullMode::None;
                return cull == MaterialCull::Front ? RHI::CullMode::Front : RHI::CullMode::Back;
            }
            MaterialUBO buildUBO(ShadingMode renderer_default) const;
            // Copies every parameter, texture reference and render state.
            void copyFrom(const Material &other);

            // ---- renderer bookkeeping ---------------------------------
            RenderScene *owner = nullptr;
            int32_t index = -1;                         // position in the scene library
            RHI::RHIDescriptorSet *descriptor_set = nullptr;
            uint64_t texture_revision = 1;              // bumped when a slot changes
            uint64_t bound_revision = 0;                // revision the set was built for
            uint64_t ubo_frame = ~0ull;                 // frame stamp of the cached UBO slice
            uint32_t ubo_offset = 0;                    // dynamic offset of that slice
            uint64_t revision = 1;                      // bumped by any edit through the API

            void touch() { ++revision; }
        };

        using MaterialPtr = std::shared_ptr<Material>;

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_MATERIAL_HPP__ */
