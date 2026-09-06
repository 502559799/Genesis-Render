/*
 * Genesis Render - shader program cache + asset path resolution.
 */
#include "render/shader_library.hpp"
#include "foundation_layer/log.hpp"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace Genesis {
namespace fs = std::filesystem;

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static std::vector<uint32_t> readSpv(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint32_t> d((n + 3) / 4);
    f.read((char*)d.data(), n);
    return d;
}

ShaderLibrary::ShaderLibrary(IDevice* device, std::string root)
    : dev_(device), root_(std::move(root)) {}

IShaderProgram* ShaderLibrary::load(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    std::string base = root_ + "/shaders/" + name;
    std::string vs = readFile(base + ".vert");
    std::string fs = readFile(base + ".frag");
    auto vsSpv = readSpv(base + ".vert.spv");
    auto fsSpv = readSpv(base + ".frag.spv");
    IShaderProgram* p = dev_->createShaderProgram(name, vs, fs, vsSpv, fsSpv);
    cache_[name] = p;
    return p;
}

std::string findAssetRoot(const char* argv0) {
    if (const char* env = std::getenv("GENESIS_ASSETS")) return env;
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (argv0) {
        fs::path exe = fs::weakly_canonical(fs::path(argv0));
        fs::path dir = exe.parent_path();
        candidates.emplace_back(dir / "assets");
        for (int i = 0; i < 4; ++i) {
            dir = dir.parent_path();
            candidates.emplace_back(dir / "assets");
        }
    }
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) {
        candidates.emplace_back(fs::path(cwd) / "assets");
        candidates.emplace_back(fs::path(cwd));
    }
    // Prefer a tree that actually contains compiled SPIR-V (build output),
    // fall back to any directory with shaders/ (GL backend / source tree).
    for (auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "shaders/pbr.vert.spv", ec)) return c.string();
    }
    for (auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "shaders", ec)) return c.string();
    }
    return "assets";
}

} // namespace Genesis
