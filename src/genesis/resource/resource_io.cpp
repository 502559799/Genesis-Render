/*
 * Genesis Render - resource IO: files, images, model format dispatch, OBJ
 */
#include "resource/resource_types.hpp"
#include "foundation_layer/genesis_log.hpp"

#include "external/stb_image.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <zlib.h>

namespace Genesis
{
    namespace ResourceIO
    {
        //==================================================================
        // Paths & files
        //==================================================================
        std::string pathExtension(const std::string &path)
        {
            size_t dot = path.find_last_of('.');
            size_t slash = path.find_last_of("/\\");
            if (dot == std::string::npos) return "";
            if (slash != std::string::npos && dot < slash) return "";
            std::string ext = path.substr(dot + 1);
            for (auto &c : ext) c = (char)std::tolower((unsigned char)c);
            return ext;
        }

        std::string pathDirectory(const std::string &path)
        {
            size_t slash = path.find_last_of("/\\");
            if (slash == std::string::npos) return "";
            return path.substr(0, slash);
        }

        std::string pathFilename(const std::string &path)
        {
            size_t slash = path.find_last_of("/\\");
            if (slash == std::string::npos) return path;
            return path.substr(slash + 1);
        }

        std::string joinPath(const std::string &a, const std::string &b)
        {
            if (a.empty()) return b;
            if (b.empty()) return a;
            if (a.back() == '/' || a.back() == '\\') return a + b;
            return a + "/" + b;
        }

        bool fileExists(const std::string &path)
        {
            struct stat st;
            return ::stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) == 0;
        }

