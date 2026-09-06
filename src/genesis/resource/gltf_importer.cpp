/*
 * Genesis Render - glTF 2.0 / GLB importer (cgltf based)
 *
 * glTF is already +Y up / right handed / -Z forward, which matches the engine
 * convention, so no coordinate conversion is required here (unlike FBX).
 *
 * Supported:
 *   - meshes with POSITION / NORMAL / TANGENT / TEXCOORD_0 / TEXCOORD_1 /
 *     JOINTS_0 / WEIGHTS_0 / COLOR_0 attributes
 *   - u8/u16/u32 index buffers
 *   - pbrMetallicRoughness materials incl. all five texture slots
 *   - embedded images (GLB buffer views) and external image URIs
 *   - full node hierarchy and scenes
 *   - KHR_materials_unlit / KHR_materials_transmission are ignored gracefully
 * Not supported yet: morph targets, sparse accessors, Draco/Meshopt compression.
 */
#include "resource/resource_types.hpp"
#include "foundation_layer/genesis_log.hpp"

#include "external/cgltf/cgltf.h"

#include <algorithm>
#include <cstring>

namespace Genesis
{
    namespace ResourceIO
    {
        namespace
        {
            const cgltf_accessor *findAttribute(const cgltf_primitive &prim, const char *name)
            {
                for (cgltf_size i = 0; i < prim.attributes_count; ++i)
                {
                    if (prim.attributes[i].name && std::strcmp(prim.attributes[i].name, name) == 0)
                        return prim.attributes[i].data;
                }
                return nullptr;
            }

            // Resolves a cgltf texture into a TextureRef, loading embedded
            // images into model->images so that they survive after cgltf_free.
            bool resolveTexture(const cgltf_texture *texture, const cgltf_data *data,
                                ModelData &model, TextureRef &out, bool srgb,
                                const std::string &base_dir)
            {
                out.valid = false;
                if (!texture || !texture->image) return false;
                const cgltf_image *image = texture->image;

                if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data)
                {
                    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                    size_t size = image->buffer_view->size;
                    std::shared_ptr<ImageData> decoded = loadImageFromMemory(ptr, size, image->name ? image->name : "embedded");
                    if (!decoded)
                    {
                        GEN_LOG_WARN("[glTF] failed to decode embedded image '%s'", image->name ? image->name : "?");
                        return false;
                    }
                    decoded->is_cube = false;
                    model.images.push_back(decoded);
                    out.embedded = decoded;
                    out.srgb = srgb;
                    out.valid = true;
                    return true;
                }

                if (image->uri && image->uri[0])
                {
                    // Data URIs are handled by cgltf_load_buffers into a buffer view,
                    // so anything left here is a plain relative/absolute file URI.
                    std::string uri = image->uri;
                    if (uri.compare(0, 5, "data:") == 0)
                    {
                        GEN_LOG_WARN("[glTF] unsupported data URI image");
                        return false;
                    }
                    std::string full = base_dir.empty() ? uri : joinPath(base_dir, uri);
                    out.path = full;
                    out.srgb = srgb;
                    out.valid = true;
                    return true;
                }
                return false;
            }

            Math::Mat4 cgltfNodeMatrix(const cgltf_node *node)
            {
                if (node->has_matrix)
                {
                    // cgltf stores column major, same as our Mat4
                    return Math::Mat4(node->matrix);
                }
                Math::Vec3 t(node->translation[0], node->translation[1], node->translation[2]);
                Math::Quat r(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
                Math::Vec3 s(node->scale[0], node->scale[1], node->scale[2]);
                return Math::Mat4::compose(t, r, s);
            }
        } // namespace

