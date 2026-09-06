/*
 * Genesis Render - Engine
 * glsl_preprocess.cpp
 */
#include "glsl_preprocess.hpp"

#include <regex>
#include <sstream>

namespace Genesis
{
std::string resolveShaderIncludes(const std::string &source,
                                  const std::function<std::string(const std::string &)> &resolver,
                                  int depth)
{
    if (depth > 16) return source;
    std::ostringstream out;
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line))
    {
        std::smatch m;
        static const std::regex include_re(R"RE(\s*#\s*include\s*"([^"]+)"\s*)RE");
        if (std::regex_match(line, m, include_re))
        {
            std::string inc = resolver(m[1].str());
            out << "// ===== begin include " << m[1].str() << " =====\n";
            out << resolveShaderIncludes(inc, resolver, depth + 1);
            out << "\n// ===== end include " << m[1].str() << " =====\n";
        }
        else
            out << line << "\n";
    }
    return out.str();
}

static int extractBinding(const std::string &quals)
{
    static const std::regex re(R"(binding\s*=\s*(\d+))");
    std::smatch m;
    if (std::regex_search(quals, m, re)) return std::stoi(m[1].str());
    return -1;
}

std::string preprocessGLSLForOpenGL(const std::string &raw)
{
    std::ostringstream out;
    std::istringstream in(raw);
    std::string line;
    bool version_emitted = false;

    auto emitHeader = [&]() {
        if (version_emitted) return;
        out << "#version 410 core\n";
        out << "#define GENESIS_GL 1\n";
        out << "#define gl_VertexIndex gl_VertexID\n";
        out << "#define gl_InstanceIndex gl_InstanceID\n";
        version_emitted = true;
    };

    // sampler/image declarations end with ';'
    static const std::regex sampler_re(
        R"(layout\s*\(([^)]*)\)\s*uniform\s+((?:isampler|usampler|sampler|image)\w*)\s+(\w+)\s*;)");
    // push_constant block header (the opening brace may be on the next line)
    static const std::regex push_re(
        R"(layout\s*\(\s*push_constant\s*\)\s*uniform\s+(\w+)\s*(.*)$)");
    // uniform block header (opening brace same line or next)
    static const std::regex block_re(
        R"(layout\s*\(([^)]*)\)\s*uniform\s+(\w+)\s*(\{?)\s*$)");

    while (std::getline(in, line))
    {
        std::string trimmed = line;
        size_t p = trimmed.find_first_not_of(" \t");
        trimmed = (p == std::string::npos) ? "" : trimmed.substr(p);

        if (trimmed.rfind("#version", 0) == 0) { emitHeader(); continue; }
        if (trimmed.rfind("#extension", 0) == 0)
        {
            // Drop Vulkan-only / include directives.
            if (trimmed.find("GL_EXT") != std::string::npos ||
                trimmed.find("GL_KHR") != std::string::npos ||
                trimmed.find("GL_GOOGLE") != std::string::npos ||
                trimmed.find("separate_shader_objects") != std::string::npos)
                continue;
            out << line << "\n";
            continue;
        }

        std::smatch m;
        if (std::regex_search(line, m, push_re))
        {
            out << "/*@binding=3*/ layout(std140) uniform " << m[1].str();
            std::string tail = m[2].str();
            if (tail.find('{') != std::string::npos) out << " {";
            out << "\n";
            continue;
        }
        std::smatch sm;
        if (std::regex_search(line, sm, sampler_re))
        {
            int binding = extractBinding(sm[1].str());
            if (binding >= 0) out << "/*@binding=" << binding << "*/ ";
            out << "uniform " << sm[2].str() << " " << sm[3].str() << ";\n";
            continue;
        }
        if (std::regex_search(line, m, block_re))
        {
            std::string type = m[2].str();
            if (type.rfind("sampler", 0) == 0 || type.rfind("image", 0) == 0)
            {
                out << line << "\n";
                continue;
            }
            int binding = extractBinding(m[1].str());
            if (binding >= 0) out << "/*@binding=" << binding << "*/ ";
            out << "layout(std140) uniform " << type;
            if (m[3].str() == "{") out << " {";
            out << "\n";
            continue;
        }

        out << line << "\n";
    }
    emitHeader();
    return out.str();
}
}
