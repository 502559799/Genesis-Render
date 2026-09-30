/*
 * Genesis Render - render scene: GPU resources, camera, material library
 *
 * The render scene is the GPU-side mirror of the ECS world.  RenderSystem
 * synchronises it every frame and then executes the passes.  Keeping GPU
 * objects here (rather than in ECS components) means the simulation layer
 * never touches the RHI.
 */
#ifndef __GENESIS_RENDER_SCENE_HPP__
#define __GENESIS_RENDER_SCENE_HPP__

#include "ecs/components.hpp"
#include "renderer/material.hpp"
#include "renderer/render_ubo.hpp"
#include "resource/resource_types.hpp"
#include "rhi/rhi.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        class FrameAllocator;

        //==================================================================
        // Camera
        //==================================================================
        struct CameraState
        {
            Math::Vec3 position = Math::Vec3(0, 0, 5);
            Math::Quat rotation = Math::Quat::identity();
            Math::Vec3 forward = Math::Vec3(0, 0, -1);
            Math::Vec3 up = Math::Vec3(0, 1, 0);
            Math::Vec3 right = Math::Vec3(1, 0, 0);

            float fov_degrees = 60.0f;
            float near_plane = 0.05f;
            float far_plane = 500.0f;
            float aspect = 16.0f / 9.0f;
            bool orthographic = false;
            float ortho_size = 10.0f;

            Math::Mat4 view = Math::Mat4::identity();
            Math::Mat4 projection = Math::Mat4::identity();
            Math::Mat4 view_projection = Math::Mat4::identity();
            Math::Mat4 inv_view_projection = Math::Mat4::identity();
            Math::Mat4 inv_view = Math::Mat4::identity();

            // NDC depth convention of the device (RHI DeviceInfo).
            bool depth_zero_to_one = false;

            // The projection is never flipped: NDC y = -1 is row 0 of every
            // render target on every backend (see RenderTexture).
            void update();
            void updateFromTransform(const Math::Mat4 &world_matrix);
            void lookAt(const Math::Vec3 &eye, const Math::Vec3 &target, const Math::Vec3 &world_up = Math::Vec3(0, 1, 0));

            // Picking ray from viewport coordinates whose origin is the
            // top-left corner of the displayed image.
            Math::Ray screenRay(float pixel_x, float pixel_y, float viewport_w, float viewport_h) const;
        };

        //==================================================================
        // GPU meshes
        //==================================================================
        struct GpuMesh
        {
            std::string name;
            RHI::RHIBuffer *vertex_buffer = nullptr;
            RHI::RHIBuffer *index_buffer = nullptr;
            uint32_t vertex_count = 0;
            uint32_t index_count = 0;
            RHI::IndexType index_type = RHI::IndexType::UInt32;
            int32_t material_index = -1;
            Math::AABB bounds;
            uint32_t source_mesh_index = 0;
        };

        //==================================================================
        // Texture cache: deduplicates by resolved key (path or embedded ptr)
        //==================================================================
        class TextureCache
        {
        public:
            explicit TextureCache(RHI::RHIDevice *device);
            ~TextureCache();

            // Loads (or reuses) the texture referenced by `ref`.  Returns the
            // default texture for `fallback_key` when the reference is invalid.
            GpuTexture acquire(const TextureRef &ref, bool srgb, const std::string &fallback_key = "white");
            GpuTexture acquirePath(const std::string &path, bool srgb);

            GpuTexture createFromImage(const ImageData &image, const std::string &key, bool srgb, bool mipmap = true);

            // Built-in defaults, created lazily.
            const GpuTexture &white();
            const GpuTexture &black();
            const GpuTexture &flatNormal();
            const GpuTexture &grey();
            const GpuTexture &blackCube(); // for unbound samplerCube slots

            size_t count() const { return cache_.size(); }
            // Loaded file / embedded textures (defaults excluded), for editors.
            std::vector<GpuTexture> list() const;
            void clear();

        private:
            RHI::RHISampler *samplerFor(bool mipmap, RHI::AddressMode address);

            RHI::RHIDevice *device_ = nullptr;
            std::unordered_map<std::string, GpuTexture> cache_;
            std::unordered_map<std::string, RHI::RHISampler *> samplers_;
            GpuTexture white_, black_, normal_, grey_, black_cube_;
        };

        //==================================================================
        // Per-frame draw item
        //==================================================================
        struct RenderItem
        {
            GpuMesh *mesh = nullptr;
            Material *material = nullptr;
            Math::Mat4 model = Math::Mat4::identity();
            Math::Mat4 normal_matrix = Math::Mat4::identity();
            Math::AABB world_bounds;
            float depth = 0.0f; // distance to camera, used for sorting
            uint32_t pick_id = 0;
            ECS::Entity entity;
        };

        //==================================================================
        // Scene
        //==================================================================
        class RenderScene
        {
        public:
            RenderScene(RHI::RHIDevice *device, RHI::RHIResourceLayout *mesh_layout, FrameAllocator *frames);
            ~RenderScene();

            TextureCache &textures() { return texture_cache_; }

            // Uploads a model (all meshes + materials).  Returns mesh indices
            // into meshes(), in model order.
            std::vector<uint32_t> uploadModel(const ModelData &model, const std::string &name_hint = "");
            uint32_t uploadMesh(const MeshData &mesh, const std::string &name);

            GpuMesh *mesh(uint32_t index) { return index < meshes_.size() ? meshes_[index].get() : nullptr; }
            size_t meshCount() const { return meshes_.size(); }

            // ---- material library -------------------------------------
            int32_t createMaterial(const MaterialData &material);
            int32_t addMaterial(const MaterialPtr &material);
            MaterialPtr createMaterial(const std::string &name);
            MaterialPtr cloneMaterial(const Material &source, const std::string &new_name);
            Material *material(int32_t index) { return (index >= 0 && (size_t)index < materials_.size()) ? materials_[index].get() : nullptr; }
            MaterialPtr materialPtr(int32_t index) { return (index >= 0 && (size_t)index < materials_.size()) ? materials_[index] : nullptr; }
            MaterialPtr findMaterial(const std::string &name) const;
            size_t materialCount() const { return materials_.size(); }
            Material *defaultMaterial();

            // Builds / refreshes the descriptor set of a material (set 1).
            // `uniform_ring` is the frame allocator's uniform buffer.
            bool prepareMaterial(Material &material);

            // Convenience editors kept for code and tests that address
            // materials by index.
            void setMaterialBaseColor(int32_t index, const Math::Color &color);
            void setMaterialPBR(int32_t index, float metallic, float roughness);
            void setMaterialEmissive(int32_t index, const Math::Color &color, float strength);

            void releaseAll();

        private:
            RHI::RHIDevice *device_ = nullptr;
            RHI::RHIResourceLayout *mesh_layout_ = nullptr;
            FrameAllocator *frames_ = nullptr;
            TextureCache texture_cache_;
            std::vector<std::unique_ptr<GpuMesh>> meshes_;
            std::vector<MaterialPtr> materials_;
            int32_t default_material_index_ = -1;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_SCENE_HPP__ */
