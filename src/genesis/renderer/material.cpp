/*
 * Genesis Render - material system implementation.
 */
#include "renderer/material.hpp"
#include "renderer/render_scene.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        const char *textureSlotName(TextureSlot slot)
        {
            switch (slot)
            {
            case TextureSlot::BaseColor: return "base_color";
            case TextureSlot::MetallicRoughness: return "metallic_roughness";
            case TextureSlot::Normal: return "normal";
            case TextureSlot::Occlusion: return "occlusion";
            case TextureSlot::Emissive: return "emissive";
            default: return "unknown";
            }
        }

        bool textureSlotFromName(const std::string &name, TextureSlot &out)
        {
            for (uint32_t i = 0; i < (uint32_t)TextureSlot::Count; ++i)
            {
                if (name == textureSlotName((TextureSlot)i))
                {
                    out = (TextureSlot)i;
                    return true;
                }
            }
            if (name == "albedo" || name == "base") { out = TextureSlot::BaseColor; return true; }
            if (name == "mr" || name == "metal_rough") { out = TextureSlot::MetallicRoughness; return true; }
            if (name == "ao") { out = TextureSlot::Occlusion; return true; }
            return false;
        }

        const char *materialShadingName(MaterialShading s)
        {
            switch (s)
            {
            case MaterialShading::Default: return "default";
            case MaterialShading::CookTorrance: return "cook_torrance";
            case MaterialShading::BlinnPhong: return "blinn_phong";
            case MaterialShading::Unlit: return "unlit";
            }
            return "default";
        }

        const char *materialBlendName(MaterialBlend b)
        {
            switch (b)
            {
            case MaterialBlend::Opaque: return "opaque";
            case MaterialBlend::Masked: return "masked";
            case MaterialBlend::Transparent: return "transparent";
            case MaterialBlend::Additive: return "additive";
            }
            return "opaque";
        }

        const char *materialCullName(MaterialCull c)
        {
            switch (c)
            {
            case MaterialCull::Back: return "back";
            case MaterialCull::Front: return "front";
            case MaterialCull::None: return "none";
            }
            return "back";
        }

        static bool slotIsColour(TextureSlot slot)
        {
            return slot == TextureSlot::BaseColor || slot == TextureSlot::Emissive;
        }

        bool Material::setTexture(TextureSlot slot, const std::string &path)
        {
            if ((uint32_t)slot >= (uint32_t)TextureSlot::Count) return false;
            Slot &s = slots[(size_t)slot];
            if (path.empty())
            {
                clearTexture(slot);
                return true;
            }
            if (!owner)
            {
                GEN_LOG_WARN("[Material] '%s' is not registered with a scene; cannot load '%s'", name.c_str(), path.c_str());
                return false;
            }
            GpuTexture tex = owner->textures().acquirePath(path, slotIsColour(slot));
            if (!tex.valid() || tex.key != path)
            {
                GEN_LOG_WARN("[Material] '%s': texture '%s' could not be loaded", name.c_str(), path.c_str());
                return false;
            }
            s.texture = tex;
            s.source = path;
            s.render_texture.reset();
            s.enabled = true;
            ++texture_revision;
            touch();
            return true;
        }

        void Material::setRenderTexture(TextureSlot slot, const RenderTexturePtr &rt)
        {
            if ((uint32_t)slot >= (uint32_t)TextureSlot::Count) return;
            Slot &s = slots[(size_t)slot];
            s.texture = GpuTexture();
            s.render_texture = rt;
            s.source = rt ? "rt:" + rt->name() : "";
            s.enabled = rt != nullptr;
            s.rt_generation = rt ? rt->generation() : 0;
            if (rt && slotIsColour(slot))
            {
                // Rendered images are stored bottom-up; mesh uvs are top-down.
                uv_scale.y = -std::abs(uv_scale.y);
                uv_offset.y = 1.0f;
            }
            ++texture_revision;
            touch();
        }

        void Material::clearTexture(TextureSlot slot)
        {
            if ((uint32_t)slot >= (uint32_t)TextureSlot::Count) return;
            Slot &s = slots[(size_t)slot];
            s = Slot();
            ++texture_revision;
            touch();
        }

        void Material::setTextureEnabled(TextureSlot slot, bool enabled)
        {
            if ((uint32_t)slot >= (uint32_t)TextureSlot::Count) return;
            Slot &s = slots[(size_t)slot];
            if (s.enabled == enabled) return;
            s.enabled = enabled;
            ++texture_revision;
            touch();
        }

        bool Material::hasTexture(TextureSlot slot) const
        {
            if ((uint32_t)slot >= (uint32_t)TextureSlot::Count) return false;
            const Slot &s = slots[(size_t)slot];
            return s.enabled && (s.texture.valid() || (s.render_texture && s.render_texture->valid()));
        }

        MaterialUBO Material::buildUBO(ShadingMode renderer_default) const
        {
            MaterialUBO u;
            u.base_color_factor = GpuVec4::from(base_color);
            u.emissive_factor = GpuVec4(emissive.r, emissive.g, emissive.b, emissive_strength);
            u.params0 = GpuVec4(metallic, roughness, normal_scale, occlusion_strength);
            float alpha_mode = 0.0f;
            if (blend == MaterialBlend::Masked) alpha_mode = 1.0f;
            else if (isTransparent()) alpha_mode = 2.0f;
            float model = (float)renderer_default;
            if (shading == MaterialShading::CookTorrance) model = (float)ShadingMode::CookTorrance;
            else if (shading == MaterialShading::BlinnPhong) model = (float)ShadingMode::BlinnPhong;
            else if (shading == MaterialShading::Unlit) model = (float)ShadingMode::Unlit;
            u.params1 = GpuVec4(alpha_cutoff, alpha_mode, double_sided ? 1.0f : 0.0f, model);
            u.texture_mask = GpuVec4(hasTexture(TextureSlot::BaseColor) ? 1.0f : 0.0f,
                                     hasTexture(TextureSlot::MetallicRoughness) ? 1.0f : 0.0f,
                                     hasTexture(TextureSlot::Normal) ? 1.0f : 0.0f,
                                     hasTexture(TextureSlot::Occlusion) ? 1.0f : 0.0f);
            u.extra = GpuVec4(hasTexture(TextureSlot::Emissive) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            u.uv_transform = GpuVec4(uv_scale.x, uv_scale.y, uv_offset.x, uv_offset.y);
            return u;
        }

        void Material::copyFrom(const Material &o)
        {
            base_color = o.base_color;
            emissive = o.emissive;
            emissive_strength = o.emissive_strength;
            metallic = o.metallic;
            roughness = o.roughness;
            normal_scale = o.normal_scale;
            occlusion_strength = o.occlusion_strength;
            alpha_cutoff = o.alpha_cutoff;
            uv_scale = o.uv_scale;
            uv_offset = o.uv_offset;
            shading = o.shading;
            blend = o.blend;
            cull = o.cull;
            double_sided = o.double_sided;
            depth_test = o.depth_test;
            depth_write = o.depth_write;
            for (size_t i = 0; i < (size_t)TextureSlot::Count; ++i) slots[i] = o.slots[i];
            ++texture_revision;
            touch();
        }

    } // namespace Render
} // namespace Genesis
