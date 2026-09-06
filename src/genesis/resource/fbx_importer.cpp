/*
 * Genesis Render - native FBX importer (binary + basic ASCII)
 *
 * Hand-written Kaydara FBX parser.  No Autodesk FBX SDK, no assimp: only the
 * standard library plus zlib (already linked by the engine) for compressed
 * array properties.
 *
 * ---------------------------------------------------------------------------
 * FBX binary format notes ("Kaydara FBX Binary", versions 6.x / 7.x)
 * ---------------------------------------------------------------------------
 * File header (27 bytes, little endian):
 *   bytes  0..20 : "Kaydara FBX Binary  \x00"  (magic, two trailing spaces + NUL)
 *   bytes 21..22 : 0x1A 0x00                   (constant observed in all files)
 *   bytes 23..26 : uint32 file version         (e.g. 6100, 7300, 7400, 7700)
 *
 * Node record:
 *   version  < 7500 : EndOffset u32 | NumProperties u32 | PropertyListLen u32 | NameLen u8
 *   version >= 7500 : EndOffset u64 | NumProperties u64 | PropertyListLen u64 | NameLen u8
 *   then NameLen bytes of name (not NUL terminated), then the property list
 *   (exactly PropertyListLen bytes), then optional nested child records.  A
 *   node that has nested children terminates the child list with a NULL
 *   record (header-size zero bytes) which still lies INSIDE EndOffset.
 *   EndOffset is the absolute file offset of the end of this node (including
 *   the NULL record).  EndOffset == 0 with NameLen == 0 IS the NULL record.
 *
 * Property types (single tag byte):
 *   Y = int16, C = uint8 (bool), I = int32, L = int64, F = float32, D = float64
 *   S = string (u32 length + bytes; object names embed "\x00\x01ClassName")
 *   R = raw bytes (u32 length + bytes)
 *   f / d / l / i / b = typed arrays:
 *       u32 ArrayLength | u32 Encoding | u32 CompressedLength | data
 *       Encoding 0 -> raw little-endian elements (CompressedLength == raw size)
 *       Encoding 1 -> zlib deflate stream, uncompress() to ArrayLength elements
 *   FBX 6.x often stores "arrays" as NumProperties repeated scalar properties
 *   (e.g. Vertices as N single 'D' doubles); the node accessors below fall
 *   back to concatenating scalar properties when no true array is present.
 *
 * File footer (written by the SDK for version >= 7400; readers may ignore it,
 * parsing simply stops at the top-level NULL record):
 *   16-byte semirandom file id, zero padding up to a 16-byte aligned file
 *   offset, 4 zero bytes, uint32 version echo, 120 zero bytes, 16-byte
 *   trailer signature.
 *
 * Document structure (subset consumed here):
 *   FBXHeaderExtension { FBXVersion }
 *   GlobalSettings { Properties70 { P: "UpAxis" / "UnitScaleFactor" / ... } }
 *   Definitions { ObjectType ... }                     (informational)
 *   Objects { Geometry, Model, Material, Texture, Video, Deformer,
 *             NodeAttribute, AnimStack, ... }
 *   Connections { C: "OO"|"OP", srcId, dstId [, "SlotName"] }
 *
 * Geometry:
 *   Vertices           : flat double array, 3 components per control point.
 *   PolygonVertexIndex : one entry per polygon corner; the LAST corner of
 *                        every polygon is stored bitwise-complemented
 *                        (value < 0, real index = ~value).  Polygons may have
 *                        any number of sides >= 3 -> fan triangulation.
 *   LayerElementNormal / UV / Tangent / Color / Material children carry:
 *     MappingInformationType   : AllSame | ByPolygon | ByVertex | ByPolygonVertex
 *     ReferenceInformationType : Direct | IndexToDirect
 *   "ByPolygonVertex" indexes attributes with the global polygon-vertex
 *   counter (= the position inside PolygonVertexIndex).
 *
 * Model evaluation (simplified - pivot offsets are not supported):
 *   Local = T(Lcl Translation) * R(PreRotation) * R(Lcl Rotation) * S(Lcl Scaling)
 *   Lcl Rotation is Euler angles in DEGREES.  Default RotationOrder
 *   eEulerXYZ composes to R = Rz * Ry * Rx in column-vector convention
 *   (composition tables verified against assimp's FBXConverter).
 *
 * Coordinate system / units:
 *   Engine convention: right handed, +Y up, -Z forward, meters.
 *   FBX scenes are typically +Y up (Maya/Blender/MotionBuilder) or +Z up
 *   (3ds Max).  One change-of-basis matrix
 *       A = axisFixRotation * scale(UnitScaleFactor)
 *   is baked into the mesh vertices (positions rotated+scaled, normals and
 *   tangents rotated), and every node local transform is conjugated
 *       L' = A * L * A^-1
 *   which keeps the full hierarchy mathematically consistent:
 *       world' * v' = (A*world*A^-1) * (A*v) = A * (world*v).
 *   Note on UnitScaleFactor: the FBX SDK defines it as centimeters per scene
 *   unit (meters = units * USF / 100).  This engine's convention treats FBX
 *   units as meters already and uses USF as a plain multiplier (USF == 1 ->
 *   identity), which is what game-oriented exporters (Blender "apply unit",
 *   Unity) effectively produce.  The applied scale is logged at INFO level.
 * ---------------------------------------------------------------------------
 */
