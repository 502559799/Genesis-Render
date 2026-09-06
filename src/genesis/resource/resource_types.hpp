/*
 * Genesis Render - resource layer data structures
 *
 * These are CPU-side, backend-agnostic representations produced by the asset
 * importers (glTF / GLB / FBX / OBJ / images) and consumed by the renderer to
 * build GPU resources.
 */
#ifndef __GENESIS_RESOURCE_TYPES_HPP__
#define __GENESIS_RESOURCE_TYPES_HPP__

#include "foundation_layer/math/math.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    //======================================================================
    // Vertex layout
    //
    // A single interleaved struct used by every mesh in the engine.  All
    // attributes are float based so that the same layout works identically on
    // the OpenGL and Vulkan backends (no integer vertex attribute formats).
    //
    //   location 0 : position  vec3   (offset  0)
    //   location 1 : normal    vec3   (offset 12)
    //   location 2 : tangent   vec4   (offset 24)   xyz = tangent, w = handedness
    //   location 3 : uv0       vec2   (offset 40)
    //   location 4 : uv1       vec2   (offset 48)
    //   location 5 : joints    vec4   (offset 56)   skinning joint indices as floats
    //   location 6 : weights   vec4   (offset 72)
    //   stride           : 88 bytes
    //======================================================================
    static constexpr uint32_t kVertexStride = 88;
    static constexpr uint32_t kMaxJointInfluences = 4;

    struct Vertex
    {
        float position[3] = {0, 0, 0};
        float normal[3] = {0, 0, 1};
        float tangent[4] = {1, 0, 0, 1};
        float uv0[2] = {0, 0};
        float uv1[2] = {0, 0};
        float joints[4] = {0, 0, 0, 0};
        float weights[4] = {1, 0, 0, 0};
    };
    static_assert(sizeof(Vertex) == kVertexStride, "Vertex layout must stay 88 bytes");

    //======================================================================
    // Images / textures
    //======================================================================
    struct ImageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 4;    // 1,2,3 or 4 (LDR, 8-bit per channel, always expanded to `channels`)
        bool is_hdr = false;      // when true, `hdr` holds RGBA float data
        bool is_cube = false;     // equirectangular HDR maps set this so the IBL baker can pick them up
        std::vector<uint8_t> pixels; // LDR data, size = width*height*channels
        std::vector<float> hdr;      // HDR data, size = width*height*4
        std::string source_path;     // original file (empty for embedded images)
        std::string name;

        size_t ldrSizeBytes() const { return (size_t)width * height * channels; }
        size_t hdrSizeBytes() const { return (size_t)width * height * 4 * sizeof(float); }

        // Expand LDR data to RGBA8 regardless of the source channel count.
        std::vector<uint8_t> asRGBA8() const
        {
            std::vector<uint8_t> out((size_t)width * height * 4);
            if (channels == 4)
            {
                out = pixels;
            }
            else if (channels == 3)
            {
                for (size_t i = 0, j = 0; i + 2 < pixels.size(); i += 3, j += 4)
                {
                    out[j + 0] = pixels[i + 0];
                    out[j + 1] = pixels[i + 1];
                    out[j + 2] = pixels[i + 2];
                    out[j + 3] = 255;
                }
            }
            else if (channels == 2)
            {
                for (size_t i = 0, j = 0; i + 1 < pixels.size(); i += 2, j += 4)
                {
                    out[j + 0] = out[j + 1] = out[j + 2] = pixels[i + 0];
                    out[j + 3] = pixels[i + 1];
                }
            }
            else
            {
                for (size_t i = 0, j = 0; i < pixels.size(); ++i, j += 4)
                    out[j + 0] = out[j + 1] = out[j + 2] = pixels[i], out[j + 3] = 255;
            }
            return out;
        }

        Math::Color sampleNearest(float u, float v) const
        {
            Math::Color c(1, 0, 1, 1);
            if (is_hdr && !hdr.empty())
            {
                uint32_t x = Math::clampf(u, 0.f, 1.f) * (width - 1);
                uint32_t y = Math::clampf(v, 0.f, 1.f) * (height - 1);
                size_t idx = ((size_t)y * width + x) * 4;
                if (idx + 3 < hdr.size()) c = Math::Color(hdr[idx], hdr[idx + 1], hdr[idx + 2], hdr[idx + 3]);
            }
            else if (!pixels.empty())
            {
                uint32_t x = (uint32_t)(Math::clampf(u, 0.f, 1.f) * (width - 1));
                uint32_t y = (uint32_t)(Math::clampf(v, 0.f, 1.f) * (height - 1));
                size_t idx = ((size_t)y * width + x) * channels;
                if (idx + channels - 1 < pixels.size())
                {
                    float r = pixels[idx] / 255.0f;
                    float g = channels > 1 ? pixels[idx + 1] / 255.0f : r;
                    float b = channels > 2 ? pixels[idx + 2] / 255.0f : r;
                    float a = channels > 3 ? pixels[idx + 3] / 255.0f : 1.0f;
                    c = Math::Color(r, g, b, a);
                }
            }
            return c;
        }
    };

    // Reference to a texture: either a path on disk or embedded image data.
    struct TextureRef
    {
        std::string path;                        // relative or absolute file path
        std::shared_ptr<ImageData> embedded;     // images embedded in glTF/FBX containers
        bool srgb = true;                        // colour textures are sRGB, data textures are not
        bool valid = false;

        std::string describe() const
        {
            if (!valid) return "<none>";
            if (!path.empty()) return path;
            if (embedded) return embedded->name.empty() ? "<embedded>" : embedded->name;
            return "<none>";
        }
    };

    //======================================================================
    // Material (metallic-roughness PBR workflow, glTF compatible)
    //======================================================================
    struct MaterialData
    {
        enum class AlphaMode : uint32_t
        {
            Opaque = 0,
            Mask = 1,
            Blend = 2,
        };

        std::string name = "material";

        // Factors (multiplied with the corresponding texture in the shader)
        Math::Color base_color_factor = Math::Color(1, 1, 1, 1);
        float metallic_factor = 1.0f;
        float roughness_factor = 1.0f;
        Math::Color emissive_factor = Math::Color(0, 0, 0, 1);
        float normal_scale = 1.0f;
        float occlusion_strength = 1.0f;
        float alpha_cutoff = 0.5f;

        AlphaMode alpha_mode = AlphaMode::Opaque;
        bool double_sided = false;

        // Textures.  `metallic_roughness` follows the glTF convention:
        //   G = roughness, B = metallic
        TextureRef base_color;
        TextureRef metallic_roughness;
        TextureRef normal;
        TextureRef occlusion;
        TextureRef emissive;

        // Convenience: scalar metallic/roughness fallbacks used when no
        // metallic-roughness texture is present.
        float metallic() const { return metallic_factor; }
        float roughness() const { return roughness_factor; }

        bool hasAnyTexture() const
        {
            return base_color.valid || metallic_roughness.valid || normal.valid || occlusion.valid || emissive.valid;
        }
    };

    //======================================================================
    // Mesh
    //======================================================================
    struct MeshData
    {
        std::string name = "mesh";
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        int32_t material_index = -1; // index into ModelData::materials
        Math::AABB bounds;
        bool has_normals = true;
        bool has_tangents = false;
        bool has_uv1 = false;
        bool has_skin = false;

        // Fills in missing normals (flat shading) and tangents (from uv0).
        void computeNormals();
        void computeTangents();
        void computeBounds();
    };

    //======================================================================
    // Node hierarchy (kept from glTF/FBX so multi-mesh models stay intact)
    //======================================================================
    struct NodeData
    {
        std::string name = "node";
        Math::Mat4 local_transform = Math::Mat4::identity();
        std::vector<int32_t> children;
        std::vector<int32_t> meshes; // indices into ModelData::meshes
    };

    //======================================================================
    // A whole imported model
    //======================================================================
    struct ModelData
    {
        std::string name = "model";
        std::string source_path;
        std::vector<MeshData> meshes;
        std::vector<MaterialData> materials;
        std::vector<std::shared_ptr<ImageData>> images; // embedded images (may be empty)
        std::vector<NodeData> nodes;
        std::vector<int32_t> root_nodes;
        Math::AABB bounds;
        std::string importer; // "gltf" | "fbx" | "obj" | "procedural"

        // Collapses the node hierarchy into a single list of world-space meshes.
        // Used by the simple (non-animated) render path.
        struct PlacedMesh
        {
            int32_t mesh_index = -1;
            Math::Mat4 transform = Math::Mat4::identity();
            std::string node_name;
        };
        std::vector<PlacedMesh> flattenNodes() const;

        void computeBounds();
    };

    //======================================================================
    // Importer entry points
    //======================================================================
    namespace ResourceIO
    {
        // Supported extensions: .gltf .glb .fbx .obj (case insensitive)
        bool isSupportedModelFile(const std::string &path);
        std::string modelFileKind(const std::string &path); // "gltf" | "glb" | "fbx" | "obj" | ""

        // Loads any supported model format.  Returns nullptr on failure and logs
        // the reason.
        std::shared_ptr<ModelData> loadModel(const std::string &path);

        std::shared_ptr<ModelData> loadGltfModel(const std::string &path);
        std::shared_ptr<ModelData> loadFbxModel(const std::string &path);
        std::shared_ptr<ModelData> loadObjModel(const std::string &path);

        // Image loading (png/jpg/tga/bmp/hdr) via stb_image.
        std::shared_ptr<ImageData> loadImage(const std::string &path, bool force_rgba = true);
        // Decodes an image embedded in a container (glTF buffer / FBX video).
        std::shared_ptr<ImageData> loadImageFromMemory(const uint8_t *data, size_t size, const std::string &name);

        // Whole-file helpers
        bool readFile(const std::string &path, std::vector<uint8_t> &out);
        bool readFileText(const std::string &path, std::string &out);
        bool writeFile(const std::string &path, const void *data, size_t size);

        // Image writers used for screenshots and golden-image test artefacts.
        bool writeImagePNG(const std::string &path, uint32_t width, uint32_t height, const uint8_t *rgba);
        bool writeImageTGA(const std::string &path, uint32_t width, uint32_t height, const uint8_t *rgba);
        std::string pathExtension(const std::string &path);      // lower case, without dot
        std::string pathDirectory(const std::string &path);
        std::string pathFilename(const std::string &path);
        std::string joinPath(const std::string &a, const std::string &b);
        bool fileExists(const std::string &path);
    } // namespace ResourceIO

} // namespace Genesis

#endif /* __GENESIS_RESOURCE_TYPES_HPP__ */