        bool readFile(const std::string &path, std::vector<uint8_t> &out)
        {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f.is_open())
            {
                GEN_LOG_ERROR("[IO] cannot open file: %s", path.c_str());
                return false;
            }
            std::streamsize size = f.tellg();
            if (size < 0) return false;
            f.seekg(0, std::ios::beg);
            out.resize((size_t)size);
            if (size > 0 && !f.read(reinterpret_cast<char *>(out.data()), size))
            {
                GEN_LOG_ERROR("[IO] failed reading %lld bytes from %s", (long long)size, path.c_str());
                out.clear();
                return false;
            }
            return true;
        }

        bool readFileText(const std::string &path, std::string &out)
        {
            std::vector<uint8_t> data;
            if (!readFile(path, data)) return false;
            out.assign(reinterpret_cast<const char *>(data.data()), data.size());
            return true;
        }

        bool writeFile(const std::string &path, const void *data, size_t size)
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f.is_open())
            {
                GEN_LOG_ERROR("[IO] cannot write file: %s", path.c_str());
                return false;
            }
            f.write(reinterpret_cast<const char *>(data), (std::streamsize)size);
            return f.good();
        }

        //==================================================================
        // Image writers (PNG via zlib, uncompressed TGA)
        //==================================================================
        namespace
        {
            void pngChunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t size)
            {
                uint8_t len_be[4] = {(uint8_t)(size >> 24), (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size};
                out.insert(out.end(), len_be, len_be + 4);
                size_t crc_start = out.size();
                out.insert(out.end(), type, type + 4);
                if (data && size) out.insert(out.end(), data, data + size);
                uLong crc = crc32(0L, Z_NULL, 0);
                crc = crc32(crc, out.data() + crc_start, (uInt)(out.size() - crc_start));
                uint8_t crc_be[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
                out.insert(out.end(), crc_be, crc_be + 4);
            }
        } // namespace

        bool writeImagePNG(const std::string &path, uint32_t width, uint32_t height, const uint8_t *rgba)
        {
            if (!rgba || width == 0 || height == 0) return false;

            // Raw scanlines with a filter byte (0 = None) per row.
            std::vector<uint8_t> raw;
            raw.reserve((size_t)width * height * 5);
            for (uint32_t y = 0; y < height; ++y)
            {
                raw.push_back(0);
                const uint8_t *row = rgba + (size_t)y * width * 4;
                raw.insert(raw.end(), row, row + (size_t)width * 4);
            }

            uLongf compressed_size = compressBound((uLong)raw.size());
            std::vector<uint8_t> compressed(compressed_size);
            if (compress2(compressed.data(), &compressed_size, raw.data(), (uLong)raw.size(), 9) != Z_OK)
            {
                GEN_LOG_ERROR("[IO] PNG deflate failed for %s", path.c_str());
                return false;
            }
            compressed.resize(compressed_size);

            std::vector<uint8_t> out;
            const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            out.insert(out.end(), signature, signature + 8);

            uint8_t ihdr[13];
            ihdr[0] = (uint8_t)(width >> 24); ihdr[1] = (uint8_t)(width >> 16);
            ihdr[2] = (uint8_t)(width >> 8); ihdr[3] = (uint8_t)width;
            ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
            ihdr[6] = (uint8_t)(height >> 8); ihdr[7] = (uint8_t)height;
            ihdr[8] = 8;  // bit depth
            ihdr[9] = 6;  // colour type: RGBA
            ihdr[10] = 0; // compression
            ihdr[11] = 0; // filter
            ihdr[12] = 0; // interlace
            pngChunk(out, "IHDR", ihdr, sizeof(ihdr));
            pngChunk(out, "IDAT", compressed.data(), compressed.size());
            pngChunk(out, "IEND", nullptr, 0);

            return writeFile(path, out.data(), out.size());
        }

        bool writeImageTGA(const std::string &path, uint32_t width, uint32_t height, const uint8_t *rgba)
        {
            if (!rgba || width == 0 || height == 0) return false;
            std::vector<uint8_t> out;
            out.resize(18, 0);
            out[2] = 2; // uncompressed true colour
            out[12] = (uint8_t)(width & 0xFF);
            out[13] = (uint8_t)((width >> 8) & 0xFF);
            out[14] = (uint8_t)(height & 0xFF);
            out[15] = (uint8_t)((height >> 8) & 0xFF);
            out[16] = 32;  // bits per pixel
            out[17] = 0x28; // top-left origin + 8 alpha bits
            out.reserve(18 + (size_t)width * height * 4);
            for (size_t i = 0; i < (size_t)width * height; ++i)
            {
                out.push_back(rgba[i * 4 + 2]); // B
                out.push_back(rgba[i * 4 + 1]); // G
                out.push_back(rgba[i * 4 + 0]); // R
                out.push_back(rgba[i * 4 + 3]); // A
            }
            return writeFile(path, out.data(), out.size());
        }

        //==================================================================
        // Images
        //==================================================================
        std::shared_ptr<ImageData> loadImage(const std::string &path, bool force_rgba)
        {
            std::string ext = pathExtension(path);
            std::shared_ptr<ImageData> image = std::make_shared<ImageData>();
            image->source_path = path;
            image->name = pathFilename(path);

            if (ext == "hdr")
            {
                int w = 0, h = 0, c = 0;
                float *data = stbi_loadf(path.c_str(), &w, &h, &c, 4);
                if (!data)
                {
                    GEN_LOG_ERROR("[IO] stbi_loadf failed for HDR image: %s (%s)", path.c_str(), stbi_failure_reason());
                    return nullptr;
                }
                image->width = (uint32_t)w;
                image->height = (uint32_t)h;
                image->channels = 4;
                image->is_hdr = true;
                image->hdr.assign(data, data + (size_t)w * h * 4);
                stbi_image_free(data);
                GEN_LOG_INFO("[IO] loaded HDR image %s (%dx%d)", path.c_str(), w, h);
                return image;
            }

            int w = 0, h = 0, c = 0;
            int desired = force_rgba ? 4 : 0;
            stbi_uc *data = stbi_load(path.c_str(), &w, &h, &c, desired);
            if (!data)
            {
                GEN_LOG_ERROR("[IO] stbi_load failed for image: %s (%s)", path.c_str(), stbi_failure_reason());
                return nullptr;
            }
            image->width = (uint32_t)w;
            image->height = (uint32_t)h;
            image->channels = (uint32_t)(desired ? desired : c);
            image->is_hdr = false;
            image->pixels.assign(data, data + (size_t)w * h * image->channels);
            stbi_image_free(data);
            return image;
        }

        // Decodes an image that is embedded in a container (glTF/FBX).
        std::shared_ptr<ImageData> loadImageFromMemory(const uint8_t *data, size_t size, const std::string &name)
        {
            std::shared_ptr<ImageData> image = std::make_shared<ImageData>();
            image->name = name;

            // Detect Radiance HDR by its magic.
            if (size > 10 && std::memcmp(data, "#?RADIANCE", 10) == 0)
            {
                int w = 0, h = 0, c = 0;
                float *pixels = stbi_loadf_from_memory(data, (int)size, &w, &h, &c, 4);
                if (!pixels) return nullptr;
                image->width = (uint32_t)w;
                image->height = (uint32_t)h;
                image->channels = 4;
                image->is_hdr = true;
                image->hdr.assign(pixels, pixels + (size_t)w * h * 4);
                stbi_image_free(pixels);
                return image;
            }

            int w = 0, h = 0, c = 0;
            stbi_uc *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
            if (!pixels) return nullptr;
            image->width = (uint32_t)w;
            image->height = (uint32_t)h;
            image->channels = 4;
            image->is_hdr = false;
            image->pixels.assign(pixels, pixels + (size_t)w * h * 4);
            stbi_image_free(pixels);
            return image;
        }

        //==================================================================
        // Model format dispatch
        //==================================================================
        std::string modelFileKind(const std::string &path)
        {
            std::string ext = pathExtension(path);
            if (ext == "gltf") return "gltf";
            if (ext == "glb") return "glb";
            if (ext == "fbx") return "fbx";
            if (ext == "obj") return "obj";
            return "";
        }

        bool isSupportedModelFile(const std::string &path)
        {
            return !modelFileKind(path).empty();
        }

        std::shared_ptr<ModelData> loadModel(const std::string &path)
        {
            std::string kind = modelFileKind(path);
            if (kind.empty())
            {
                GEN_LOG_ERROR("[IO] unsupported model format: %s", path.c_str());
                return nullptr;
            }
            GEN_LOG_INFO("[IO] loading %s model: %s", kind.c_str(), path.c_str());
            std::shared_ptr<ModelData> model;
            if (kind == "gltf" || kind == "glb") model = loadGltfModel(path);
            else if (kind == "fbx") model = loadFbxModel(path);
            else if (kind == "obj") model = loadObjModel(path);

            if (model)
            {
                model->computeBounds();
                GEN_LOG_INFO("[IO] model '%s': %zu mesh(es), %zu material(s), %zu image(s), bounds [%0.2f,%0.2f,%0.2f]-[%0.2f,%0.2f,%0.2f]",
                             model->name.c_str(), model->meshes.size(), model->materials.size(), model->images.size(),
                             model->bounds.minPoint.x, model->bounds.minPoint.y, model->bounds.minPoint.z,
                             model->bounds.maxPoint.x, model->bounds.maxPoint.y, model->bounds.maxPoint.z);
            }
            return model;
        }

        //==================================================================
        // Wavefront OBJ importer (kept small on purpose - glTF/FBX are the
        // primary paths, OBJ exists so that legacy assets still load).
        //==================================================================
        std::shared_ptr<ModelData> loadObjModel(const std::string &path)
        {
            std::string text;
            if (!readFileText(path, text)) return nullptr;

            auto model = std::make_shared<ModelData>();
            model->name = pathFilename(path);
            model->source_path = path;
            model->importer = "obj";

            std::vector<Math::Vec3> positions;
            std::vector<Math::Vec3> normals;
            std::vector<Math::Vec2> uvs;

            struct FaceVertex
            {
                int p = -1, t = -1, n = -1;
            };
            struct ObjMesh
            {
                std::string name;
                int material = -1;
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;
            };
            std::vector<ObjMesh> meshes;
            std::vector<MaterialData> materials;

            meshes.push_back(ObjMesh());
            meshes.back().name = "default";

            // material library lookup by name
            std::vector<std::pair<std::string, int>> material_index;

            auto findOrCreateMaterial = [&](const std::string &name) -> int
            {
                for (auto &mi : material_index)
                    if (mi.first == name) return mi.second;
                MaterialData m;
                m.name = name;
                m.metallic_factor = 0.0f;
                m.roughness_factor = 0.6f;
                int idx = (int)materials.size();
                materials.push_back(m);
                material_index.push_back({name, idx});
                return idx;
            };

            std::istringstream stream(text);
            std::string line;
            std::vector<FaceVertex> face;
            while (std::getline(stream, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream ls(line);
                std::string tag;
                ls >> tag;
                if (tag == "v")
                {
                    Math::Vec3 p;
                    ls >> p.x >> p.y >> p.z;
                    positions.push_back(p);
                }
                else if (tag == "vn")
                {
                    Math::Vec3 n;
                    ls >> n.x >> n.y >> n.z;
                    normals.push_back(n);
                }
                else if (tag == "vt")
                {
                    Math::Vec2 t;
                    ls >> t.x >> t.y;
                    uvs.push_back(t);
                }
                else if (tag == "o" || tag == "g")
                {
                    std::string nm;
                    std::getline(ls, nm);
                    size_t start = nm.find_first_not_of(" \t\r");
                    if (start != std::string::npos) nm = nm.substr(start);
                    if (!meshes.back().vertices.empty() || !meshes.back().name.empty())
                    {
                        meshes.push_back(ObjMesh());
                    }
                    meshes.back().name = nm.empty() ? "mesh" : nm;
                }
                else if (tag == "usemtl")
                {
                    std::string nm;
                    std::getline(ls, nm);
                    size_t start = nm.find_first_not_of(" \t\r");
                    if (start != std::string::npos) nm = nm.substr(start);
                    if (!meshes.back().vertices.empty())
                    {
                        ObjMesh next;
                        next.name = meshes.back().name;
                        next.material = findOrCreateMaterial(nm);
                        meshes.push_back(next);
                    }
                    meshes.back().material = findOrCreateMaterial(nm);
                }
                else if (tag == "f")
                {
                    face.clear();
                    std::string token;
                    while (ls >> token)
                    {
                        FaceVertex fv;
                        // v, v/vt, v//vn, v/vt/vn
                        size_t p1 = token.find('/');
                        if (p1 == std::string::npos)
                        {
                            fv.p = std::atoi(token.c_str());
                        }
                        else
                        {
                            fv.p = std::atoi(token.substr(0, p1).c_str());
                            size_t p2 = token.find('/', p1 + 1);
                            if (p2 == std::string::npos)
                            {
                                if (p1 + 1 < token.size()) fv.t = std::atoi(token.substr(p1 + 1).c_str());
                            }
                            else
                            {
                                if (p2 > p1 + 1) fv.t = std::atoi(token.substr(p1 + 1, p2 - p1 - 1).c_str());
                                if (p2 + 1 < token.size()) fv.n = std::atoi(token.substr(p2 + 1).c_str());
                            }
                        }
                        face.push_back(fv);
                    }
                    if (face.size() < 3) continue;

                    ObjMesh &m = meshes.back();
                    auto pushVertex = [&](const FaceVertex &fv) -> uint32_t
                    {
                        Vertex v;
                        auto wrap = [](int index, size_t count) -> int
                        {
                            if (index == 0) return -1;
                            if (index > 0) return index - 1;
                            return (int)count + index; // negative index = relative
                        };
                        int pi = wrap(fv.p, positions.size());
                        int ti = wrap(fv.t, uvs.size());
                        int ni = wrap(fv.n, normals.size());
                        if (pi >= 0 && (size_t)pi < positions.size())
                        {
                            v.position[0] = positions[pi].x;
                            v.position[1] = positions[pi].y;
                            v.position[2] = positions[pi].z;
                        }
                        if (ti >= 0 && (size_t)ti < uvs.size())
                        {
                            v.uv0[0] = uvs[ti].x;
                            v.uv0[1] = 1.0f - uvs[ti].y; // OBJ v origin is bottom-left
                        }
                        if (ni >= 0 && (size_t)ni < normals.size())
                        {
                            v.normal[0] = normals[ni].x;
                            v.normal[1] = normals[ni].y;
                            v.normal[2] = normals[ni].z;
                        }
                        m.vertices.push_back(v);
                        return (uint32_t)m.vertices.size() - 1;
                    };

                    // fan triangulation for polygons with more than 3 vertices
                    for (size_t i = 2; i < face.size(); ++i)
                    {
                        m.indices.push_back(pushVertex(face[0]));
                        m.indices.push_back(pushVertex(face[i - 1]));
                        m.indices.push_back(pushVertex(face[i]));
                    }
                }
            }

            for (auto &m : meshes)
            {
                if (m.vertices.empty()) continue;
                MeshData md;
                md.name = m.name;
                md.vertices = std::move(m.vertices);
                md.indices = std::move(m.indices);
                md.material_index = m.material;
                md.has_uv1 = false;
                bool has_normals = true;
                for (const auto &v : md.vertices)
                {
                    if (v.normal[0] == 0.0f && v.normal[1] == 0.0f && v.normal[2] == 1.0f) { /* default */ }
                    (void)has_normals;
                }
                md.computeNormals();
                md.computeTangents();
                md.computeBounds();
                model->meshes.push_back(std::move(md));
            }
            model->materials = std::move(materials);

            // One node per mesh.
            for (size_t i = 0; i < model->meshes.size(); ++i)
            {
                NodeData n;
                n.name = model->meshes[i].name;
                n.local_transform = Math::Mat4::identity();
                n.meshes.push_back((int32_t)i);
                model->nodes.push_back(n);
                model->root_nodes.push_back((int32_t)(model->nodes.size() - 1));
            }
            if (model->meshes.empty())
            {
                GEN_LOG_ERROR("[OBJ] no geometry found in %s", path.c_str());
                return nullptr;
            }
            return model;
        }

    } // namespace ResourceIO
} // namespace Genesis
