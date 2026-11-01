/*
 * Genesis Render - runtime asset packages (.gpkg)
 *
 * A package is a single file containing a JSON manifest plus every asset of a
 * complete scene: scene description, materials, offline shaders for the three
 * backends, textures, scripts and configuration.  Format (little endian):
 *
 *   char     magic[8]   "GENPKG01"
 *   uint32   version    1
 *   uint32   file_count
 *   uint32   manifest_size
 *   char     manifest[manifest_size]        (UTF-8 JSON)
 *   repeated file_count times:
 *       uint32 path_size
 *       char   path[path_size]              (relative, '/' separated)
 *       uint32 data_offset                  (from the start of the data blob)
 *       uint32 stored_size
 *       uint32 raw_size
 *       uint32 crc32                        (of the raw bytes)
 *       uint8  compression                  (0 = stored, 1 = zlib deflate)
 *       uint8  reserved[3]
 *   uint32 data_size
 *   char   data[data_size]
 *
 * The offline packer is tools/genesis_pack.py.  Existing engine loaders work
 * on files, so loadFromFile() can extract the package into a cache directory;
 * small text assets can also be read straight from memory.
 */
#ifndef __GENESIS_ASSET_PACKAGE_HPP__
#define __GENESIS_ASSET_PACKAGE_HPP__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    struct PackageEntry
    {
        std::string path;
        uint32_t offset = 0;
        uint32_t stored_size = 0;
        uint32_t raw_size = 0;
        uint32_t crc32 = 0;
        bool compressed = false;
    };

    class AssetPackage
    {
    public:
        AssetPackage() = default;
        ~AssetPackage() = default;

        bool loadFromFile(const std::string &path, std::string *error = nullptr);
        void clear();

        bool loaded() const { return loaded_; }
        const std::string &sourcePath() const { return source_path_; }
        const std::string &name() const { return name_; }
        const std::string &manifestJson() const { return manifest_json_; }
        const std::vector<PackageEntry> &entries() const { return entries_; }

        const PackageEntry *find(const std::string &path) const;
        bool read(const std::string &path, std::vector<uint8_t> &out) const;
        bool readText(const std::string &path, std::string &out) const;
        bool exists(const std::string &path) const { return find(path) != nullptr; }

        // Writes every entry below `dir` (which must be a safe, relative-only
        // path inside the cache), creating parent directories.
        bool extractAll(const std::string &dir, std::string *error = nullptr) const;

        // Sum of the raw file sizes (for UI statistics).
        uint64_t totalRawBytes() const;

        static bool looksLikePackage(const std::string &path);

    private:
        bool loaded_ = false;
        std::string source_path_;
        std::string name_;
        std::string manifest_json_;
        std::vector<uint8_t> blob_; // whole file (needed because entries reference it)
        std::vector<PackageEntry> entries_;
        uint32_t data_offset_ = 0;
    };

    using AssetPackagePtr = std::shared_ptr<AssetPackage>;

} // namespace Genesis

#endif /* __GENESIS_ASSET_PACKAGE_HPP__ */