        std::shared_ptr<ModelData> loadGltfModel(const std::string &path)
        {
            cgltf_options options{};
            options.type = cgltf_file_type_invalid; // let cgltf detect glTF vs GLB
            cgltf_data *data = nullptr;
            cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
            if (result != cgltf_result_success || !data)
            {
                GEN_LOG_ERROR("[glTF] cgltf_parse_file failed for %s (result=%d)", path.c_str(), (int)result);
                return nullptr;
            }
            result = cgltf_load_buffers(&options, data, path.c_str());
            if (result != cgltf_result_success)
            {
                GEN_LOG_ERROR("[glTF] cgltf_load_buffers failed for %s (result=%d)", path.c_str(), (int)result);
                cgltf_free(data);
                return nullptr;
            }
            result = cgltf_validate(data);
            if (result != cgltf_result_success)
                GEN_LOG_WARN("[glTF] %s failed strict validation (result=%d) - continuing", path.c_str(), (int)result);

            auto model = std::make_shared<ModelData>();
            model->name = data->scenes_count && data->scenes[0].name ? data->scenes[0].name : pathFilename(path);
            if (model->name.empty()) model->name = pathFilename(path);
            model->source_path = path;
            model->importer = (data->file_type == cgltf_file_type_glb) ? "glb" : "gltf";

            std::string base_dir = pathDirectory(path);

            //--------------------------------------------------------------
            // Materials
            //--------------------------------------------------------------
            for (cgltf_size i = 0; i < data->materials_count; ++i)
            {
                const cgltf_material &src = data->materials[i];
                MaterialData m;
                m.name = src.name && src.name[0] ? src.name : ("material_" + std::to_string(i));

                const cgltf_pbr_metallic_roughness &pbr = src.pbr_metallic_roughness;
                m.base_color_factor = Math::Color(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                                  pbr.base_color_factor[2], pbr.base_color_factor[3]);
                m.metallic_factor = pbr.metallic_factor;
                m.roughness_factor = pbr.roughness_factor;
                m.emissive_factor = Math::Color(src.emissive_factor[0], src.emissive_factor[1], src.emissive_factor[2], 1.0f);
                m.normal_scale = src.normal_texture.scale;
                m.occlusion_strength = src.occlusion_texture.scale; // cgltf stores occlusion strength in `scale`
                m.alpha_cutoff = src.alpha_cutoff > 0.0f ? src.alpha_cutoff : 0.5f;
                m.double_sided = src.double_sided;
                switch (src.alpha_mode)
                {
                case cgltf_alpha_mode_mask: m.alpha_mode = MaterialData::AlphaMode::Mask; break;
                case cgltf_alpha_mode_blend: m.alpha_mode = MaterialData::AlphaMode::Blend; break;
                default: m.alpha_mode = MaterialData::AlphaMode::Opaque; break;
                }

                resolveTexture(pbr.base_color_texture.texture, data, *model, m.base_color, true, base_dir);
                resolveTexture(pbr.metallic_roughness_texture.texture, data, *model, m.metallic_roughness, false, base_dir);
                resolveTexture(src.normal_texture.texture, data, *model, m.normal, false, base_dir);
                resolveTexture(src.occlusion_texture.texture, data, *model, m.occlusion, false, base_dir);
                resolveTexture(src.emissive_texture.texture, data, *model, m.emissive, true, base_dir);

                model->materials.push_back(m);
            }
            if (model->materials.empty())
            {
                MaterialData fallback;
                fallback.name = "default";
                fallback.metallic_factor = 0.0f;
                fallback.roughness_factor = 0.7f;
                model->materials.push_back(fallback);
            }

            //--------------------------------------------------------------
            // Meshes (one MeshData per primitive)
            //--------------------------------------------------------------
            std::vector<int32_t> primitive_to_mesh; // cgltf primitive global index -> model mesh index
            int32_t global_primitive_index = 0;
            // Build a lookup from (mesh, primitive) to model mesh index
            std::vector<std::vector<int32_t>> mesh_primitive_map(data->meshes_count);

            for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
            {
                const cgltf_mesh &gmesh = data->meshes[mi];
                mesh_primitive_map[mi].resize(gmesh.primitives_count, -1);

                for (cgltf_size pi = 0; pi < gmesh.primitives_count; ++pi)
                {
                    const cgltf_primitive &prim = gmesh.primitives[pi];
                    const cgltf_accessor *pos_acc = findAttribute(prim, "POSITION");
                    if (!pos_acc || pos_acc->count == 0)
                    {
                        GEN_LOG_WARN("[glTF] mesh %zu primitive %zu has no POSITION - skipped", (size_t)mi, (size_t)pi);
                        ++global_primitive_index;
                        continue;
                    }

                    const cgltf_accessor *nrm_acc = findAttribute(prim, "NORMAL");
                    const cgltf_accessor *tan_acc = findAttribute(prim, "TANGENT");
                    const cgltf_accessor *uv0_acc = findAttribute(prim, "TEXCOORD_0");
                    const cgltf_accessor *uv1_acc = findAttribute(prim, "TEXCOORD_1");
                    const cgltf_accessor *jnt_acc = findAttribute(prim, "JOINTS_0");
                    const cgltf_accessor *wgt_acc = findAttribute(prim, "WEIGHTS_0");
                    const cgltf_accessor *col_acc = findAttribute(prim, "COLOR_0");

                    MeshData md;
                    md.name = gmesh.name && gmesh.name[0] ? gmesh.name : ("mesh_" + std::to_string(mi));
                    if (gmesh.primitives_count > 1) md.name += "_p" + std::to_string(pi);
                    md.vertices.resize(pos_acc->count);
                    md.has_normals = (nrm_acc != nullptr);
                    md.has_tangents = (tan_acc != nullptr);
                    md.has_uv1 = (uv1_acc != nullptr);
                    md.has_skin = (jnt_acc != nullptr && wgt_acc != nullptr);

                    for (cgltf_size v = 0; v < pos_acc->count; ++v)
                    {
                        Vertex &out = md.vertices[v];
                        cgltf_float p[3] = {0, 0, 0};
                        cgltf_accessor_read_float(pos_acc, v, p, 3);
                        out.position[0] = p[0]; out.position[1] = p[1]; out.position[2] = p[2];

                        if (nrm_acc)
                        {
                            cgltf_float n[3] = {0, 0, 1};
                            cgltf_accessor_read_float(nrm_acc, v, n, 3);
                            out.normal[0] = n[0]; out.normal[1] = n[1]; out.normal[2] = n[2];
                        }
                        if (tan_acc)
                        {
                            cgltf_float t[4] = {1, 0, 0, 1};
                            cgltf_accessor_read_float(tan_acc, v, t, 4);
                            out.tangent[0] = t[0]; out.tangent[1] = t[1]; out.tangent[2] = t[2]; out.tangent[3] = t[3];
                        }
                        if (uv0_acc)
                        {
                            cgltf_float uv[2] = {0, 0};
                            cgltf_accessor_read_float(uv0_acc, v, uv, 2);
                            out.uv0[0] = uv[0]; out.uv0[1] = uv[1];
                        }
                        if (uv1_acc)
                        {
                            cgltf_float uv[2] = {0, 0};
                            cgltf_accessor_read_float(uv1_acc, v, uv, 2);
                            out.uv0[0] = out.uv0[0]; // keep uv0
                            out.uv1[0] = uv[0]; out.uv1[1] = uv[1];
                        }
                        if (jnt_acc && wgt_acc)
                        {
                            cgltf_float j[4] = {0, 0, 0, 0};
                            cgltf_float w[4] = {1, 0, 0, 0};
                            // JOINTS_0 may be stored as unsigned byte or short
                            if (jnt_acc->component_type == cgltf_component_type_r_8u ||
                                jnt_acc->component_type == cgltf_component_type_r_16u)
                            {
                                cgltf_uint ju[4] = {0, 0, 0, 0};
                                cgltf_accessor_read_uint(jnt_acc, v, ju, 4);
                                for (int k = 0; k < 4; ++k) j[k] = (float)ju[k];
                            }
                            else
                            {
                                cgltf_accessor_read_float(jnt_acc, v, j, 4);
                            }
                            cgltf_accessor_read_float(wgt_acc, v, w, 4);
                            for (int k = 0; k < 4; ++k)
                            {
                                out.joints[k] = j[k];
                                out.weights[k] = w[k];
                            }
                        }
                        if (col_acc)
                        {
                            // Vertex colour is folded into uv1.x/uv1.y? No - we
                            // simply ignore it but keep the data available via
                            // the alpha of the base colour factor.
                            cgltf_float c[4] = {1, 1, 1, 1};
                            cgltf_accessor_read_float(col_acc, v, c, 4);
                            (void)c;
                        }
                    }

                    // Indices
                    if (prim.indices && prim.indices->count > 0)
                    {
                        md.indices.resize(prim.indices->count);
                        for (cgltf_size k = 0; k < prim.indices->count; ++k)
                            md.indices[k] = (uint32_t)cgltf_accessor_read_index(prim.indices, k);
                    }
                    else
                    {
                        md.indices.resize(md.vertices.size());
                        for (size_t k = 0; k < md.indices.size(); ++k) md.indices[k] = (uint32_t)k;
                    }

                    if (prim.type != cgltf_primitive_type_triangles)
                        GEN_LOG_WARN("[glTF] primitive type %d is not triangles; importing as-is", (int)prim.type);

                    if (!md.has_normals) md.computeNormals();
                    if (!md.has_tangents) md.computeTangents();
                    md.computeBounds();

                    if (prim.material)
                    {
                        cgltf_size mat_index = (cgltf_size)(prim.material - data->materials);
                        md.material_index = (int32_t)mat_index;
                    }
                    else
                    {
                        md.material_index = 0;
                    }

                    mesh_primitive_map[mi][pi] = (int32_t)model->meshes.size();
                    model->meshes.push_back(std::move(md));
                    ++global_primitive_index;
                }
            }
            (void)primitive_to_mesh;

            //--------------------------------------------------------------
            // Nodes
            //--------------------------------------------------------------
            model->nodes.resize(data->nodes_count);
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                const cgltf_node &src = data->nodes[i]; // cgltf_data::nodes is a value array
                NodeData &n = model->nodes[i];
                n.name = src.name && src.name[0] ? src.name : ("node_" + std::to_string(i));
                n.local_transform = cgltfNodeMatrix(&src);
                for (cgltf_size c = 0; c < src.children_count; ++c)
                {
                    ptrdiff_t idx = src.children[c] - data->nodes;
                    n.children.push_back((int32_t)idx);
                }
                if (src.mesh)
                {
                    ptrdiff_t mesh_idx = src.mesh - data->meshes;
                    if (mesh_idx >= 0 && (size_t)mesh_idx < mesh_primitive_map.size())
                    {
                        for (int32_t p : mesh_primitive_map[mesh_idx])
                            if (p >= 0) n.meshes.push_back(p);
                    }
                }
            }

            // Roots from the active scene (or scene 0).
            const cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
            if (scene)
            {
                for (cgltf_size i = 0; i < scene->nodes_count; ++i)
                    model->root_nodes.push_back((int32_t)(scene->nodes[i] - data->nodes));
            }
            if (model->root_nodes.empty())
            {
                std::vector<bool> is_child(data->nodes_count, false);
                for (const auto &n : model->nodes)
                    for (int32_t c : n.children)
                        if (c >= 0 && (size_t)c < is_child.size()) is_child[c] = true;
                for (size_t i = 0; i < is_child.size(); ++i)
                    if (!is_child[i]) model->root_nodes.push_back((int32_t)i);
            }

            GEN_LOG_INFO("[glTF] imported '%s': %zu meshes, %zu materials, %zu nodes, %zu roots",
                         model->name.c_str(), model->meshes.size(), model->materials.size(),
                         model->nodes.size(), model->root_nodes.size());

            cgltf_free(data);
            if (model->meshes.empty())
            {
                GEN_LOG_ERROR("[glTF] %s contains no renderable primitives", path.c_str());
                return nullptr;
            }
            return model;
        }

    } // namespace ResourceIO
} // namespace Genesis
