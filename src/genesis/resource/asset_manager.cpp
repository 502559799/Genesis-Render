/*
 * Genesis Render - Engine
 * asset_manager.cpp
 */
#include "asset_manager.hpp"
#include "image.hpp"
#include "model_loader.hpp"
#include "foundation_layer/log.hpp"

#include <cmath>
#include <cstdint>

namespace Genesis
{
// IEEE 754 binary32 -> binary16 conversion (0 round-to-nearest-even).
static uint16_t f32tof16(float f)
{
    union { float f; uint32_t u; } v{f};
    uint32_t u = v.u;
    uint32_t sign = (u >> 16) & 0x8000u;
    int32_t exp = ((u >> 23) & 0xff) - 127 + 15;
    uint32_t mant = u & 0x7fffff;
    if (((u >> 23) & 0xff) == 0xff) return (uint16_t)(sign | 0x7c00u | (mant ? 1 : 0));
    if (exp >= 31) return (uint16_t)(sign | 0x7c00u);
    if (exp <= 0)
    {
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t h = mant >> shift;
        h += (mant >> (shift - 1)) & 1;
        return (uint16_t)(sign | h);
    }
    uint16_t h = (uint16_t)(sign | (exp << 10) | (mant >> 13));
    h += (mant >> 12) & 1;
    return h;
}

void AssetManager::init(IRHIDevice *device)
{
    device_ = device;
    // 1x1 defaults.
    auto mk = [&](const uint8_t rgba[4]) -> TextureHandle {
        TextureDesc d;
        d.format = Format::R8G8B8A8Unorm;
        d.width = 1; d.height = 1; d.mip_levels = 1;
        d.usage = TextureUsage_Sampled;
        return device_->createTexture(d, rgba);
    };
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t normal[4] = {128, 128, 255, 255};
    // ORM convention: r = ao(1), g = roughness(1), b = metallic(0)
    static const uint8_t orm[4] = {255, 255, 0, 255};
    static const uint8_t black[4] = {0, 0, 0, 255};
    default_albedo_ = mk(white);
    default_normal_ = mk(normal);
    default_orm_ = mk(orm);
    default_emissive_ = mk(black);
}

void AssetManager::shutdown() {}

TextureHandle AssetManager::createTextureRGBA8(int w, int h, const uint8_t *rgba, bool srgb,
                                               uint16_t mips, bool flip)
{
    CPUImage img;
    imageFromRawRGBA8(w, h, rgba, flip, img);
    TextureDesc d;
    d.format = srgb ? Format::R8G8B8A8Srgb : Format::R8G8B8A8Unorm;
    d.width = w; d.height = h; d.mip_levels = mips;
    d.usage = TextureUsage_Sampled;
    TextureHandle t = device_->createTexture(d, img.bytes.data());
    if (mips > 1) device_->generateMipmaps(t);
    return t;
}

TextureHandle AssetManager::createTextureRGBA32F(int w, int h, const float *rgba, uint16_t mips)
{
    TextureDesc d;
    d.format = Format::R32G32B32A32Float;
    d.width = w; d.height = h; d.mip_levels = mips;
    d.usage = TextureUsage_Sampled;
    TextureHandle t = device_->createTexture(d, rgba);
    if (mips > 1) device_->generateMipmaps(t);
    return t;
}

TextureHandle AssetManager::textureFromImage(const CPUImage &img, bool srgb, bool gen_mips)
{
    uint16_t mips = 1;
    if (gen_mips)
        mips = (uint16_t)(1 + std::floor(std::log2((float)std::max(img.w, img.h))));
    std::vector<uint16_t> half_px;
    void *data = img.data();
    if (img.hdr)
    {
        half_px.resize((size_t)img.w * img.h * 4);
        const float *src = img.floats.data();
        for (size_t i = 0; i < half_px.size(); ++i) half_px[i] = f32tof16(src[i]);
        data = half_px.data();
    }
    TextureDesc d;
    // HDR environments are stored as RGBA16F (core, linearly filterable
    // everywhere, incl. OpenGL 4.1 / MoltenVK).
    d.format = img.hdr ? Format::R16G16B16A16Float
                       : (srgb ? Format::R8G8B8A8Srgb : Format::R8G8B8A8Unorm);
    d.width = img.w; d.height = img.h; d.mip_levels = mips;
    d.usage = TextureUsage_Sampled;
    TextureHandle t = device_->createTexture(d, data);
    if (mips > 1) device_->generateMipmaps(t);
    return t;
}

TextureHandle AssetManager::loadTexture2D(const std::string &path, bool srgb, bool gen_mips)
{
    auto it = texture_cache_.find(path + (srgb ? "#s" : "#l"));
    if (it != texture_cache_.end()) return it->second;
    std::string lower = path;
    for (auto &c : lower) c = (char)tolower(c);
    bool hdr = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".hdr";
    CPUImage img;
    bool ok = loadImageFile(path, hdr, flipY(), img);
    if (!ok)
    {
        GenesisPrint("[Asset] texture missing %s, using default\n", path.c_str());
        return srgb ? default_albedo_ : default_orm_;
    }
    TextureHandle t = textureFromImage(img, srgb && !hdr, gen_mips);
    texture_cache_[path + (srgb ? "#s" : "#l")] = t;
    return t;
}

TextureHandle AssetManager::embeddedOrPathTexture(const CPUMaterial::Embedded &emb,
                                                  const std::string &path, bool srgb)
{
    if (!emb.bytes.empty())
    {
        CPUImage img;
        if (loadImageMemory(emb.bytes.data(), emb.bytes.size(), false, flipY(), img))
            return textureFromImage(img, srgb, true);
    }
    if (!emb.raw.empty())
    {
        CPUImage img;
        if (imageFromRawRGBA8(emb.w, emb.h, emb.raw.data(), flipY(), img))
            return textureFromImage(img, srgb, true);
    }
    if (!path.empty() && (path[0] != '*'))
        return loadTexture2D(path, srgb, true);
    return TextureHandle{};
}

TextureHandle AssetManager::equirectFromHDR(const std::string &path)
{
    auto it = texture_cache_.find("hdr:" + path);
    if (it != texture_cache_.end()) return it->second;
    CPUImage img;
    if (!loadImageFile(path, true, false, img)) return TextureHandle{};
    TextureHandle t = textureFromImage(img, false, false);
    texture_cache_["hdr:" + path] = t;
    return t;
}

TextureHandle AssetManager::proceduralEnvironment(uint32_t w, uint32_t h)
{
    static const char *key = "procedural-env";
    auto it = texture_cache_.find(key);
    if (it != texture_cache_.end()) return it->second;
    std::vector<float> px = procgen::makeProceduralEnvironment(w, h);
    CPUImage img;
    imageFromRawRGBA32F((int)w, (int)h, px.data(), false, img);
    TextureHandle t = textureFromImage(img, false, false);
    texture_cache_[key] = t;
    return t;
}

std::shared_ptr<ModelAsset> AssetManager::loadModel(const std::string &path)
{
    auto it = model_cache_.find(path);
    if (it != model_cache_.end()) return it->second;
    CPUModel cpu;
    if (!ModelLoader::load(path, cpu)) return nullptr;
    auto m = uploadModel(cpu, path);
    model_cache_[path] = m;
    return m;
}

std::shared_ptr<ModelAsset> AssetManager::uploadModel(const CPUModel &cpu, const std::string &name)
{
    auto asset = std::make_shared<ModelAsset>();
    asset->path = name;
    size_t slash = name.find_last_of("/\\");
    asset->name = slash == std::string::npos ? name : name.substr(slash + 1);
    asset->bounds = cpu.bounds;
    bool gl = flipY();

    // materials first (synthesize a neutral default when the model carries none)
    std::vector<CPUMaterial> mats = cpu.materials;
    if (mats.empty()) mats.push_back(CPUMaterial{});
    for (const CPUMaterial &cm : mats)
    {
        auto m = std::make_shared<PBRMaterial>();
        m->name = cm.name;
        m->ubo.base_color_factor = cm.base_color;
        m->ubo.metallic_roughness = Vec4(cm.metallic, cm.roughness, 1.0f, 0.0f);
        m->ubo.emissive_factor = Vec4(cm.emissive, cm.emissive_strength);
        m->albedo_tex = embeddedOrPathTexture(cm.albedo_emb, cm.albedo_path, true);
        m->normal_tex = embeddedOrPathTexture(cm.normal_emb, cm.normal_path, false);
        m->orm_tex = embeddedOrPathTexture(cm.orm_emb, cm.orm_path, false);
        m->emissive_tex = embeddedOrPathTexture(cm.emissive_emb, cm.emissive_path, true);
        m->has_albedo = m->albedo_tex.valid();
        m->has_normal = m->normal_tex.valid();
        m->has_orm = m->orm_tex.valid();
        m->has_emissive = m->emissive_tex.valid();
        if (!m->albedo_tex) m->albedo_tex = default_albedo_;
        if (!m->normal_tex) m->normal_tex = default_normal_;
        if (!m->orm_tex) m->orm_tex = default_orm_;
        if (!m->emissive_tex) m->emissive_tex = default_emissive_;
        m->ubo.texture_flags = Vec4(m->has_albedo ? 1.f : 0.f,
                                    m->has_normal ? 1.f : 0.f,
                                    m->has_orm ? 1.f : 0.f,
                                    m->has_emissive ? 1.f : 0.f);
        BufferDesc bd;
        bd.size = 256;
        bd.usage = BufferUsage_Uniform;
        bd.host_visible = true;
        bd.name = "material_ubo";
        m->ubo_buffer = device_->createBuffer(bd);
        syncMaterial(*m);

        DescriptorSetDesc dsd;
        dsd.set_index = 1;
        dsd.addUniform(kBindingMaterial, m->ubo_buffer)
            .addTexture(kBindingBaseColor, m->albedo_tex, device_->linearSampler())
            .addTexture(kBindingNormal, m->normal_tex, device_->linearSampler())
            .addTexture(kBindingORM, m->orm_tex, device_->linearSampler())
            .addTexture(kBindingEmissive, m->emissive_tex, device_->linearSampler());
        m->set = device_->createDescriptorSet(dsd);
        asset->materials.push_back(m);
    }
    for (const auto &cmesh : cpu.meshes)
    {
        GPUMesh gm;
        gm.name = cmesh.name;
        gm.vertex_count = (uint32_t)cmesh.vertices.size();
        gm.index_count = (uint32_t)cmesh.indices.size();
        gm.index_type = IndexType::Uint32;

        std::vector<ModelVertex> verts = cmesh.vertices;
        if (gl)
            for (auto &v : verts) v.uv.y = 1.0f - v.uv.y;

        BufferDesc vb;
        vb.size = verts.size() * sizeof(ModelVertex);
        vb.usage = BufferUsage_Vertex | BufferUsage_TransferDst;
        BufferDesc ib;
        ib.size = cmesh.indices.size() * sizeof(uint32_t);
        ib.usage = BufferUsage_Index | BufferUsage_TransferDst;
        gm.vbo = device_->createBuffer(vb, verts.data());
        gm.ibo = device_->createBuffer(ib, cmesh.indices.data());
        gm.submeshes = cmesh.submeshes;
        asset->meshes.push_back(gm);
    }
    GenesisPrint("[Asset] uploaded model '%s': %zu meshes %zu materials\n",
                 asset->name.c_str(), asset->meshes.size(), asset->materials.size());
    return asset;
}

void AssetManager::syncMaterial(PBRMaterial &m)
{
    if (!m.ubo_buffer.valid()) return;
    MaterialUBO data = m.ubo;
    device_->writeBuffer(m.ubo_buffer, 0, &data, sizeof(data));
    m.dirty = false;
}
}
