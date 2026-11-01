/*
 * Genesis Render - runtime asset package implementation
 */
#include "resource/asset_package.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "resource/resource_types.hpp"

#include "external/json.hpp"

#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Genesis
{
    namespace
    {
        constexpr char kMagic[8] = {'G', 'E', 'N', 'P', 'K', 'G', '0', '1'};
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kMaxPackageSize = 2u * 1024u * 1024u * 1024u;

        uint32_t readU32(const uint8_t *p)
        {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }

        bool safeRelativePath(const std::string &path)
        {
            if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
            if (path.find(':') != std::string::npos) return false;
            std::string part;
            std::istringstream stream(path);
            while (std::getline(stream, part, '/'))
                if (part == "..") return false;
            return true;
        }

        uint32_t crcOf(const uint8_t *data, size_t size)
        {
            uLong crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, data, (uInt)size);
            return (uint32_t)crc;
        }
    } // namespace

    bool AssetPackage::looksLikePackage(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        char magic[8] = {0};
        f.read(magic, 8);
        return f.gcount() == 8 && std::memcmp(magic, kMagic, 8) == 0;
    }

    void AssetPackage::clear()
    {
        loaded_ = false;
        source_path_.clear();
        name_.clear();
        manifest_json_.clear();
        blob_.clear();
        entries_.clear();
        data_offset_ = 0;
    }

    bool AssetPackage::loadFromFile(const std::string &path, std::string *error)
    {
        clear();
        if (!ResourceIO::readFile(path, blob_))
        {
            if (error) *error = "cannot read package: " + path;
            return false;
        }
        if (blob_.size() < 20 || std::memcmp(blob_.data(), kMagic, 8) != 0)
        {
            if (error) *error = "not a Genesis package (bad magic): " + path;
            clear();
            return false;
        }
        size_t cursor = 8;
        auto need = [&](size_t n) { return cursor + n <= blob_.size(); };
        if (!need(12))
        {
            if (error) *error = "truncated package header";
            clear();
            return false;
        }
        const uint32_t version = readU32(blob_.data() + cursor); cursor += 4;
        const uint32_t count = readU32(blob_.data() + cursor); cursor += 4;
        const uint32_t manifest_size = readU32(blob_.data() + cursor); cursor += 4;
        if (version != kVersion)
        {
            if (error) *error = "unsupported package version";
            clear();
            return false;
        }
        if (blob_.size() > kMaxPackageSize || count > 100000)
        {
            if (error) *error = "package is absurdly large";
            clear();
            return false;
        }
        if (!need(manifest_size))
        {
            if (error) *error = "truncated manifest";
            clear();
            return false;
        }
        manifest_json_.assign(reinterpret_cast<const char *>(blob_.data() + cursor), manifest_size);
        cursor += manifest_size;

        entries_.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!need(4)) { if (error) *error = "truncated entry table"; clear(); return false; }
            const uint32_t path_size = readU32(blob_.data() + cursor); cursor += 4;
            if (path_size == 0 || path_size > 4096 || !need((size_t)path_size + 18))
            {
                if (error) *error = "invalid package entry";
                clear();
                return false;
            }
            PackageEntry e;
            e.path.assign(reinterpret_cast<const char *>(blob_.data() + cursor), path_size);
            cursor += path_size;
            e.offset = readU32(blob_.data() + cursor); cursor += 4;
            e.stored_size = readU32(blob_.data() + cursor); cursor += 4;
            e.raw_size = readU32(blob_.data() + cursor); cursor += 4;
            e.crc32 = readU32(blob_.data() + cursor); cursor += 4;
            e.compressed = blob_[cursor] != 0;
            cursor += 4; // compression + 3 reserved
            if (!safeRelativePath(e.path))
            {
                if (error) *error = "unsafe path in package: " + e.path;
                clear();
                return false;
            }
            entries_.push_back(std::move(e));
        }
        if (!need(4))
        {
            if (error) *error = "truncated data size";
            clear();
            return false;
        }
        data_offset_ = (uint32_t)cursor + 4;
        const uint32_t data_size = readU32(blob_.data() + cursor);
        if ((uint64_t)data_offset_ + data_size > blob_.size())
        {
            if (error) *error = "truncated package data";
            clear();
            return false;
        }
        for (const PackageEntry &e : entries_)
        {
            if ((uint64_t)e.offset + e.stored_size > data_size)
            {
                if (error) *error = "entry outside the data blob: " + e.path;
                clear();
                return false;
            }
        }

        source_path_ = path;
        name_ = ResourceIO::pathFilename(path);
        const size_t dot = name_.find_last_of('.');
        if (dot != std::string::npos) name_ = name_.substr(0, dot);
        try
        {
            nlohmann::json manifest = nlohmann::json::parse(manifest_json_);
            if (manifest.contains("name") && manifest["name"].is_string()) name_ = manifest["name"].get<std::string>();
        }
        catch (const std::exception &)
        {
            // The manifest is still exposed as text; only the optional name
            // field is lost.
        }
        loaded_ = true;
        GEN_LOG_INFO("[Package] loaded '%s' (%zu files, %.1f KB raw)", name_.c_str(), entries_.size(),
                     totalRawBytes() / 1024.0);
        return true;
    }

    const PackageEntry *AssetPackage::find(const std::string &path) const
    {
        for (const PackageEntry &e : entries_)
            if (e.path == path) return &e;
        return nullptr;
    }

    bool AssetPackage::read(const std::string &path, std::vector<uint8_t> &out) const
    {
        const PackageEntry *e = find(path);
        if (!e) return false;
        const uint8_t *src = blob_.data() + data_offset_ + e->offset;
        if (!e->compressed)
        {
            out.assign(src, src + e->stored_size);
        }
        else
        {
            out.resize(e->raw_size);
            uLongf dest_len = e->raw_size;
            if (uncompress(out.data(), &dest_len, src, e->stored_size) != Z_OK || dest_len != e->raw_size)
            {
                GEN_LOG_ERROR("[Package] inflate failed for %s", path.c_str());
                out.clear();
                return false;
            }
        }
        if (out.size() != e->raw_size || crcOf(out.data(), out.size()) != e->crc32)
        {
            GEN_LOG_ERROR("[Package] CRC mismatch for %s", path.c_str());
            out.clear();
            return false;
        }
        return true;
    }

    bool AssetPackage::readText(const std::string &path, std::string &out) const
    {
        std::vector<uint8_t> data;
        if (!read(path, data)) return false;
        out.assign(reinterpret_cast<const char *>(data.data()), data.size());
        return true;
    }

    bool AssetPackage::extractAll(const std::string &dir, std::string *error) const
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            if (error) *error = "cannot create " + dir + ": " + ec.message();
            return false;
        }
        for (const PackageEntry &e : entries_)
        {
            if (!safeRelativePath(e.path))
            {
                if (error) *error = "unsafe path: " + e.path;
                return false;
            }
            std::vector<uint8_t> data;
            if (!read(e.path, data))
            {
                if (error) *error = "cannot read " + e.path;
                return false;
            }
            const fs::path target = fs::path(dir) / fs::path(e.path);
            fs::create_directories(target.parent_path(), ec);
            if (ec)
            {
                if (error) *error = "cannot create directory for " + e.path + ": " + ec.message();
                return false;
            }
            if (!ResourceIO::writeFile(target.string(), data.data(), data.size()))
            {
                if (error) *error = "cannot write " + target.string();
                return false;
            }
        }
        return true;
    }

    uint64_t AssetPackage::totalRawBytes() const
    {
        uint64_t bytes = 0;
        for (const PackageEntry &e : entries_) bytes += e.raw_size;
        return bytes;
    }

} // namespace Genesis