#include "resource/resource_types.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Genesis
{
    namespace ResourceIO
    {
        namespace
        {
            //==================================================================
            // Limits - every loop in the parser is bounded by one of these so
            // that a corrupt/hostile file can never hang or exhaust memory.
            //==================================================================
            static constexpr int kFbxMaxDepth = 64;                       // node nesting depth
            static constexpr uint32_t kFbxMaxArrayElems = 16u << 20;      // elements per array property
            static constexpr size_t kFbxMaxFileBytes = 512u << 20;        // 512 MB input cap
            static constexpr size_t kFbxMaxVertices = 8u << 20;           // control points per geometry
            static constexpr size_t kFbxMaxCorners = 64u << 20;           // polygon-vertex entries
            static constexpr size_t kFbxAsciiMaxTokens = 8u << 20;
            static constexpr size_t kFbxAsciiMaxSkips = 1u << 17;         // junk-token recovery budget
            static constexpr size_t kFbxMaxDecompressRatio = 256;         // zlib bomb guard
            static constexpr size_t kFbxMaxDecompressSlack = 1u << 20;

            // "Kaydara FBX Binary  \0" + 0x1A + 0x00  (23 bytes; version u32 follows)
            static const uint8_t kFbxBinaryMagic[23] = {
                'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X', ' ',
                'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', 0x00, 0x1A, 0x00
            };

            //==================================================================
            // Small self-contained file/path helpers.  ResourceIO::readFile &
            // friends live in resource_io.cpp; this translation unit keeps its
            // own statics so it links standalone (no dependency on files that
            // may still be in flight on the mainline).
            //==================================================================
            static bool fbxReadWholeFile(const std::string &path, std::vector<uint8_t> &out)
            {
                std::ifstream f(path, std::ios::binary | std::ios::ate);
                if (!f.is_open())
                {
                    GEN_LOG_ERROR("[FBX] cannot open '%s'", path.c_str());
                    return false;
                }
                std::streamsize size = f.tellg();
                if (size <= 0)
                {
                    GEN_LOG_ERROR("[FBX] '%s' is empty", path.c_str());
                    return false;
                }
                if ((uint64_t)size > kFbxMaxFileBytes)
                {
                    GEN_LOG_ERROR("[FBX] '%s' too large (%lld bytes, cap %llu)", path.c_str(),
                                  (long long)size, (unsigned long long)kFbxMaxFileBytes);
                    return false;
                }
                f.seekg(0, std::ios::beg);
                out.resize((size_t)size);
                if (!f.read(reinterpret_cast<char *>(out.data()), size))
                {
                    GEN_LOG_ERROR("[FBX] short read on '%s'", path.c_str());
                    return false;
                }
                return true;
            }

            static bool fbxFileExists(const std::string &path)
            {
                if (path.empty()) return false;
                std::ifstream f(path, std::ios::binary);
                return f.good();
            }

            static std::string fbxDirOf(const std::string &path)
            {
                size_t slash = path.find_last_of("/\\");
                return slash == std::string::npos ? std::string() : path.substr(0, slash);
            }

            static std::string fbxJoinPath(const std::string &dir, const std::string &rel)
            {
                if (dir.empty()) return rel;
                if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) return dir + rel;
                return dir + "/" + rel;
            }

            static std::string fbxBaseNameNoExt(const std::string &path)
            {
                size_t slash = path.find_last_of("/\\");
                std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
                return name;
            }

            static std::string fbxToLower(std::string s)
            {
                for (char &c : s) c = (char)std::tolower((unsigned char)c);
                return s;
            }

            // Object names embed a "\x00\x01ClassName" suffix in binary files.
            static std::string fbxStripNullSuffix(const std::string &raw)
            {
                size_t nul = raw.find('\0');
                return nul == std::string::npos ? raw : raw.substr(0, nul);
            }

            // "Geometry::GenesisCube" -> "GenesisCube" (after null-stripping).
            static std::string fbxObjectName(const std::string &raw)
            {
                std::string s = fbxStripNullSuffix(raw);
                size_t sep = s.find("::");
                if (sep != std::string::npos && sep + 2 < s.size()) return s.substr(sep + 2);
                return s;
            }

            //==================================================================
            // Generic FBX node tree (shared by the binary and ASCII parsers)
            //==================================================================
            struct FbxProp
            {
                char type = '?';        // FBX type code: Y C I L F D S R f d l i b
                bool is_array = false;
                int64_t i = 0;          // scalar integer / bool
                double d = 0.0;         // scalar float
                std::string s;          // S (string) or R (raw bytes)
                std::vector<int64_t> ints;     // l / i / b arrays
                std::vector<double> doubles;   // d / f arrays
                uint32_t count = 0;            // declared array element count
            };

            struct FbxNode
            {
                std::string name;
                std::vector<FbxProp> props;
                std::vector<std::shared_ptr<FbxNode>> children;

                const FbxNode *child(const char *childName) const
                {
                    for (const auto &c : children)
                        if (c && c->name == childName) return c.get();
                    return nullptr;
                }

                std::vector<const FbxNode *> childrenNamed(const char *childName) const
                {
                    std::vector<const FbxNode *> out;
                    for (const auto &c : children)
                        if (c && c->name == childName) out.push_back(c.get());
                    return out;
                }
            };

            //--------------------------------------------------------------
            // Property accessors - these deliberately accept BOTH the binary
            // representations and the looser ASCII/6.x shapes so the document
            // layer below never has to care which parser produced the tree.
            //--------------------------------------------------------------
            static bool propIsString(const FbxProp &p) { return p.type == 'S'; }

            static bool propIsNumeric(const FbxProp &p)
            {
                return p.type == 'Y' || p.type == 'C' || p.type == 'I' || p.type == 'L' ||
                       p.type == 'F' || p.type == 'D' ||
                       (p.is_array && (p.type == 'f' || p.type == 'd' || p.type == 'l' ||
                                       p.type == 'i' || p.type == 'b'));
            }

            static double propDouble(const FbxProp &p)
            {
                switch (p.type)
                {
                case 'D': case 'F': return p.d;
                case 'Y': case 'C': case 'I': case 'L': return (double)p.i;
                case 'd': case 'f': return p.doubles.empty() ? 0.0 : p.doubles[0];
                case 'l': case 'i': case 'b': return p.ints.empty() ? 0.0 : (double)p.ints[0];
                default: return 0.0;
                }
            }

            static int64_t propInt(const FbxProp &p)
            {
                switch (p.type)
                {
                case 'Y': case 'C': case 'I': case 'L': return p.i;
                case 'D': case 'F': return (int64_t)p.d;
                case 'l': case 'i': case 'b': return p.ints.empty() ? 0 : p.ints[0];
                case 'd': case 'f': return p.doubles.empty() ? 0 : (int64_t)p.doubles[0];
                default: return 0;
                }
            }

            static void propDoubleArray(const FbxProp &p, std::vector<double> &out)
            {
                if (p.is_array)
                {
                    if (p.type == 'd' || p.type == 'f')
                        out.insert(out.end(), p.doubles.begin(), p.doubles.end());
                    else
                        for (int64_t v : p.ints) out.push_back((double)v);
                }
                else if (p.type == 'D' || p.type == 'F' || p.type == 'Y' || p.type == 'C' ||
                         p.type == 'I' || p.type == 'L')
                {
                    out.push_back(propDouble(p));
                }
            }

            static void propIntArray(const FbxProp &p, std::vector<int64_t> &out)
            {
                if (p.is_array)
                {
                    if (p.type == 'l' || p.type == 'i' || p.type == 'b')
                        out.insert(out.end(), p.ints.begin(), p.ints.end());
                    else
                        for (double v : p.doubles) out.push_back((int64_t)v);
                }
                else if (p.type == 'Y' || p.type == 'C' || p.type == 'I' || p.type == 'L' ||
                         p.type == 'D' || p.type == 'F')
                {
                    out.push_back(propInt(p));
                }
            }

            // Concatenates every numeric property of a node.  Handles the 7.x
            // single-array case as well as the FBX 6.x "array stored as many
            // scalar properties" case.
            static bool nodeDoubleArray(const FbxNode &n, std::vector<double> &out)
            {
                out.clear();
                for (const FbxProp &p : n.props) propDoubleArray(p, out);
                return !out.empty();
            }

            static bool nodeIntArray(const FbxNode &n, std::vector<int64_t> &out)
            {
                out.clear();
                for (const FbxProp &p : n.props) propIntArray(p, out);
                return !out.empty();
            }

            static std::string nodeString(const FbxNode &n)
            {
                for (const FbxProp &p : n.props)
                    if (propIsString(p)) return p.s;
                return std::string();
            }

            //==================================================================
            // Binary parser
            //==================================================================
            class FbxBinaryParser
            {
            public:
                bool parse(const std::vector<uint8_t> &data, uint32_t &version,
                           std::vector<std::shared_ptr<FbxNode>> &outRoots)
                {
                    d_ = data.data();
                    size_ = data.size();
                    pos_ = 0;
                    if (size_ < 27 || std::memcmp(d_, kFbxBinaryMagic, sizeof(kFbxBinaryMagic)) != 0)
                    {
                        GEN_LOG_ERROR("[FBX] not a Kaydara FBX binary file (bad magic)");
                        return false;
                    }
                    std::memcpy(&version, d_ + 23, 4);
                    version_ = version;
                    if (version < 6000 || version > 9999)
                    {
                        GEN_LOG_ERROR("[FBX] unsupported binary version %u (expected 6000..9999)", version);
                        return false;
                    }
                    headerSize_ = (version >= 7500) ? 25 : 13;
                    pos_ = 27;
                    bool ok = parseNodeList(0, size_, outRoots);
                    if (outRoots.empty())
                    {
                        GEN_LOG_ERROR("[FBX] binary parse produced no nodes");
                        return false;
                    }
                    if (!ok)
                        GEN_LOG_WARN("[FBX] binary parse hit corrupt data; recovered %zu top-level node(s)",
                                     outRoots.size());
                    GEN_LOG_INFO("[FBX] binary format, version %u, %zu top-level node(s)",
                                 version, outRoots.size());
                    return true;
                }

            private:
                const uint8_t *d_ = nullptr;
                size_t size_ = 0;
                size_t pos_ = 0;
                size_t headerSize_ = 13;
                uint32_t version_ = 0;
                bool err_ = false;

                bool need(size_t n) const { return n <= size_ && pos_ <= size_ - n; }

                uint8_t u8()
                {
                    if (!need(1)) { err_ = true; return 0; }
                    return d_[pos_++];
                }
                uint16_t u16()
                {
                    if (!need(2)) { err_ = true; return 0; }
                    uint16_t v; std::memcpy(&v, d_ + pos_, 2); pos_ += 2; return v;
                }
                uint32_t u32()
                {
                    if (!need(4)) { err_ = true; return 0; }
                    uint32_t v; std::memcpy(&v, d_ + pos_, 4); pos_ += 4; return v;
                }
                uint64_t u64()
                {
                    if (!need(8)) { err_ = true; return 0; }
                    uint64_t v; std::memcpy(&v, d_ + pos_, 8); pos_ += 8; return v;
                }
                float f32()
                {
                    if (!need(4)) { err_ = true; return 0.0f; }
                    float v; std::memcpy(&v, d_ + pos_, 4); pos_ += 4; return v;
                }
                double f64()
                {
                    if (!need(8)) { err_ = true; return 0.0; }
                    double v; std::memcpy(&v, d_ + pos_, 8); pos_ += 8; return v;
                }

                // Reads one property; `listEnd` bounds the declared property
                // list so a corrupt length cannot run into sibling nodes.
                bool parseProp(size_t listEnd, FbxProp &out)
                {
                    if (!need(1)) { err_ = true; return false; }
                    char t = (char)d_[pos_++];
                    out = FbxProp();
                    out.type = t;
                    err_ = false;
                    switch (t)
                    {
                    case 'Y': out.i = (int16_t)u16(); break;
                    case 'C': out.i = u8(); break;
                    case 'I': out.i = (int32_t)u32(); break;
                    case 'L': out.i = (int64_t)u64(); break;
                    case 'F': out.d = f32(); break;
                    case 'D': out.d = f64(); break;
                    case 'S':
                    case 'R':
                    {
                        uint32_t len = u32();
                        if (err_ || !need(len)) { err_ = true; return false; }
                        out.s.assign((const char *)d_ + pos_, len);
                        pos_ += len;
                        break;
                    }
                    case 'f': case 'd': case 'l': case 'i': case 'b':
                        return parseArrayProp(t, listEnd, out);
                    default:
                        GEN_LOG_WARN("[FBX] unknown property type '%c' (0x%02X) at offset %zu",
                                     std::isprint((unsigned char)t) ? t : '?', (unsigned)(uint8_t)t, pos_ - 1);
                        err_ = true;
                        return false;
                    }
                    if (err_ || pos_ > listEnd) { err_ = true; return false; }
                    return true;
                }

                // f/d/l/i/b array property:
                //   u32 ArrayLength | u32 Encoding | u32 CompressedLength | data
                // Encoding 0: raw elements.  Encoding 1: zlib compressed.
                bool parseArrayProp(char t, size_t listEnd, FbxProp &out)
                {
                    uint32_t count = u32();
                    uint32_t encoding = u32();
                    uint32_t compLen = u32();
                    if (err_) return false;

                    size_t elemSize = (t == 'd' || t == 'l') ? 8 : (t == 'f' || t == 'i') ? 4 : 1;
                    if (count > kFbxMaxArrayElems)
                    {
                        GEN_LOG_WARN("[FBX] array with %u elements exceeds cap %u", count, kFbxMaxArrayElems);
                        err_ = true;
                        return false;
                    }
                    uint64_t rawSize64 = (uint64_t)count * elemSize;
                    if (rawSize64 > kFbxMaxFileBytes) { err_ = true; return false; }
                    size_t rawSize = (size_t)rawSize64;

                    out.is_array = true;
                    out.count = count;

                    const uint8_t *src = nullptr;
                    std::vector<uint8_t> scratch;
                    if (encoding == 0)
                    {
                        if (rawSize > size_ - pos_)
                        {
                            GEN_LOG_WARN("[FBX] raw array of %zu bytes truncated (have %zu)",
                                         rawSize, size_ - pos_);
                            err_ = true;
                            return false;
                        }
                        // CompressedLength should equal the raw size for
                        // encoding 0; some writers leave it 0 - trust the
                        // element math instead.
                        src = d_ + pos_;
                        pos_ += rawSize;
                    }
                    else if (encoding == 1)
                    {
                        if (compLen > size_ - pos_)
                        {
                            GEN_LOG_WARN("[FBX] compressed array truncated (%u bytes wanted, %zu left)",
                                         compLen, size_ - pos_);
                            err_ = true;
                            return false;
                        }
                        // Decompression-bomb guard: FBX arrays realistically
                        // compress well below 100:1.
                        if (rawSize > (size_t)compLen * kFbxMaxDecompressRatio + kFbxMaxDecompressSlack)
                        {
                            GEN_LOG_WARN("[FBX] suspicious compression ratio (%zu from %u bytes) - rejected",
                                         rawSize, compLen);
                            err_ = true;
                            return false;
                        }
                        if (rawSize > 0)
                        {
                            scratch.resize(rawSize);
                            uLongf destLen = (uLongf)rawSize;
                            int rc = uncompress(scratch.data(), &destLen, d_ + pos_, compLen);
                            if (rc != Z_OK || (size_t)destLen != rawSize)
                            {
                                GEN_LOG_WARN("[FBX] zlib uncompress failed (rc=%d, got %lu of %zu bytes)",
                                             rc, (unsigned long)destLen, rawSize);
                                pos_ += compLen;
                                err_ = true;
                                return false;
                            }
                        }
                        pos_ += compLen;
                        src = scratch.data();
                    }
                    else
                    {
                        GEN_LOG_WARN("[FBX] unknown array encoding %u", encoding);
                        err_ = true;
                        return false;
                    }

                    if (t == 'd' || t == 'f')
                    {
                        out.doubles.resize(count);
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            if (t == 'd')
                            {
                                double v; std::memcpy(&v, src + (size_t)k * 8, 8);
                                out.doubles[k] = v;
                            }
                            else
                            {
                                float v; std::memcpy(&v, src + (size_t)k * 4, 4);
                                out.doubles[k] = v;
                            }
                        }
                    }
                    else
                    {
                        out.ints.resize(count);
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            if (t == 'l')
                            {
                                int64_t v; std::memcpy(&v, src + (size_t)k * 8, 8);
                                out.ints[k] = v;
                            }
                            else if (t == 'i')
                            {
                                int32_t v; std::memcpy(&v, src + (size_t)k * 4, 4);
                                out.ints[k] = v;
                            }
                            else
                            {
                                out.ints[k] = src[k];
                            }
                        }
                    }
                    if (pos_ > listEnd) { err_ = true; return false; }
                    return true;
                }

                // Parses sibling records until the NULL record (or `regionEnd`
                // for tolerant recovery).  Returns false on corruption; the
                // caller can still trust `out` for everything parsed so far
                // because each node is only appended when its own header was
                // consistent, and recovery always jumps to EndOffset.
                bool parseNodeList(int depth, size_t regionEnd, std::vector<std::shared_ptr<FbxNode>> &out)
                {
                    if (depth > kFbxMaxDepth)
                    {
                        GEN_LOG_WARN("[FBX] node nesting deeper than %d - aborting branch", kFbxMaxDepth);
                        return false;
                    }
                    while (true)
                    {
                        if (pos_ >= regionEnd) return true;   // no NULL record - tolerant end
                        if (pos_ + headerSize_ > size_) return true; // truncated tail

                        size_t nodeStart = pos_;
                        uint64_t endOffset, numProps, propListLen;
                        if (headerSize_ == 25)
                        {
                            endOffset = u64();
                            numProps = u64();
                            propListLen = u64();
                        }
                        else
                        {
                            endOffset = u32();
                            numProps = u32();
                            propListLen = u32();
                        }
                        uint8_t nameLen = u8();
                        if (err_) return false;

                        // NULL record: terminates this sibling list.
                        if (endOffset == 0 && nameLen == 0)
                        {
                            pos_ = nodeStart + headerSize_;
                            return true;
                        }

                        // Header sanity.  Every check guarantees that the
                        // recovery jump below (pos_ = endOffset) makes strict
                        // forward progress, so no loop can spin.
                        if (endOffset <= nodeStart || endOffset > size_ || endOffset > regionEnd ||
                            numProps > propListLen ||
                            nodeStart + headerSize_ + nameLen > (size_t)endOffset ||
                            propListLen > (uint64_t)endOffset - (nodeStart + headerSize_ + nameLen))
                        {
                            GEN_LOG_WARN("[FBX] corrupt node header at offset %zu (end=%llu, props=%llu, len=%llu)",
                                         nodeStart, (unsigned long long)endOffset,
                                         (unsigned long long)numProps, (unsigned long long)propListLen);
                            return false;
                        }

                        auto node = std::make_shared<FbxNode>();
                        node->name.assign((const char *)d_ + pos_, nameLen);
                        pos_ += nameLen;

                        size_t propStart = pos_;
                        size_t propEnd = propStart + (size_t)propListLen;
                        bool propsOk = true;
                        node->props.reserve((size_t)std::min<uint64_t>(numProps, 64));
                        for (uint64_t p = 0; p < numProps && propsOk; ++p)
                        {
                            if (pos_ >= propEnd) { propsOk = false; break; }
                            FbxProp prop;
                            propsOk = parseProp(propEnd, prop);
                            if (propsOk) node->props.push_back(std::move(prop));
                        }
                        if (!propsOk)
                            GEN_LOG_WARN("[FBX] node '%s': corrupt property list (kept %zu props)",
                                         node->name.c_str(), node->props.size());

                        pos_ = std::min(propEnd, (size_t)endOffset);
                        if ((size_t)endOffset > pos_)
                        {
                            // Nested children (terminated by their own NULL record).
                            bool nestedOk = parseNodeList(depth + 1, (size_t)endOffset, node->children);
                            if (!nestedOk)
                                GEN_LOG_WARN("[FBX] node '%s': corrupt nested content skipped",
                                             node->name.c_str());
                        }
                        pos_ = (size_t)endOffset; // forward progress guaranteed
                        out.push_back(std::move(node));
                    }
                }
            };

            //==================================================================
            // ASCII parser (basic support for "; FBX x.y.z project file" form)
            //
            // Grammar (whitespace/newlines insignificant, ';' comments):
            //   NodeList := (Node)*  [ '}' handled by caller ]
            //   Node     := IDENT ':' ValueList [ '{' NodeList '}' ]
            //   ValueList:= Value (',' Value)*
            //   Value    := STRING | NUMBER | BOOLIDENT | '*' INT '{' ['a' ':']
            //               NUMBER (',' NUMBER)* '}'          (array property)
            // Values that are not strings/numbers (e.g. the unquoted
            // "CreationTime: 2020-01-01 10:00:00:000" line emitted by the SDK)
            // are recovered from by skipping junk tokens until the next
            // IDENT ':' pair - bounded by kFbxAsciiMaxSkips.
            //==================================================================
            class FbxAsciiParser
            {
            public:
                bool parse(const std::string &text, std::vector<std::shared_ptr<FbxNode>> &outRoots)
                {
                    src_ = &text;
                    toks_.clear();
                    if (!tokenize()) return false;
                    cur_ = 0;
                    skips_ = 0;
                    parseList(0, outRoots, true);
                    if (outRoots.empty())
                    {
                        GEN_LOG_ERROR("[FBX] ASCII parse produced no nodes");
                        return false;
                    }
                    GEN_LOG_INFO("[FBX] ASCII format, %zu top-level node(s)", outRoots.size());
                    return true;
                }

            private:
                struct Tok
                {
                    enum K
                    {
                        End, Ident, Num, Str, Colon, Comma, LBrace, RBrace, Star, Junk
                    } k = End;
                    std::string text;
                    int64_t i = 0;
                    double d = 0.0;
                    bool isInt = false;
                };

                const std::string *src_ = nullptr;
                std::vector<Tok> toks_;
                size_t cur_ = 0;
                size_t skips_ = 0;

                const Tok &peek(size_t ahead = 0) const
                {
                    size_t idx = cur_ + ahead;
                    if (idx >= toks_.size()) idx = toks_.size() - 1; // End sentinel
                    return toks_[idx];
                }
                void advance()
                {
                    if (cur_ + 1 < toks_.size()) ++cur_;
                    else cur_ = toks_.size(); // stay on End sentinel
                }

                bool tokenize()
                {
                    const std::string &s = *src_;
                    const size_t n = s.size();
                    size_t i = 0;
                    while (i < n)
                    {
                        char c = s[i];
                        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0') { ++i; continue; }
                        if (c == ';') { while (i < n && s[i] != '\n') ++i; continue; } // comment
                        if (toks_.size() >= kFbxAsciiMaxTokens)
                        {
                            GEN_LOG_ERROR("[FBX] ASCII token limit exceeded");
                            return false;
                        }
                        Tok t;
                        if (c == '"')
                        {
                            t.k = Tok::Str;
                            ++i;
                            while (i < n && s[i] != '"')
                            {
                                if (s[i] == '\\' && i + 1 < n) { ++i; }
                                t.text += s[i++];
                            }
                            if (i < n) ++i; // closing quote (missing tolerated)
                        }
                        else if (std::isalpha((unsigned char)c) || c == '_')
                        {
                            t.k = Tok::Ident;
                            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) t.text += s[i++];
                        }
                        else if (std::isdigit((unsigned char)c) || c == '.' ||
                                 ((c == '-' || c == '+') && i + 1 < n &&
                                  (std::isdigit((unsigned char)s[i + 1]) || s[i + 1] == '.')))
                        {
                            t.k = Tok::Num;
                            std::string num;
                            bool isFloat = false;
                            // Consume a leading sign first - the inner loop
                            // only accepts '-'/'+' after an exponent marker.
                            if (c == '-' || c == '+') { num += c; ++i; }
                            while (i < n)
                            {
                                char ch = s[i];
                                if (std::isdigit((unsigned char)ch)) { num += ch; ++i; }
                                else if (ch == '.') { isFloat = true; num += ch; ++i; }
                                else if ((ch == 'e' || ch == 'E') && i + 1 < n &&
                                         (std::isdigit((unsigned char)s[i + 1]) ||
                                          ((s[i + 1] == '-' || s[i + 1] == '+') && i + 2 < n &&
                                           std::isdigit((unsigned char)s[i + 2]))))
                                {
                                    isFloat = true; num += ch; ++i;
                                }
                                else if ((ch == '-' || ch == '+') && !num.empty() &&
                                         (num.back() == 'e' || num.back() == 'E'))
                                {
                                    num += ch; ++i;
                                }
                                else break;
                            }
                            t.text = num;
                            if (num.empty() || num == "+" || num == "-")
                            {
                                // Cannot happen for well-formed input; make
                                // absolute sure the scanner advances.
                                t.k = Tok::Junk;
                                if (i < n && num.empty()) ++i;
                            }
                            else
                            {
                                t.isInt = !isFloat;
                                if (isFloat)
                                {
                                    t.d = std::strtod(num.c_str(), nullptr);
                                }
                                else
                                {
                                    errno = 0;
                                    t.i = std::strtoll(num.c_str(), nullptr, 10);
                                    if (errno == ERANGE)
                                    {
                                        t.isInt = false;
                                        t.d = std::strtod(num.c_str(), nullptr);
                                    }
                                }
                            }
                        }
                        else if (c == ':') { t.k = Tok::Colon; ++i; }
                        else if (c == ',') { t.k = Tok::Comma; ++i; }
                        else if (c == '{') { t.k = Tok::LBrace; ++i; }
                        else if (c == '}') { t.k = Tok::RBrace; ++i; }
                        else if (c == '*') { t.k = Tok::Star; ++i; }
                        else { t.k = Tok::Junk; ++i; }
                        toks_.push_back(std::move(t));
                    }
                    Tok end;
                    end.k = Tok::End;
                    toks_.push_back(end);
                    return true;
                }

                bool isValueStart() const
                {
                    Tok::K k = peek().k;
                    if (k == Tok::Str || k == Tok::Num || k == Tok::Star) return true;
                    // An identifier is a value (e.g. "Shading: T") only when it
                    // is NOT followed by ':' (which would start a child node).
                    if (k == Tok::Ident && peek(1).k != Tok::Colon && peek(1).k != Tok::LBrace) return true;
                    return false;
                }

                bool parseArrayProp(const std::shared_ptr<FbxNode> &node)
                {
                    advance(); // '*'
                    int64_t declared = -1;
                    if (peek().k == Tok::Num && peek().isInt)
                    {
                        declared = peek().i;
                        advance();
                    }
                    if (peek().k != Tok::LBrace)
                    {
                        GEN_LOG_WARN("[FBX-ASCII] malformed array for node '%s'", node->name.c_str());
                        return false;
                    }
                    advance(); // '{'
                    // 7.x arrays wrap their payload in "a:"
                    if (peek().k == Tok::Ident && peek().text == "a" && peek(1).k == Tok::Colon)
                    {
                        advance();
                        advance();
                    }
                    std::vector<int64_t> ints;
                    std::vector<double> dbls;
                    bool allInt = true;
                    while (true)
                    {
                        const Tok &t = peek();
                        if (t.k == Tok::RBrace || t.k == Tok::End) break;
                        if (t.k == Tok::Comma) { advance(); continue; }
                        if (t.k == Tok::Num)
                        {
                            if (t.isInt)
                            {
                                ints.push_back(t.i);
                                dbls.push_back((double)t.i);
                            }
                            else
                            {
                                allInt = false;
                                dbls.push_back(t.d);
                            }
                            advance();
                            continue;
                        }
                        advance(); // junk inside array - bounded by outer skip budget
                        if (++skips_ > kFbxAsciiMaxSkips) return false;
                    }
                    if (peek().k == Tok::RBrace) advance();

                    FbxProp p;
                    p.is_array = true;
                    size_t got = allInt ? ints.size() : dbls.size();
                    if (declared >= 0 && (uint64_t)declared != (uint64_t)got)
                        GEN_LOG_WARN("[FBX-ASCII] node '%s': array declared *%lld but holds %zu values",
                                     node->name.c_str(), (long long)declared, got);
                    if (allInt)
                    {
                        p.type = 'l';
                        p.count = (uint32_t)std::min<size_t>(ints.size(), 0xFFFFFFFFu);
                        p.ints = std::move(ints);
                    }
                    else
                    {
                        p.type = 'd';
                        p.count = (uint32_t)std::min<size_t>(dbls.size(), 0xFFFFFFFFu);
                        p.doubles = std::move(dbls);
                    }
                    node->props.push_back(std::move(p));
                    return true;
                }

                std::shared_ptr<FbxNode> parseNode(int depth, bool &ok)
                {
                    ok = true;
                    auto node = std::make_shared<FbxNode>();
                    node->name = peek().text;
                    advance(); // IDENT
                    advance(); // ':'

                    while (isValueStart())
                    {
                        const Tok &t = peek();
                        if (t.k == Tok::Str)
                        {
                            FbxProp p; p.type = 'S'; p.s = t.text;
                            node->props.push_back(std::move(p));
                            advance();
                        }
                        else if (t.k == Tok::Num)
                        {
                            FbxProp p;
                            if (t.isInt) { p.type = 'L'; p.i = t.i; }
                            else { p.type = 'D'; p.d = t.d; }
                            node->props.push_back(std::move(p));
                            advance();
                        }
                        else if (t.k == Tok::Star)
                        {
                            if (!parseArrayProp(node)) { ok = false; return node; }
                        }
                        else // Ident value: single letters are bool-ish (T/Y/U true)
                        {
                            FbxProp p;
                            if (t.text.size() == 1)
                            {
                                char c = t.text[0];
                                p.type = 'C';
                                p.i = (c == 'T' || c == 'Y' || c == 'U') ? 1 : 0;
                            }
                            else
                            {
                                p.type = 'S';
                                p.s = t.text;
                            }
                            node->props.push_back(std::move(p));
                            advance();
                        }
                        if (peek().k == Tok::Comma) { advance(); continue; }
                        break;
                    }

                    if (peek().k == Tok::LBrace)
                    {
                        if (depth + 1 > kFbxMaxDepth)
                        {
                            GEN_LOG_WARN("[FBX-ASCII] nesting deeper than %d at node '%s'",
                                         kFbxMaxDepth, node->name.c_str());
                            ok = false;
                            return node;
                        }
                        advance(); // '{'
                        bool childOk = parseList(depth + 1, node->children, false);
                        if (peek().k == Tok::RBrace) advance();
                        else GEN_LOG_WARN("[FBX-ASCII] node '%s': missing closing brace", node->name.c_str());
                        if (!childOk) ok = false;
                    }
                    return node;
                }

                bool parseList(int depth, std::vector<std::shared_ptr<FbxNode>> &out, bool topLevel)
                {
                    (void)topLevel;
                    while (true)
                    {
                        Tok::K k = peek().k;
                        if (k == Tok::End) return true;   // tolerant EOF
                        if (k == Tok::RBrace) return true; // caller consumes the brace
                        if (k == Tok::Ident && peek(1).k == Tok::Colon)
                        {
                            bool ok = false;
                            std::shared_ptr<FbxNode> node = parseNode(depth, ok);
                            if (node) out.push_back(std::move(node));
                            if (!ok) return false;
                            continue;
                        }
                        advance(); // junk recovery (stray tokens between statements)
                        if (++skips_ > kFbxAsciiMaxSkips)
                        {
                            GEN_LOG_WARN("[FBX-ASCII] too much unparsable content - giving up");
                            return false;
                        }
                    }
                }
            };

            //==================================================================
            // Document-level helpers
            //==================================================================

            // Properties70 (7.x) / Properties60 (6.x) "P:" record lookup.
            // P layout: name, type, typeclass, flags, then value(s).  Values
            // are either numeric (Vector3D triples, scalars) or a string
            // (FileName, ShadingModel...).
            struct P70Value
            {
                bool found = false;
                std::vector<double> numbers;
                std::string str;

                Math::Vec3 vec3(const Math::Vec3 &def) const
                {
                    if (numbers.size() >= 3)
                        return Math::Vec3((float)numbers[0], (float)numbers[1], (float)numbers[2]);
                    return def;
                }
                double first(double def) const { return numbers.empty() ? def : numbers[0]; }
            };

            static void collectP70(const FbxNode &pNode, P70Value &out)
            {
                out.found = true;
                // First four properties are the name/type/class/flag strings;
                // everything after them is the value payload.
                size_t start = std::min<size_t>(4, pNode.props.size());
                for (size_t k = start; k < pNode.props.size(); ++k)
                {
                    const FbxProp &p = pNode.props[k];
                    if (propIsString(p))
                    {
                        if (out.str.empty()) out.str = p.s;
                    }
                    else if (propIsNumeric(p))
                    {
                        propDoubleArray(p, out.numbers);
                    }
                }
            }

            static P70Value findProperty(const FbxNode *owner, const char *propName)
            {
                P70Value out;
                if (!owner) return out;
                static const char *containers[] = {"Properties70", "Properties60"};
                for (const char *container : containers)
                {
                    const FbxNode *c = owner->child(container);
                    if (!c) continue;
                    for (const FbxNode *p : c->childrenNamed("P"))
                    {
                        if (p->props.empty() || !propIsString(p->props[0])) continue;
                        if (p->props[0].s == propName)
                        {
                            collectP70(*p, out);
                            return out;
                        }
                    }
                    // 6.x occasionally nests the value node directly:
                    const FbxNode *direct = c->child(propName);
                    if (direct && !direct->props.empty())
                    {
                        collectP70(*direct, out);
                        return out;
                    }
                }
                // Fallback: some 6.x files put e.g. "Lcl Translation" straight
                // under the object node.
                const FbxNode *direct = owner->child(propName);
                if (direct && !direct->props.empty())
                {
                    collectP70(*direct, out);
                    return out;
                }
                return out;
            }

            //--------------------------------------------------------------
            // Global settings & coordinate-system conversion
            //--------------------------------------------------------------
            struct FbxAxisInfo
            {
                int upAxis = 1;          // 0=X 1=Y 2=Z
                int upAxisSign = 1;
                int frontAxis = 2;
                int frontAxisSign = 1;
                int coordAxis = 0;
                int coordAxisSign = 1;
                double unitScaleFactor = 1.0;
            };

            static const FbxNode *findRootNode(const std::vector<std::shared_ptr<FbxNode>> &roots,
                                               const char *name)
            {
                for (const auto &r : roots)
                    if (r && r->name == name) return r.get();
                return nullptr;
            }

            static FbxAxisInfo readGlobalSettings(const std::vector<std::shared_ptr<FbxNode>> &roots)
            {
                FbxAxisInfo info;
                const FbxNode *gs = findRootNode(roots, "GlobalSettings");
                if (!gs) return info;
                auto readInt = [&](const char *name, int def)
                {
                    P70Value v = findProperty(gs, name);
                    return (v.found && !v.numbers.empty()) ? (int)v.numbers[0] : def;
                };
                info.upAxis = readInt("UpAxis", 1);
                info.upAxisSign = readInt("UpAxisSign", 1);
                info.frontAxis = readInt("FrontAxis", 2);
                info.frontAxisSign = readInt("FrontAxisSign", 1);
                info.coordAxis = readInt("CoordAxis", 0);
                info.coordAxisSign = readInt("CoordAxisSign", 1);
                P70Value usf = findProperty(gs, "UnitScaleFactor");
                if (usf.found && !usf.numbers.empty()) info.unitScaleFactor = usf.numbers[0];
                return info;
            }

            static Math::Mat4 fbxRotX(float rad)
            {
                float c = std::cos(rad), s = std::sin(rad);
                return Math::Mat4(1, 0, 0, 0,
                                  0, c, -s, 0,
                                  0, s, c, 0,
                                  0, 0, 0, 1);
            }
            static Math::Mat4 fbxRotY(float rad)
            {
                float c = std::cos(rad), s = std::sin(rad);
                return Math::Mat4(c, 0, s, 0,
                                  0, 1, 0, 0,
                                  -s, 0, c, 0,
                                  0, 0, 0, 1);
            }
            static Math::Mat4 fbxRotZ(float rad)
            {
                float c = std::cos(rad), s = std::sin(rad);
                return Math::Mat4(c, -s, 0, 0,
                                  s, c, 0, 0,
                                  0, 0, 1, 0,
                                  0, 0, 0, 1);
            }

            // Euler degrees -> Mat4 following the FBX RotationOrder enum
            // (0=XYZ is the SDK default).  Column-vector convention: XYZ
            // composes to Rz*Ry*Rx (X applied first), matching assimp.
            static Math::Mat4 fbxEulerToMatrix(float xDeg, float yDeg, float zDeg, int order)
            {
                Math::Mat4 rx = fbxRotX(Math::toRadians(xDeg));
                Math::Mat4 ry = fbxRotY(Math::toRadians(yDeg));
                Math::Mat4 rz = fbxRotZ(Math::toRadians(zDeg));
                switch (order)
                {
                case 0: return rz * ry * rx; // eEulerXYZ (default)
                case 1: return ry * rz * rx; // eEulerXZY
                case 2: return rx * rz * ry; // eEulerYZX
                case 3: return rz * rx * ry; // eEulerYXZ
                case 4: return ry * rx * rz; // eEulerZXY
                case 5: return rx * ry * rz; // eEulerZYX
                default: return rz * ry * rx;
                }
            }

            // Builds the rotation that maps the FBX axis system onto the
            // engine convention (+Y up, right handed, -Z forward).  Only the
            // up-axis is converted; odd front/coord combinations are reported
            // but left alone (documented limitation).
            static Math::Mat4 buildAxisFix(const FbxAxisInfo &gs)
            {
                if (gs.upAxis == 1)
                {
                    if (gs.upAxisSign >= 0) return Math::Mat4::identity();
                    // Upside-down scene (-Y up): flip through 180 deg about X.
                    GEN_LOG_WARN("[FBX] UpAxisSign=-1: rotating scene 180 deg about X");
                    return fbxRotX(Math::PI);
                }
                if (gs.upAxis == 2)
                {
                    // Z-up (3ds Max / Blender Z-up exports):
                    //   sign +1: rotate -90 deg about X  ->  (x,y,z) -> (x,z,-y)
                    //   sign -1: rotate +90 deg about X
                    GEN_LOG_INFO("[FBX] Z-up scene detected: applying -/+90 deg X rotation");
                    return fbxRotX(gs.upAxisSign >= 0 ? -Math::HALF_PI : Math::HALF_PI);
                }
                if (gs.upAxis == 0)
                {
                    // X-up (rare): rotate +90 deg about Z -> (x,y,z) -> (-y,x,z)
                    GEN_LOG_WARN("[FBX] X-up scene detected: applying +/-90 deg Z rotation");
                    return fbxRotZ(gs.upAxisSign >= 0 ? Math::HALF_PI : -Math::HALF_PI);
                }
                GEN_LOG_WARN("[FBX] unknown UpAxis %d - keeping identity conversion", gs.upAxis);
                return Math::Mat4::identity();
            }

            static bool matIsIdentity(const Math::Mat4 &m)
            {
                static const Math::Mat4 ident = Math::Mat4::identity();
                return std::memcmp(m.m, ident.m, sizeof(Math::Mat4)) == 0;
            }

            //--------------------------------------------------------------
            // Object registry & connections
            //--------------------------------------------------------------
            struct FbxObject
            {
                int64_t id = 0;
                std::string fullName; // "Model::Cube" (null-suffix stripped)
                std::string name;     // "Cube"
                std::string subtype;  // e.g. "Mesh", "Skin", "Cluster"
                const FbxNode *node = nullptr;
            };

            struct FbxRegistry
            {
                std::vector<FbxObject> objs;
                std::unordered_map<int64_t, size_t> byId;
                std::unordered_map<std::string, size_t> byName;

                size_t add(const FbxObject &o)
                {
                    size_t idx = objs.size();
                    objs.push_back(o);
                    byId[o.id] = idx;
                    if (!o.fullName.empty()) byName[o.fullName] = idx;
                    return idx;
                }
                int64_t indexOfId(int64_t id) const
                {
                    auto it = byId.find(id);
                    return it == byId.end() ? -1 : (int64_t)it->second;
                }
                int64_t indexOfName(const std::string &name) const
                {
                    auto it = byName.find(name);
                    return it == byName.end() ? -1 : (int64_t)it->second;
                }
            };

            struct FbxLinkSide
            {
                bool byName = false;
                int64_t id = 0;
                std::string name; // full "Class::name" form
            };

            struct FbxLink
            {
                std::string type; // "OO" or "OP"
                FbxLinkSide src, dst;
                std::string slot; // OP property slot ("DiffuseColor", ...)
            };

            static void collectObjects(const std::vector<std::shared_ptr<FbxNode>> &roots,
                                       const char *category, FbxRegistry &reg, int64_t &syntheticId)
            {
                for (const auto &root : roots)
                {
                    if (!root || root->name != "Objects") continue;
                    for (const auto &child : root->children)
                    {
                        if (!child || child->name != category) continue;
                        FbxObject o;
                        o.node = child.get();
                        // First property is the object id (binary 7.x) or the
                        // full name (6.x ASCII-ish files).
                        if (!child->props.empty() && !propIsString(child->props[0]))
                            o.id = propInt(child->props[0]);
                        for (const FbxProp &p : child->props)
                        {
                            if (propIsString(p))
                            {
                                o.fullName = fbxStripNullSuffix(p.s);
                                break;
                            }
                        }
                        // Subtype is the string after the name in binary
                        // ("Mesh", "Skin", ...); the first string prop may be
                        // the name itself, so look at the second string.
                        int seenStrings = 0;
                        for (const FbxProp &p : child->props)
                        {
                            if (!propIsString(p)) continue;
                            ++seenStrings;
                            if (seenStrings == 2)
                            {
                                o.subtype = fbxStripNullSuffix(p.s);
                                break;
                            }
                        }
                        o.name = fbxObjectName(o.fullName);
                        if (o.id == 0 && o.fullName.empty()) continue; // unusable object
                        if (reg.byId.count(o.id) && o.id != 0)
                        {
                            // Duplicate id - give it a synthetic one so name
                            // resolution still works.
                            o.id = --syntheticId;
                        }
                        if (o.id == 0) o.id = --syntheticId;
                        reg.add(o);
                    }
                }
            }

            static void collectConnections(const std::vector<std::shared_ptr<FbxNode>> &roots,
                                           std::vector<FbxLink> &links)
            {
                for (const auto &root : roots)
                {
                    if (!root || root->name != "Connections") continue;
                    for (const auto &c : root->children)
                    {
                        if (!c || (c->name != "C" && c->name != "Connect")) continue;
                        if (c->props.size() < 3) continue;
                        FbxLink link;
                        link.type = propIsString(c->props[0]) ? c->props[0].s : std::string("OO");
                        auto side = [](const FbxProp &p, FbxLinkSide &out)
                        {
                            if (propIsString(p))
                            {
                                out.byName = true;
                                out.name = fbxStripNullSuffix(p.s);
                            }
                            else
                            {
                                out.id = propInt(p);
                            }
                        };
                        side(c->props[1], link.src);
                        side(c->props[2], link.dst);
                        if (c->props.size() > 3 && propIsString(c->props[3]))
                            link.slot = c->props[3].s;
                        links.push_back(std::move(link));
                    }
                }
            }

            static int64_t resolveSide(const FbxRegistry &reg, const FbxLinkSide &s)
            {
                return s.byName ? reg.indexOfName(s.name) : reg.indexOfId(s.id);
            }

            //--------------------------------------------------------------
            // Attribute layers (normals / uv / tangents / colors / materials)
            //--------------------------------------------------------------
            struct FbxLayer
            {
                enum Mapping
                {
                    None = 0, AllSame, ByPolygon, ByVertex, ByPolygonVertex
                } mapping = None;
                bool indexToDirect = false;
                std::vector<double> values;
                std::vector<int64_t> indices;
                int stride = 0;

                bool valid() const { return mapping != None && stride > 0 && !values.empty(); }
                int64_t elementCount() const { return stride > 0 ? (int64_t)(values.size() / (size_t)stride) : 0; }
            };

            static bool fbxReadLayer(const FbxNode &elem, const char *const *valueNames,
                                     const char *const *indexNames, int stride, FbxLayer &out)
            {
                std::string mapping = "bypolygonvertex"; // SDK default
                std::string ref = "direct";
                if (const FbxNode *c = elem.child("MappingInformationType"))
                {
                    std::string s = nodeString(*c);
                    if (!s.empty()) mapping = fbxToLower(s);
                }
                if (const FbxNode *c = elem.child("ReferenceInformationType"))
                {
                    std::string s = nodeString(*c);
                    if (!s.empty()) ref = fbxToLower(s);
                }
                if (mapping == "allsame") out.mapping = FbxLayer::AllSame;
                else if (mapping == "bypolygon") out.mapping = FbxLayer::ByPolygon;
                else if (mapping == "byvertex") out.mapping = FbxLayer::ByVertex;
                else if (mapping == "bypolygonvertex") out.mapping = FbxLayer::ByPolygonVertex;
                else
                {
                    GEN_LOG_WARN("[FBX] unsupported MappingInformationType '%s' on '%s'",
                                 mapping.c_str(), elem.name.c_str());
                    return false;
                }
                out.indexToDirect = (ref == "indextodirect" || ref == "index");
                if (!out.indexToDirect && ref != "direct")
                    GEN_LOG_WARN("[FBX] unknown ReferenceInformationType '%s' - assuming Direct", ref.c_str());

                for (const char *const *vn = valueNames; *vn; ++vn)
                {
                    const FbxNode *c = elem.child(*vn);
                    if (c && nodeDoubleArray(*c, out.values) && !out.values.empty()) break;
                }
                if (out.values.empty())
                {
                    GEN_LOG_WARN("[FBX] layer '%s' has no value array", elem.name.c_str());
                    return false;
                }
                if (out.values.size() % (size_t)stride != 0)
                {
                    GEN_LOG_WARN("[FBX] layer '%s': %zu values not a multiple of stride %d",
                                 elem.name.c_str(), out.values.size(), stride);
                    return false;
                }
                out.stride = stride;
                if (out.indexToDirect)
                {
                    for (const char *const *iname = indexNames; *iname; ++iname)
                    {
                        const FbxNode *c = elem.child(*iname);
                        if (c && nodeIntArray(*c, out.indices) && !out.indices.empty()) break;
                    }
                    if (out.indices.empty())
                    {
                        GEN_LOG_WARN("[FBX] layer '%s': IndexToDirect without index array", elem.name.c_str());
                        return false;
                    }
                }
                return true;
            }

            // Resolves one attribute element for a polygon corner.
            //   polyIdx - polygon ordinal
            //   vertIdx - control point index of the corner
            //   pvIdx   - global polygon-vertex counter (position in PRI)
            static bool fbxSampleLayer(const FbxLayer &L, int64_t polyIdx, int64_t vertIdx,
                                       int64_t pvIdx, double *out, int comps)
            {
                int64_t slot = -1;
                switch (L.mapping)
                {
                case FbxLayer::AllSame: slot = 0; break;
                case FbxLayer::ByPolygon: slot = polyIdx; break;
                case FbxLayer::ByVertex: slot = vertIdx; break;
                case FbxLayer::ByPolygonVertex: slot = pvIdx; break;
                default: return false;
                }
                if (slot < 0) return false;
                if (L.indexToDirect)
                {
                    if ((size_t)slot >= L.indices.size()) return false;
                    slot = L.indices[(size_t)slot];
                    if (slot < 0) return false;
                }
                size_t base = (size_t)slot * (size_t)L.stride;
                if (base + (size_t)L.stride > L.values.size()) return false;
                int n = std::min(comps, L.stride);
                for (int c = 0; c < n; ++c) out[c] = L.values[base + (size_t)c];
                for (int c = n; c < comps; ++c) out[c] = 0.0;
                return true;
            }

            //--------------------------------------------------------------
            // Vertex dedupe key: bit pattern of position + normal + uv0 +
            // tangent (12 floats).  uv1 is deliberately excluded (the task
            // spec key); -0.0f is normalized to +0.0f so identical vertices
            // never split on signed zero.
            //--------------------------------------------------------------
            struct VertexKey
            {
                uint32_t bits[12];
                bool operator==(const VertexKey &o) const
                {
                    return std::memcmp(bits, o.bits, sizeof(bits)) == 0;
                }
            };
            struct VertexKeyHash
            {
                size_t operator()(const VertexKey &k) const
                {
                    // FNV-1a over the 48 raw bytes
                    size_t h = (sizeof(size_t) == 8) ? 1469598103934665603ull : 2166136261u;
                    for (uint32_t b : k.bits)
                    {
                        h ^= (size_t)b;
                        h *= (sizeof(size_t) == 8) ? 1099511628211ull : 16777619u;
                    }
                    return h;
                }
            };
            static inline uint32_t fbxFloatBits(float f)
            {
                if (f == 0.0f) f = 0.0f; // flush -0.0f
                uint32_t b;
                std::memcpy(&b, &f, 4);
                return b;
            }

            //--------------------------------------------------------------
            // Mesh construction from an Objects/Geometry node.
            // Strategy: expand every polygon corner into a full Vertex
            // (per-polygon-vertex expansion, no sharing assumptions), fan
            // triangulate N-gons, then dedupe identical (position, normal,
            // uv0, tangent) tuples through a hash map.
            //--------------------------------------------------------------
            static bool fbxBuildMesh(const FbxObject &geomObj, const Math::Mat4 &A,
                                     bool transformData, MeshData &mesh,
                                     int32_t &outMaterialSlot, int32_t &outSlotCount)
            {
                const FbxNode &geom = *geomObj.node;
                outMaterialSlot = 0;
                outSlotCount = 0;

                mesh.name = geomObj.name.empty() ? std::string("fbx_mesh") : geomObj.name;

                // ---- control points ------------------------------------
                std::vector<double> verts;
                const FbxNode *vnode = geom.child("Vertices");
                if (!vnode || !nodeDoubleArray(*vnode, verts) || verts.size() < 9 ||
                    verts.size() % 3 != 0)
                {
                    GEN_LOG_WARN("[FBX] geometry '%s': missing/invalid Vertices array", mesh.name.c_str());
                    return false;
                }
                size_t numVerts = verts.size() / 3;
                if (numVerts > kFbxMaxVertices)
                {
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu vertices exceeds cap", mesh.name.c_str(), numVerts);
                    return false;
                }
                if (const FbxNode *vc = geom.child("VertexCount"))
                {
                    std::vector<double> declared;
                    if (nodeDoubleArray(*vc, declared) && !declared.empty() &&
                        (size_t)declared[0] != numVerts)
                        GEN_LOG_WARN("[FBX] geometry '%s': VertexCount %d != Vertices/3 %zu (trusting array)",
                                     mesh.name.c_str(), (int)declared[0], numVerts);
                }

                // ---- polygon vertex index ------------------------------
                std::vector<int64_t> pri;
                const FbxNode *pnode = geom.child("PolygonVertexIndex");
                if (!pnode || !nodeIntArray(*pnode, pri) || pri.empty())
                {
                    GEN_LOG_WARN("[FBX] geometry '%s': missing/invalid PolygonVertexIndex", mesh.name.c_str());
                    return false;
                }
                if (pri.size() > kFbxMaxCorners)
                {
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu polygon corners exceeds cap", mesh.name.c_str(), pri.size());
                    return false;
                }

                // ---- attribute layers ----------------------------------
                static const char *normalValueNames[] = {"Normals", nullptr};
                static const char *normalIndexNames[] = {"NormalsIndex", nullptr};
                static const char *uvValueNames[] = {"UV", nullptr};
                static const char *uvIndexNames[] = {"UVIndex", nullptr};
                static const char *tangentValueNames[] = {"Tangents", "Tangent", nullptr};
                static const char *tangentIndexNames[] = {"TangentIndex", "TangentsIndex", nullptr};
                static const char *colorValueNames[] = {"Colors", nullptr};
                static const char *colorIndexNames[] = {"ColorsIndex", nullptr};

                FbxLayer normals, uv0, uv1, tangents, colors;
                bool haveNormals = false, haveUv0 = false, haveUv1 = false,
                     haveTangents = false;

                std::vector<const FbxNode *> normalElems = geom.childrenNamed("LayerElementNormal");
                if (!normalElems.empty())
                {
                    if (normalElems.size() > 1)
                        GEN_LOG_WARN("[FBX] geometry '%s': %zu normal layers - using the first",
                                     mesh.name.c_str(), normalElems.size());
                    haveNormals = fbxReadLayer(*normalElems[0], normalValueNames, normalIndexNames, 3, normals);
                }

                std::vector<const FbxNode *> uvElems = geom.childrenNamed("LayerElementUV");
                for (size_t u = 0; u < uvElems.size() && u < 2; ++u)
                {
                    FbxLayer &target = (u == 0) ? uv0 : uv1;
                    bool ok = fbxReadLayer(*uvElems[u], uvValueNames, uvIndexNames, 2, target);
                    if (u == 0) haveUv0 = ok;
                    else haveUv1 = ok;
                }
                if (uvElems.size() > 2)
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu UV layers - keeping the first two",
                                 mesh.name.c_str(), uvElems.size());

                std::vector<const FbxNode *> tangentElems = geom.childrenNamed("LayerElementTangent");
                if (!tangentElems.empty())
                {
                    // FBX >= 7.2 stores 4-component tangents (xyz + handedness
                    // w); older files store 3.  Disambiguate with the expected
                    // element count from the mapping when possible.
                    FbxLayer probe;
                    probe.mapping = FbxLayer::None;
                    std::string mapping = "bypolygonvertex";
                    if (const FbxNode *c = tangentElems[0]->child("MappingInformationType"))
                    {
                        std::string s = nodeString(*c);
                        if (!s.empty()) mapping = fbxToLower(s);
                    }
                    int64_t expected = -1;
                    if (mapping == "allsame") expected = 1;
                    else if (mapping == "byvertex") expected = (int64_t)numVerts;
                    else if (mapping == "bypolygonvertex") expected = (int64_t)pri.size();
                    int stride = 4;
                    std::vector<double> rawVals;
                    for (const char *const *vn = tangentValueNames; *vn; ++vn)
                    {
                        const FbxNode *c = tangentElems[0]->child(*vn);
                        if (c && nodeDoubleArray(*c, rawVals) && !rawVals.empty()) break;
                    }
                    bool s4 = !rawVals.empty() && rawVals.size() % 4 == 0;
                    bool s3 = !rawVals.empty() && rawVals.size() % 3 == 0;
                    if (s4 && s3 && expected > 0)
                    {
                        if ((int64_t)(rawVals.size() / 4) == expected) stride = 4;
                        else if ((int64_t)(rawVals.size() / 3) == expected) stride = 3;
                    }
                    else if (!s4 && s3) stride = 3;
                    if (!s4 && !s3)
                    {
                        GEN_LOG_WARN("[FBX] geometry '%s': tangent array size %zu fits no stride",
                                     mesh.name.c_str(), rawVals.size());
                    }
                    else
                    {
                        haveTangents = fbxReadLayer(*tangentElems[0], tangentValueNames,
                                                    tangentIndexNames, stride, tangents);
                    }
                }

                // Vertex colors are parsed for completeness but the engine
                // Vertex layout has no color attribute - the data is dropped.
                std::vector<const FbxNode *> colorElems = geom.childrenNamed("LayerElementColor");
                if (!colorElems.empty())
                {
                    if (fbxReadLayer(*colorElems[0], colorValueNames, colorIndexNames, 4, colors))
                        GEN_LOG_INFO("[FBX] geometry '%s': vertex colors present but unsupported by the "
                                     "engine vertex layout - ignored", mesh.name.c_str());
                }

                // ---- per-polygon material slots -------------------------
                // LayerElementMaterial: the "Materials" array holds, per
                // element (AllSame -> one entry, ByPolygon -> one per
                // polygon), an index into the list of materials connected to
                // the owning model.  ReferenceInformationType is nominally
                // "IndexToDirect" here but there is no second indirection
                // array - the values ARE the slots, so read them directly.
                std::vector<const FbxNode *> matElems = geom.childrenNamed("LayerElementMaterial");
                if (!matElems.empty())
                {
                    const FbxNode *arr = matElems[0]->child("Materials");
                    std::vector<int64_t> slots;
                    if (arr && nodeIntArray(*arr, slots) && !slots.empty())
                    {
                        std::unordered_set<int64_t> distinct(slots.begin(), slots.end());
                        outSlotCount = (int32_t)distinct.size();
                        outMaterialSlot = (int32_t)slots[0];
                        for (int64_t s : slots)
                            if (s < -1) { outMaterialSlot = 0; break; } // sanity clamp
                    }
                }

                // ---- triangulate + expand + dedupe ----------------------
                std::unordered_map<VertexKey, uint32_t, VertexKeyHash> dedupe;
                dedupe.reserve(std::min<size_t>(pri.size() + 16, 1u << 20));
                mesh.vertices.reserve(std::min<size_t>(pri.size(), kFbxMaxVertices));
                mesh.indices.reserve(pri.size() + pri.size() / 2);

                size_t normalFails = 0, uvFails = 0, tangentFails = 0, skippedPolys = 0;
                std::vector<int64_t> poly; // corner vertex indices of current polygon

                // Expands one polygon corner into a deduplicated vertex index.
                auto emitCorner = [&](int64_t vertIdx, int64_t polyIdx, int64_t pvIdx) -> uint32_t
                {
                    Vertex v;
                    // Position (+ change of basis / unit scale)
                    Math::Vec3 p((float)verts[(size_t)vertIdx * 3 + 0],
                                 (float)verts[(size_t)vertIdx * 3 + 1],
                                 (float)verts[(size_t)vertIdx * 3 + 2]);
                    if (transformData) p = A.transformPoint(p);
                    v.position[0] = p.x; v.position[1] = p.y; v.position[2] = p.z;

                    // Normal
                    double tmp[4] = {0, 0, 0, 0};
                    if (haveNormals)
                    {
                        if (fbxSampleLayer(normals, polyIdx, vertIdx, pvIdx, tmp, 3))
                        {
                            Math::Vec3 n((float)tmp[0], (float)tmp[1], (float)tmp[2]);
                            if (transformData) n = A.transformDirection(n);
                            n = Math::normalize(n);
                            v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
                        }
                        else ++normalFails;
                    }

                    // UV sets
                    if (haveUv0)
                    {
                        if (fbxSampleLayer(uv0, polyIdx, vertIdx, pvIdx, tmp, 2))
                        {
                            v.uv0[0] = (float)tmp[0];
                            v.uv0[1] = (float)tmp[1];
                        }
                        else ++uvFails;
                    }
                    if (haveUv1)
                    {
                        if (fbxSampleLayer(uv1, polyIdx, vertIdx, pvIdx, tmp, 2))
                        {
                            v.uv1[0] = (float)tmp[0];
                            v.uv1[1] = (float)tmp[1];
                        }
                    }

                    // Tangent (xyz direction + w handedness)
                    if (haveTangents)
                    {
                        int comps = std::min(4, tangents.stride);
                        tmp[3] = -1.0; // 3-component tangents: assume w = -1
                        if (fbxSampleLayer(tangents, polyIdx, vertIdx, pvIdx, tmp, comps))
                        {
                            Math::Vec3 t((float)tmp[0], (float)tmp[1], (float)tmp[2]);
                            if (transformData) t = A.transformDirection(t);
                            t = Math::normalize(t);
                            v.tangent[0] = t.x; v.tangent[1] = t.y; v.tangent[2] = t.z;
                            v.tangent[3] = (float)tmp[3];
                        }
                        else ++tangentFails;
                    }

                    VertexKey key;
                    key.bits[0] = fbxFloatBits(v.position[0]);
                    key.bits[1] = fbxFloatBits(v.position[1]);
                    key.bits[2] = fbxFloatBits(v.position[2]);
                    key.bits[3] = fbxFloatBits(v.normal[0]);
                    key.bits[4] = fbxFloatBits(v.normal[1]);
                    key.bits[5] = fbxFloatBits(v.normal[2]);
                    key.bits[6] = fbxFloatBits(v.uv0[0]);
                    key.bits[7] = fbxFloatBits(v.uv0[1]);
                    key.bits[8] = fbxFloatBits(v.tangent[0]);
                    key.bits[9] = fbxFloatBits(v.tangent[1]);
                    key.bits[10] = fbxFloatBits(v.tangent[2]);
                    key.bits[11] = fbxFloatBits(v.tangent[3]);
                    auto it = dedupe.find(key);
                    if (it != dedupe.end()) return it->second;
                    uint32_t index = (uint32_t)mesh.vertices.size();
                    mesh.vertices.push_back(v);
                    dedupe.emplace(key, index);
                    return index;
                };

                // Walk PolygonVertexIndex.  A negative entry terminates its
                // polygon; the real control point index is ~entry.  Corners
                // of an N-gon are fan triangulated as (0, k, k+1).
                int64_t polyIdx = 0;
                size_t cursor = 0;
                while (cursor < pri.size())
                {
                    size_t end = cursor;
                    while (end < pri.size() && pri[end] >= 0) ++end;
                    bool terminated = (end < pri.size());
                    if (!terminated && end > cursor)
                        GEN_LOG_WARN("[FBX] geometry '%s': last polygon not terminated by a negative "
                                     "index - treating end of array as terminator", mesh.name.c_str());
                    // The negative entry is itself the polygon's LAST corner
                    // (real index = ~entry), so terminated polygons span
                    // [cursor, end] inclusive.
                    size_t n = end - cursor;
                    if (terminated) ++n;
                    if (n >= 3)
                    {
                        poly.clear();
                        poly.reserve(n);
                        bool polyOk = true;
                        for (size_t k = 0; k < n; ++k)
                        {
                            int64_t raw = pri[cursor + k];
                            int64_t idx = (raw < 0) ? ~raw : raw;
                            if (idx < 0 || (size_t)idx >= numVerts)
                            {
                                polyOk = false;
                                break;
                            }
                            poly.push_back(idx);
                        }
                        if (polyOk)
                        {
                            uint32_t i0 = emitCorner(poly[0], polyIdx, (int64_t)(cursor + 0));
                            for (size_t k = 1; k + 1 < n; ++k)
                            {
                                uint32_t i1 = emitCorner(poly[k], polyIdx, (int64_t)(cursor + k));
                                uint32_t i2 = emitCorner(poly[k + 1], polyIdx, (int64_t)(cursor + k + 1));
                                mesh.indices.push_back(i0);
                                mesh.indices.push_back(i1);
                                mesh.indices.push_back(i2);
                            }
                        }
                        else ++skippedPolys;
                    }
                    else if (n > 0)
                    {
                        ++skippedPolys; // degenerate 1-2 corner "polygon"
                    }
                    cursor = end + 1; // skip the terminator (or run past the end)
                    ++polyIdx;
                }

                if (skippedPolys > 0)
                    GEN_LOG_WARN("[FBX] geometry '%s': skipped %zu degenerate/corrupt polygon(s)",
                                 mesh.name.c_str(), skippedPolys);
                if (normalFails > 0)
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu normal lookup(s) out of range",
                                 mesh.name.c_str(), normalFails);
                if (uvFails > 0)
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu UV lookup(s) out of range",
                                 mesh.name.c_str(), uvFails);
                if (tangentFails > 0)
                    GEN_LOG_WARN("[FBX] geometry '%s': %zu tangent lookup(s) out of range",
                                 mesh.name.c_str(), tangentFails);

                if (mesh.indices.empty() || mesh.vertices.empty())
                {
                    GEN_LOG_WARN("[FBX] geometry '%s': produced no triangles", mesh.name.c_str());
                    return false;
                }

                // Normals: use file data when complete, otherwise compute
                // flat/area-weighted normals so the mesh always renders.
                if (!haveNormals || normalFails > 0)
                {
                    if (haveNormals && normalFails > 0)
                        GEN_LOG_WARN("[FBX] geometry '%s': recomputing normals after lookup failures",
                                     mesh.name.c_str());
                    mesh.computeNormals(); // also sets has_normals
                }
                else
                {
                    mesh.has_normals = true;
                }

                // Tangents: prefer file data; otherwise derive from uv0 when
                // the mesh has UVs (nothing sensible to derive from without).
                if (!haveTangents || tangentFails > 0)
                {
                    if (haveUv0 && uvFails == 0)
                    {
                        mesh.computeTangents(); // also sets has_tangents
                    }
                    else
                    {
                        mesh.has_tangents = false;
                    }
                }
                else
                {
                    mesh.has_tangents = true;
                }

                mesh.has_uv1 = haveUv1;
                mesh.has_skin = false; // skinning deliberately not applied (see loadFbxModel)
                mesh.computeBounds();
                return true;
            }

            //--------------------------------------------------------------
            // Materials & textures
            //--------------------------------------------------------------
            static void fbxBuildMaterial(const FbxObject &matObj, MaterialData &m)
            {
                const FbxNode &node = *matObj.node;
                m.name = matObj.name.empty() ? std::string("fbx_material") : matObj.name;

                P70Value diffuse = findProperty(&node, "DiffuseColor");
                P70Value diffuseFactor = findProperty(&node, "DiffuseFactor");
                P70Value emissive = findProperty(&node, "EmissiveColor");
                P70Value ambient = findProperty(&node, "AmbientColor");
                P70Value specular = findProperty(&node, "SpecularColor");
                P70Value shininess = findProperty(&node, "Shininess");
                if (!shininess.found) shininess = findProperty(&node, "ShininessExponent");
                P70Value reflection = findProperty(&node, "ReflectionFactor");
                P70Value transparency = findProperty(&node, "TransparencyFactor");
                P70Value opacity = findProperty(&node, "Opacity");

                // FBX traditional materials (Phong/Blinn/Lambert) have no PBR
                // channels.  Mapping used here:
                //   base_color  = DiffuseColor * DiffuseFactor
                //   metallic    = 0 (specular-workflow assets carry no metalness)
                //   roughness   ~= clamp(1 - Shininess/128, 0.04, 1)
                // The roughness formula is an APPROXIMATION that maps the
                // Blinn-Phong exponent onto perceptual roughness; it is not
                // physically derived.
                float dr = 1.0f, dg = 1.0f, db = 1.0f;
                if (diffuse.found)
                {
                    Math::Vec3 c = diffuse.vec3(Math::Vec3(1, 1, 1));
                    dr = c.x; dg = c.y; db = c.z;
                }
                float df = (float)diffuseFactor.first(1.0);
                float alpha = 1.0f;
                if (transparency.found) alpha = 1.0f - (float)transparency.first(0.0);
                else if (opacity.found) alpha = (float)opacity.first(1.0);
                m.base_color_factor = Math::Color(dr * df, dg * df, db * df, Math::clampf(alpha, 0.0f, 1.0f));
                if (m.base_color_factor.a < 0.999f)
                {
                    // FBX has no alpha-test cutoff; treat any transparency as
                    // blended.
                    m.alpha_mode = MaterialData::AlphaMode::Blend;
                }
                m.metallic_factor = 0.0f;
                if (shininess.found)
                {
                    double s = shininess.first(0.0);
                    m.roughness_factor = Math::clampf((float)(1.0 - s / 128.0), 0.04f, 1.0f);
                }
                if (emissive.found)
                {
                    Math::Vec3 e = emissive.vec3(Math::Vec3(0, 0, 0));
                    m.emissive_factor = Math::Color(e.x, e.y, e.z, 1.0f);
                }
                // Read for completeness / logging; the metallic-roughness
                // contract has no slots for legacy specular workflow values.
                if (ambient.found || specular.found || reflection.found)
                {
                    GEN_LOG_DEBUG("[FBX] material '%s': ambient/specular/reflection values ignored "
                                  "(PBR target)", m.name.c_str());
                }
            }

            static std::string fbxResolveTexturePath(const std::string &rawIn, const std::string &fbxDir)
            {
                if (rawIn.empty()) return std::string();
                std::string raw = rawIn;
                if (raw.compare(0, 7, "file://") == 0) raw = raw.substr(7);
                for (char &c : raw)
                    if (c == '\\') c = '/';
                bool absolute = (!raw.empty() && raw.front() == '/') ||
                                (raw.size() > 2 && raw[1] == ':'); // windows drive letter
                std::vector<std::string> candidates;
                candidates.push_back(raw);
                if (!fbxDir.empty())
                {
                    candidates.push_back(fbxJoinPath(fbxDir, raw));
                    std::string base = raw.substr(raw.find_last_of('/') + 1);
                    candidates.push_back(fbxJoinPath(fbxDir, base));
                }
                (void)absolute;
                for (const std::string &c : candidates)
                    if (fbxFileExists(c)) return c;
                return raw; // not found on disk - keep the original reference
            }

            struct FbxTextureInfo
            {
                std::string name;
                std::string fileName;
            };

            // Gathers "FileName"/"RelativeFilename" from a Texture or Video
            // object (Properties70 first, then plain child nodes for 6.x).
            static std::string fbxTextureFileName(const FbxNode &node)
            {
                static const char *names[] = {"RelativeFilename", "FileName", "Filename"};
                for (const char *name : names)
                {
                    P70Value v = findProperty(&node, name);
                    if (v.found && !v.str.empty()) return v.str;
                }
                return std::string();
            }

            static void fbxApplyTextures(ModelData &model,
                                         const FbxRegistry &textures,
                                         const FbxRegistry &videos,
                                         const FbxRegistry &materials,
                                         const std::vector<FbxLink> &links,
                                         const std::string &fbxDir)
            {
                if (textures.objs.empty()) return;

                // Texture -> Video (media source) links first.
                std::unordered_map<int64_t, int64_t> texToVideo; // texture idx -> video idx
                for (const FbxLink &l : links)
                {
                    int64_t ti = resolveSide(textures, l.src);
                    int64_t vi = resolveSide(videos, l.dst);
                    if (ti >= 0 && vi >= 0) texToVideo[ti] = vi;
                }

                for (const FbxLink &l : links)
                {
                    int64_t ti = resolveSide(textures, l.src);
                    if (ti < 0) continue;
                    // Destination must be a material.  Materials were pushed
                    // into model->materials in registry order, so the object
                    // index IS the material index (works for id-based 7.x and
                    // name-based 6.x connections alike).
                    int32_t materialIndex = (int32_t)resolveSide(materials, l.dst);
                    if (materialIndex < 0 || (size_t)materialIndex >= model.materials.size()) continue;

                    const FbxObject &texObj = textures.objs[(size_t)ti];
                    FbxTextureInfo info;
                    info.name = fbxToLower(texObj.name);
                    info.fileName = fbxTextureFileName(*texObj.node);
                    if (info.fileName.empty())
                    {
                        auto vit = texToVideo.find(ti);
                        if (vit != texToVideo.end())
                            info.fileName = fbxTextureFileName(*videos.objs[(size_t)vit->second].node);
                    }
                    if (info.fileName.empty())
                    {
                        GEN_LOG_WARN("[FBX] texture '%s' has no file name - skipped", texObj.name.c_str());
                        continue;
                    }

                    std::string resolved = fbxResolveTexturePath(info.fileName, fbxDir);
                    bool onDisk = fbxFileExists(resolved);

                    TextureRef ref;
                    ref.path = resolved;
                    ref.valid = onDisk;
                    if (!onDisk)
                        GEN_LOG_WARN("[FBX] texture file '%s' not found on disk (reference kept)",
                                     resolved.c_str());

                    // Slot selection: the OP connection property wins, with a
                    // name-based fallback ("normal"/"bump" in the texture or
                    // file name means a normal map even if mislabeled).
                    std::string slot = fbxToLower(l.slot);
                    std::string fileLower = fbxToLower(resolved);
                    bool looksLikeNormal = info.name.find("normal") != std::string::npos ||
                                           info.name.find("bump") != std::string::npos ||
                                           fileLower.find("normal") != std::string::npos ||
                                           fileLower.find("bump") != std::string::npos ||
                                           slot == "normalmap" || slot == "bump";
                    MaterialData &mat = model.materials[(size_t)materialIndex];
                    TextureRef *target = nullptr;
                    if (looksLikeNormal && slot != "diffusecolor")
                    {
                        target = &mat.normal;
                        ref.srgb = false;
                    }
                    else if (slot == "diffusecolor" || slot.empty())
                    {
                        target = &mat.base_color;
                        ref.srgb = true;
                    }
                    else if (slot == "normalmap" || slot == "bump")
                    {
                        target = &mat.normal;
                        ref.srgb = false;
                    }
                    else if (slot == "emissivecolor")
                    {
                        target = &mat.emissive;
                        ref.srgb = true;
                    }
                    else if (slot == "ambientcolor")
                    {
                        // Closest glTF analogue of an FBX ambient map.
                        target = &mat.occlusion;
                        ref.srgb = false;
                    }
                    else
                    {
                        GEN_LOG_WARN("[FBX] texture slot '%s' has no PBR equivalent - ignored", l.slot.c_str());
                        continue;
                    }
                    if (target->valid)
                    {
                        GEN_LOG_WARN("[FBX] material '%s': slot already bound - keeping first texture",
                                     mat.name.c_str());
                        continue;
                    }
                    *target = ref;
                    GEN_LOG_INFO("[FBX] material '%s': bound texture '%s' (%s)",
                                 mat.name.c_str(), resolved.c_str(),
                                 target == &mat.normal ? "normal" : target == &mat.emissive ? "emissive" :
                                 target == &mat.occlusion ? "occlusion" : "base color");
                }
            }

            //--------------------------------------------------------------
            // Model local transform
            //--------------------------------------------------------------
            static Math::Mat4 fbxModelLocalTransform(const FbxNode &model, bool &warnedExtras)
            {
                P70Value t = findProperty(&model, "Lcl Translation");
                P70Value r = findProperty(&model, "Lcl Rotation");
                P70Value s = findProperty(&model, "Lcl Scaling");
                P70Value pre = findProperty(&model, "PreRotation");
                P70Value order = findProperty(&model, "RotationOrder");

                Math::Vec3 tv = t.vec3(Math::Vec3(0, 0, 0));
                Math::Vec3 rv = r.vec3(Math::Vec3(0, 0, 0));
                Math::Vec3 sv = s.vec3(Math::Vec3(1, 1, 1));
                Math::Vec3 prev = pre.vec3(Math::Vec3(0, 0, 0));
                int rotOrder = (order.found && !order.numbers.empty()) ? (int)order.numbers[0] : 0;
                if (rotOrder < 0 || rotOrder > 5)
                {
                    GEN_LOG_WARN("[FBX] unsupported RotationOrder %d - falling back to XYZ", rotOrder);
                    rotOrder = 0;
                }

                // Pivot offsets / post rotation are part of the full FBX
                // evaluation chain but rare in game exports; warn once.
                if (!warnedExtras)
                {
                    static const char *extras[] = {"RotationPivot", "ScalingPivot", "PostRotation",
                                                   "RotationOffset", "PreferedAngle"};
                    for (const char *name : extras)
                    {
                        P70Value v = findProperty(&model, name);
                        if (v.found)
                        {
                            Math::Vec3 x = v.vec3(Math::Vec3(0, 0, 0));
                            if (std::fabs(x.x) > 1e-6f || std::fabs(x.y) > 1e-6f || std::fabs(x.z) > 1e-6f)
                            {
                                GEN_LOG_WARN("[FBX] '%s' on a model is not supported and was ignored "
                                             "(transforms may be offset)", name);
                                warnedExtras = true;
                                break;
                            }
                        }
                    }
                }

                Math::Mat4 R = fbxEulerToMatrix(rv.x, rv.y, rv.z, rotOrder);
                // PreRotation always uses XYZ order (matches assimp/SDK).
                Math::Mat4 Rpre = fbxEulerToMatrix(prev.x, prev.y, prev.z, 0);
                return Math::Mat4::translate(tv) * Rpre * R * Math::Mat4::scale(sv);
            }

        } // anonymous namespace

        //======================================================================
        // Entry point
        //======================================================================
        std::shared_ptr<ModelData> loadFbxModel(const std::string &path)
        {
            std::vector<uint8_t> data;
            if (!fbxReadWholeFile(path, data)) return nullptr;

            // ----------------------------------------------------------
            // 1. Parse into the generic node tree (binary or ASCII).
            // ----------------------------------------------------------
            std::vector<std::shared_ptr<FbxNode>> roots;
            uint32_t version = 0;
            bool isBinary = data.size() >= 27 &&
                            std::memcmp(data.data(), kFbxBinaryMagic, sizeof(kFbxBinaryMagic)) == 0;
            if (isBinary)
            {
                FbxBinaryParser parser;
                if (!parser.parse(data, version, roots)) return nullptr;
            }
            else
            {
                // Heuristic ASCII detection: mostly printable leading bytes.
                size_t probe = std::min<size_t>(data.size(), 4096);
                size_t printable = 0;
                for (size_t i = 0; i < probe; ++i)
                {
                    uint8_t c = data[i];
                    if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7F)) ++printable;
                }
                if (probe == 0 || printable * 20 < probe * 19) // < 95% printable
                {
                    GEN_LOG_ERROR("[FBX] '%s' is neither FBX binary nor FBX ASCII", path.c_str());
                    return nullptr;
                }
                std::string text(data.begin(), data.end());
                FbxAsciiParser parser;
                if (!parser.parse(text, roots)) return nullptr;
                // Recover the version from FBXHeaderExtension when present.
                const FbxNode *ext = findRootNode(roots, "FBXHeaderExtension");
                if (ext)
                {
                    const FbxNode *vnode = ext->child("FBXVersion");
                    if (vnode)
                    {
                        std::vector<double> vals;
                        if (nodeDoubleArray(*vnode, vals) && !vals.empty())
                            version = (uint32_t)vals[0];
                    }
                }
                GEN_LOG_INFO("[FBX] ASCII document, version %u", version ? version : 7400);
            }
            data.clear();
            data.shrink_to_fit();

            // ----------------------------------------------------------
            // 2. Global settings -> change-of-basis matrix A.
            // ----------------------------------------------------------
            FbxAxisInfo axis = readGlobalSettings(roots);
            Math::Mat4 axisFix = buildAxisFix(axis);
            float unitScale = 1.0f;
            if (axis.unitScaleFactor > 1e-6 && axis.unitScaleFactor < 1e6)
                unitScale = (float)axis.unitScaleFactor;
            else
                GEN_LOG_WARN("[FBX] implausible UnitScaleFactor %g - using 1.0", axis.unitScaleFactor);
            Math::Mat4 A = axisFix * Math::Mat4::scale(Math::Vec3(unitScale));
            Math::Mat4 Ainv = A.inversed();
            bool transformData = !matIsIdentity(A);
            GEN_LOG_INFO("[FBX] axis system: up=%d(%d) front=%d(%d) coord=%d(%d), unit scale %g -> %s",
                         axis.upAxis, axis.upAxisSign, axis.frontAxis, axis.frontAxisSign,
                         axis.coordAxis, axis.coordAxisSign, axis.unitScaleFactor,
                         transformData ? "conversion applied" : "identity (matches engine convention)");
            if (axis.frontAxis != 2 || axis.coordAxis != 0)
                GEN_LOG_WARN("[FBX] non-default FrontAxis/CoordAxis combination - only the up-axis "
                             "conversion is applied (handedness/front flips are not corrected)");

            // ----------------------------------------------------------
            // 3. Object registries & connections.
            // ----------------------------------------------------------
            int64_t syntheticId = 0; // negative synthetic ids for 6.x name-only objects
            FbxRegistry geometries, models, materials, textures, videos, deformers, nodeAttrs;
            collectObjects(roots, "Geometry", geometries, syntheticId);
            collectObjects(roots, "Model", models, syntheticId);
            collectObjects(roots, "Material", materials, syntheticId);
            collectObjects(roots, "Texture", textures, syntheticId);
            collectObjects(roots, "Video", videos, syntheticId);
            collectObjects(roots, "Deformer", deformers, syntheticId);
            collectObjects(roots, "NodeAttribute", nodeAttrs, syntheticId);

            std::vector<FbxLink> links;
            collectConnections(roots, links);

            // Animation is out of scope for the static import path.
            {
                size_t animStacks = 0, animCurves = 0;
                for (const auto &root : roots)
                {
                    if (!root || root->name != "Objects") continue;
                    for (const auto &c : root->children)
                    {
                        if (!c) continue;
                        if (c->name == "AnimStack") ++animStacks;
                        else if (c->name == "AnimCurve" || c->name == "AnimCurveNode") ++animCurves;
                    }
                }
                if (animStacks || animCurves)
                    GEN_LOG_WARN("[FBX] %zu animation stack(s) / %zu curve node(s) present - animation "
                                 "is ignored (static import)", animStacks, animCurves);
            }

            auto model = std::make_shared<ModelData>();
            model->name = fbxBaseNameNoExt(path);
            model->source_path = path;
            model->importer = "fbx";

            // ----------------------------------------------------------
            // 4. Materials.  Materials are pushed in registry order, so a
            //    material's object index equals its ModelData index.
            // ----------------------------------------------------------
            for (size_t i = 0; i < materials.objs.size(); ++i)
            {
                MaterialData m;
                fbxBuildMaterial(materials.objs[i], m);
                model->materials.push_back(std::move(m));
            }

            // ----------------------------------------------------------
            // 5. Textures -> material slots.
            // ----------------------------------------------------------
            fbxApplyTextures(*model, textures, videos, materials, links, fbxDirOf(path));

            // ----------------------------------------------------------
            // 6. Skinning detection.  Deformer evaluation (Skin/Cluster
            //    bind matrices) is deliberately NOT implemented: the engine
            //    contract (MeshData/NodeData) has no storage for inverse
            //    bind matrices, so joints/weights alone could not be
            //    consumed correctly.  Skinned meshes import in bind pose.
            // ----------------------------------------------------------
            std::unordered_set<int64_t> skinnedGeomIdx;
            size_t clusterCount = 0;
            for (size_t di = 0; di < deformers.objs.size(); ++di)
            {
                const FbxObject &d = deformers.objs[di];
                std::string sub = fbxToLower(d.subtype);
                if (sub == "cluster" || sub.find("cluster") != std::string::npos) ++clusterCount;
                if (sub != "skin") continue;
                for (const FbxLink &l : links)
                {
                    if (resolveSide(deformers, l.src) == (int64_t)di)
                    {
                        int64_t gi = resolveSide(geometries, l.dst);
                        if (gi >= 0) skinnedGeomIdx.insert(gi);
                    }
                }
            }
            if (!skinnedGeomIdx.empty())
                GEN_LOG_WARN("[FBX] %zu skinned geometry node(s) (%zu cluster(s)) found - skinning is "
                             "NOT applied; meshes import in bind pose with has_skin=false",
                             skinnedGeomIdx.size(), clusterCount);

            // NodeAttribute types (camera/light markers) - informational.
            {
                size_t cameras = 0, lights = 0;
                for (const FbxObject &a : nodeAttrs.objs)
                {
                    std::string sub = fbxToLower(a.subtype);
                    if (sub == "camera") ++cameras;
                    else if (sub == "light") ++lights;
                }
                if (cameras || lights)
                    GEN_LOG_WARN("[FBX] %zu camera / %zu light attribute node(s) ignored (geometry only)",
                                 cameras, lights);
            }

            // ----------------------------------------------------------
            // 7. Geometry -> MeshData.
            // ----------------------------------------------------------
            std::vector<int32_t> meshOfGeom(geometries.objs.size(), -1);
            std::vector<int32_t> meshSlots;   // LayerElementMaterial slot per built mesh
            std::vector<int32_t> meshSlotCounts;
            for (size_t gi = 0; gi < geometries.objs.size(); ++gi)
            {
                const FbxObject &g = geometries.objs[gi];
                std::string subtype = fbxToLower(g.subtype);
                if (!subtype.empty() && subtype != "mesh")
                {
                    GEN_LOG_WARN("[FBX] geometry '%s' of type '%s' unsupported - skipped",
                                 g.name.c_str(), g.subtype.c_str());
                    continue;
                }
                MeshData mesh;
                int32_t slot = 0, slotCount = 0;
                if (!fbxBuildMesh(g, A, transformData, mesh, slot, slotCount))
                {
                    GEN_LOG_WARN("[FBX] geometry '%s' failed to build - skipped", g.name.c_str());
                    continue;
                }
                if (skinnedGeomIdx.count((int64_t)gi))
                {
                    mesh.has_skin = false; // bind pose (see skinning note above)
                }
                if (slotCount > 1)
                    GEN_LOG_WARN("[FBX] mesh '%s' references %d material slots - MeshData holds one "
                                 "material; using slot %d", mesh.name.c_str(), (int)slotCount, (int)slot);
                meshOfGeom[gi] = (int32_t)model->meshes.size();
                meshSlots.push_back(slot);
                meshSlotCounts.push_back(slotCount);
                model->meshes.push_back(std::move(mesh));
            }
            if (model->meshes.empty())
            {
                GEN_LOG_ERROR("[FBX] '%s' contains no usable mesh geometry", path.c_str());
                return nullptr;
            }

            // ----------------------------------------------------------
            // 8. Model nodes + hierarchy.
            // ----------------------------------------------------------
            // Link classification.
            std::vector<int32_t> parentOf(models.objs.size(), -1);
            std::vector<std::vector<int64_t>> modelGeoms(models.objs.size());
            std::vector<std::vector<int64_t>> modelMats(models.objs.size());
            for (const FbxLink &l : links)
            {
                int64_t sModel = resolveSide(models, l.src);
                int64_t dModel = resolveSide(models, l.dst);
                if (sModel >= 0 && dModel >= 0)
                {
                    if (sModel == dModel)
                    {
                        GEN_LOG_WARN("[FBX] model '%s' is its own parent - link ignored",
                                     models.objs[(size_t)sModel].name.c_str());
                        continue;
                    }
                    if (parentOf[(size_t)sModel] >= 0)
                    {
                        GEN_LOG_WARN("[FBX] model '%s' has multiple parents - keeping the first",
                                     models.objs[(size_t)sModel].name.c_str());
                        continue;
                    }
                    parentOf[(size_t)sModel] = (int32_t)dModel;
                    continue;
                }
                int64_t sGeom = resolveSide(geometries, l.src);
                if (sGeom >= 0 && dModel >= 0)
                {
                    modelGeoms[(size_t)dModel].push_back(sGeom);
                    continue;
                }
                int64_t sMat = resolveSide(materials, l.src);
                if (sMat >= 0 && dModel >= 0)
                {
                    modelMats[(size_t)dModel].push_back(sMat);
                }
            }

            // Cycle sanitization: walk each parent chain; a chain that returns
            // to its start is broken at that node (it becomes a root).
            for (size_t mi = 0; mi < models.objs.size(); ++mi)
            {
                int32_t p = parentOf[mi];
                if (p < 0) continue;
                std::unordered_set<int32_t> seen;
                seen.insert((int32_t)mi);
                int32_t walk = p;
                int32_t guard = 0;
                while (walk >= 0 && guard++ <= (int32_t)models.objs.size())
                {
                    if (!seen.insert(walk).second)
                    {
                        GEN_LOG_WARN("[FBX] cyclic model hierarchy at '%s' - breaking the link",
                                     models.objs[mi].name.c_str());
                        parentOf[mi] = -1;
                        break;
                    }
                    walk = parentOf[(size_t)walk];
                }
            }

            bool warnedExtras = false;
            std::vector<char> meshOwned(model->meshes.size(), 0);
            for (size_t mi = 0; mi < models.objs.size(); ++mi)
            {
                const FbxObject &mo = models.objs[mi];
                NodeData nd;
                nd.name = mo.name.empty() ? ("Model_" + std::to_string(mo.id)) : mo.name;
                Math::Mat4 local = fbxModelLocalTransform(*mo.node, warnedExtras);
                // Change of basis: L' = A * L * A^-1 (skipped when A is the
                // identity, which keeps the common case bit-exact).
                nd.local_transform = transformData ? (A * local * Ainv) : local;

                const std::vector<int64_t> &mats = modelMats[mi];
                for (int64_t gi : modelGeoms[mi])
                {
                    if (gi < 0 || (size_t)gi >= meshOfGeom.size()) continue;
                    int32_t meshIdx = meshOfGeom[(size_t)gi];
                    if (meshIdx < 0) continue; // geometry failed to build

                    int32_t slot = meshSlots[(size_t)meshIdx];
                    int32_t matIdx = -1;
                    if (!mats.empty())
                    {
                        size_t pick = (slot >= 0 && (size_t)slot < mats.size()) ? (size_t)slot : 0;
                        int64_t matObjIdx = mats[pick];
                        if (matObjIdx >= 0 && (size_t)matObjIdx < model->materials.size())
                            matIdx = (int32_t)matObjIdx; // object index == material index
                    }

                    if (!meshOwned[(size_t)meshIdx])
                    {
                        meshOwned[(size_t)meshIdx] = 1;
                        model->meshes[(size_t)meshIdx].material_index = matIdx;
                    }
                    else if (model->meshes[(size_t)meshIdx].material_index != matIdx)
                    {
                        // Same geometry, different material: duplicate the mesh.
                        MeshData clone = model->meshes[(size_t)meshIdx];
                        clone.material_index = matIdx;
                        meshIdx = (int32_t)model->meshes.size();
                        model->meshes.push_back(std::move(clone));
                        meshOwned.push_back(1);
                    }
                    nd.meshes.push_back(meshIdx);
                }
                model->nodes.push_back(std::move(nd));
            }

            // Orphan meshes (geometry never connected to a model): give each
            // its own root node so nothing silently disappears.
            for (size_t meshIdx = 0; meshIdx < model->meshes.size(); ++meshIdx)
            {
                if (meshIdx < meshOwned.size() && meshOwned[meshIdx]) continue;
                NodeData nd;
                nd.name = model->meshes[meshIdx].name;
                // A is already baked into the vertices -> identity local.
                nd.local_transform = Math::Mat4::identity();
                nd.meshes.push_back((int32_t)meshIdx);
                if (model->meshes[meshIdx].material_index < 0 && model->materials.size() == 1)
                    model->meshes[meshIdx].material_index = 0; // helpful default
                model->nodes.push_back(std::move(nd));
                model->root_nodes.push_back((int32_t)model->nodes.size() - 1);
            }

            // Parent/child wiring for the model-derived nodes.
            for (size_t mi = 0; mi < models.objs.size(); ++mi)
            {
                int32_t p = parentOf[mi];
                if (p < 0 || (size_t)p >= models.objs.size()) continue;
                model->nodes[(size_t)p].children.push_back((int32_t)mi);
            }
            for (size_t mi = 0; mi < models.objs.size(); ++mi)
                if (parentOf[mi] < 0) model->root_nodes.push_back((int32_t)mi);

            if (model->root_nodes.empty() && !model->nodes.empty())
            {
                GEN_LOG_WARN("[FBX] no root nodes resolved - forcing node 0 as root");
                model->root_nodes.push_back(0);
            }
            if (models.objs.empty())
                GEN_LOG_WARN("[FBX] file has no Model objects - meshes attached to synthetic root nodes");

            // ----------------------------------------------------------
            // 9. Finish.
            // ----------------------------------------------------------
            model->computeBounds();
            GEN_LOG_INFO("[FBX] imported '%s': %zu mesh(es), %zu material(s), %zu node(s), %zu root(s)",
                         model->name.c_str(), model->meshes.size(), model->materials.size(),
                         model->nodes.size(), model->root_nodes.size());
            for (size_t i = 0; i < model->meshes.size(); ++i)
            {
                const MeshData &m = model->meshes[i];
                GEN_LOG_INFO("[FBX]   mesh[%zu] '%s': %zu verts, %zu indices, material %d, N=%d T=%d UV1=%d",
                             i, m.name.c_str(), m.vertices.size(), m.indices.size(), m.material_index,
                             (int)m.has_normals, (int)m.has_tangents, (int)m.has_uv1);
            }
            return model;
        }

    } // namespace ResourceIO
} // namespace Genesis
