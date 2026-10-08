/*
 * Genesis Render - render scene: GPU resources, camera, lights
 *
 * The render scene is the GPU-side mirror of the ECS world.  RenderSystem
 * synchronises it every frame (create/destroy/update) and then executes the
 * passes.  Keeping GPU objects here (rather than in ECS components) means the
 * simulation layer never touches the RHI.
 */
#ifndef __GENESIS_RENDER_SCENE_HPP__
#define __GENESIS_RENDER_SCENE_HPP__

#include "ecs/components.hpp"
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
        //==================================================================
        // Camera
        //==================================================================
        struct CameraState
        {
            Math::Vec3 position = Math::Vec3(0, 0, 5);
            Math::Quat rotation = Math::Quat::identity();
            Math::Vec3 forward = Math::Vec3(0, 0, -1);
            Math::Vec3 up = Math::Vec3(0, 1, 0);

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

            // True when the matrices were built for a swapchain target (GL then
            // does not flip Y, see the orientation note in render_system.cpp).
            bool target_is_swapchain = false;
            RHI::BackendType backend = RHI::BackendType::OpenGL;

            void update();
            void updateFromTransform(const Math::Mat4 &world_matrix);

            Math::Vec3 worldPosition() const { return position; }
            // Builds a picking ray in world space from viewport coordinates
            // whose origin is the top-left corner.
            Math::Ray screenRay(float pixel_x, float pixel_y, float viewport_w, float viewport_h) const;
        };

        //==================================================================
        // GPU resources
        //==================================================================
        struct GpuTexture
        {
            RHI::RHITexture *texture = nullptr;
            RHI::RHISampler *sampler = nullptr;
            std::string key;
            uint32_t width = 0, height = 0;
            bool srgb = false;
            bool owns = true;
        };

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

        struct GpuMaterial
        {
            std::string name;
            MaterialUBO ubo;
            RHI::RHIBuffer *ubo_buffer = nullptr;
            RHI::RHIDescriptorSet *descriptor_set = nullptr; // set 1
            GpuTexture base_color;
            GpuTexture metallic_roughness;
            GpuTexture normal;
            GpuTexture occlusion;
            GpuTexture emissive;
            bool transparent = false;
            bool alpha_mask = false;
            bool double_sided = false;
        };

        //==================================================================
        // Texture cache: deduplicates by resolved key (path or embedded ptr)
        //==================================================================
        class TextureCache
        {
        public:
            explicit TextureCache(RHI::RHIDevice *device);
            ~TextureCache();

            // Loads (or reuses) the texture referenced by `ref`.
            // Returns a default white texture when the reference is invalid or
            // loading fails, so materials always have something bindable.
            GpuTexture acquire(const TextureRef &ref, bool srgb, const std::string &fallback_key = "white");

            GpuTexture createFromImage(const ImageData &image, const std::string &key, bool srgb, bool mipmap = true);
            GpuTexture createCubeFromFaces(const std::vector<std::vector<uint8_t>> &faces_rgba8, uint32_t size,
                                           const std::string &key, bool srgb);

            // Built-in defaults, created lazily.
            const GpuTexture &white();
            const GpuTexture &black();
            const GpuTexture &flatNormal();
            const GpuTexture &grey();

            size_t count() const { return cache_.size(); }
            void clear();

        private:
            RHI::RHISampler *samplerFor(bool mipmap, RHI::AddressMode address, bool srgb);

            RHI::RHIDevice *device_ = nullptr;
            std::unordered_map<std::string, GpuTexture> cache_;
            std::unordered_map<std::string, RHI::RHISampler *> samplers_;
            GpuTexture white_, black_, normal_, grey_;
            bool defaults_created_ = false;
        };

        //==================================================================
        // Scene
        //==================================================================
        struct SceneLight
        {
            GpuLight gpu;
            Math::Vec3 position;
            Math::Vec3 direction;
            float intensity = 0.0f;
        };

        struct RenderItem
        {
            GpuMesh *mesh = nullptr;
            GpuMaterial *material = nullptr;
            Math::Mat4 model = Math::Mat4::identity();
            Math::Mat4 normal_matrix = Math::Mat4::identity();
            Math::AABB world_bounds;
            float depth = 0.0f; // distance to camera, used for sorting
            uint32_t pick_id = 0;
            ECS::Entity entity;
        };

        class RenderScene
        {
        public:
            RenderScene(RHI::RHIDevice *device, RHI::RHIResourceLayout *material_layout);
            ~RenderScene();

            TextureCache &textures() { return texture_cache_; }

            // Uploads a model (all meshes + materials).  Returns mesh indices
            // into meshes(), in model order.
            std::vector<uint32_t> uploadModel(const ModelData &model, const std::string &name_hint = "");
            uint32_t uploadMesh(const MeshData &mesh, const std::string &name);
            int32_t uploadMaterial(const MaterialData &material, const std::string &base_dir);

            GpuMesh *mesh(uint32_t index) { return index < meshes_.size() ? meshes_[index].get() : nullptr; }
            GpuMaterial *material(int32_t index) { return (index >= 0 && (size_t)index < materials_.size()) ? materials_[index].get() : nullptr; }
            size_t meshCount() const { return meshes_.size(); }
            size_t materialCount() const { return materials_.size(); }

            GpuMaterial *defaultMaterial();

            // Real-time material editing: push new scalar factors into an
            // already-uploaded material's UBO so UI/slider changes are
            // visible the next frame without re-uploading the model.
            void setMaterialBaseColor(int32_t index, const Math::Color &color);
            void setMaterialPBR(int32_t index, float metallic, float roughness);
            void setMaterialEmissive(int32_t index, const Math::Color &color, float strength);

            // Pushes a per-frame material setting (currently the shading model
            // selector) into every uploaded material UBO.  Returns the number
            // of materials updated.
            size_t updateShadingMode(RHI::RHIDevice *device, float shading_mode);

            void releaseAll();

        private:
            RHI::RHIDevice *device_ = nullptr;
            RHI::RHIResourceLayout *material_layout_ = nullptr;
            TextureCache texture_cache_;
            std::vector<std::unique_ptr<GpuMesh>> meshes_;
            std::vector<std::unique_ptr<GpuMaterial>> materials_;
            int32_t default_material_index_ = -1;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_SCENE_HPP__ */
