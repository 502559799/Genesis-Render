/*
 * Genesis Render - OpenGL RHI backend (core profile 4.1+, tested on 4.5).
 *
 * Windowed : SDL_GL context (Linux/macOS/Windows).
 * Headless : EGL pbuffer on Mesa (Linux CI), or offscreen context elsewhere.
 *
 * The same GLSL source feeds both backends; this backend strips the Vulkan
 * `layout(set=N, ...)` qualifier and, on GL < 4.2, `layout(binding=N)` too,
 * rebinding blocks/samplers by introspected name so macOS 4.1 still works.
 */
#include "rhi/rhi.hpp"
#include "rhi/gl_procs.hpp"
#include "rhi/half.hpp"
#include "foundation_layer/log.hpp"

#include <SDL.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <utility>
#include <regex>

#if defined(__linux__) && !defined(__ANDROID__)
#define GENESIS_EGL_HEADLESS 1
#include <EGL/egl.h>
#endif

#include <algorithm>

namespace Genesis {

// ---------------------------------------------------------------------------
// Format helpers
// ---------------------------------------------------------------------------
static void glImageFormat(RHIFormat f, GLenum& internal, GLenum& fmt, GLenum& type) {
    switch (f) {
    case RHIFormat::R8:        internal = GL_R8;        fmt = GL_RED;  type = GL_UNSIGNED_BYTE; break;
    case RHIFormat::RGBA8:     internal = GL_RGBA8;     fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    case RHIFormat::RGBA8_SRGB:internal = GL_SRGB8_ALPHA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    case RHIFormat::RG16F:     internal = GL_RG16F;     fmt = GL_RG;   type = GL_FLOAT; break;
    case RHIFormat::RGBA16F:   internal = GL_RGBA16F;   fmt = GL_RGBA; type = GL_FLOAT; break;
    case RHIFormat::R32F:      internal = GL_R32F;      fmt = GL_RED;  type = GL_FLOAT; break;
    case RHIFormat::RG32F:     internal = GL_RG32F;     fmt = GL_RG;   type = GL_FLOAT; break;
    default:                   internal = GL_RGBA8;     fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    }
}
static GLboolean glDepthFormat(RHIFormat f, GLenum& internal) {
    if (f == RHIFormat::D32F) { internal = GL_DEPTH_COMPONENT32F; return GL_TRUE; }
    return GL_FALSE;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
struct GLBuffer : IBuffer {
    GLuint id = 0;
    size_t bytes = 0;
    bool host = false;
    std::vector<uint8_t> shadow; // client copy for host-visible dynamic buffers
    void* mapped() override { return host ? shadow.data() : nullptr; }
    size_t size() const override { return bytes; }
    void upload(size_t offset, size_t sz, const void* data) override {
        if (!data || sz == 0) return;
        g_gl.BindBuffer(GL_ARRAY_BUFFER, id);
        g_gl.BufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)sz, data);
        if (host) memcpy(shadow.data() + offset, data, sz);
    }
};

struct GLTexture : ITexture {
    GLuint id = 0;
    GLuint sampler = 0;
    TextureDesc d;
    const TextureDesc& desc() const override { return d; }

    void allocateStorage();
    void applySamplerParams();
    void uploadPixels(int w, int h, GLenum fmt, GLenum type, const void* px, bool srgbFlip);
    void uploadRGBA8(const void* px) override;
    void uploadRGBA16F(const void* px, uint32_t mip=0) override;
    void uploadRGBA32F(const void* px) override;
};

struct GLBindingSetLayout : IBindingSetLayout {
    std::vector<BindingDecl> decls;
};
struct GLBindingSet : IBindingSet {
    struct Slot { GLBuffer* buf = nullptr; size_t offset = 0, range = 0; GLTexture* tex = nullptr;
                  bool dynamicUbo = false; };
    std::unordered_map<uint32_t, Slot> slots;
    void setBuffer(uint32_t b, IBuffer* buf, size_t off, size_t range) override {
        auto& s = slots[b];
        s.buf = (GLBuffer*)buf; s.offset = off; s.range = range ? range : (buf ? buf->size() - off : 0);
    }
    void setTexture(uint32_t b, ITexture* t) override { slots[b].tex = (GLTexture*)t; }
};

struct GLProgram : IShaderProgram {
    GLuint id = 0;
    std::string name;
    // sampler binding assignments (for GL < 4.2 without layout(binding))
    std::vector<std::pair<GLint, GLint>> samplerUnits; // location -> unit
};

struct GLPipeline : IPipeline {
    GLProgram* prog = nullptr;
    VertexLayout layout;
    RHITopology topology = RHITopology::Triangles;
    RHICull cull = RHICull::None;
    bool depthTest = false, depthWrite = false;
    RHICompare depthFunc = RHICompare::Less;
    bool blend = false;
};

struct GLFrameBuffer : IFrameBuffer {
    GLuint fbo = 0;
    GLuint depthRbo = 0;
    uint32_t w = 0, h = 0;
    GLTexture* color0 = nullptr;
    uint32_t color0Mip = 0;
    bool cubeFace = false;
    bool arrayFace = false;
    uint32_t cubeLayer = 0;
    uint32_t width() const override { return w; }
    uint32_t height() const override { return h; }
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
class GLRenderContext;

class GLDevice : public IDevice {
public:
    bool init(const DeviceInitInfo& info) override;
    void shutdown() override;
    void waitIdle() override { g_gl.Finish(); }

    RHIBackend backend() const override { return RHIBackend::OpenGL; }
    const BackendInfo& info() const override { return binfo_; }
    bool isHeadless() const override { return headless_; }
    bool vulkanClip() const override { return false; }

    bool beginFrame() override;
    IRenderContext* context() override;
    IFrameBuffer* currentScreenFB() override { return screen_fb_; }
    ITexture* screenColorTexture() override { return screen_tex_; }
    void endFrame() override;
    void onResize(uint32_t w, uint32_t h) override;

    IBuffer* createBuffer(const BufferDesc& d, const void* data) override;
    ITexture* createTexture(const TextureDesc& d) override;
    IBindingSetLayout* createBindingSetLayout(const std::vector<BindingDecl>& d) override;
    IBindingSet* createBindingSet(IBindingSetLayout*) override;
    IShaderProgram* createShaderProgram(const std::string& name, const std::string& vs,
                                        const std::string& fs, const std::vector<uint32_t>&,
                                        const std::vector<uint32_t>&) override;
    IPipeline* createPipeline(const PipelineDesc& d) override;
    IFrameBuffer* createFrameBuffer(const FrameBufferDesc& d) override;

    void generateMipmaps(ITexture* t) override;
    void textureSamplingBarrier(ITexture* t) override;
    void copyLayerToCube(ITexture* src, ITexture* dstCube, uint32_t size,
                         uint32_t mip, uint32_t layer) override;
    void blitToSwapchain(ITexture* s) override { (void)s; }
    void immediateGraphics(const std::function<void(void*)>& fn) override { fn(nullptr); }
    RHIFormat screenFormat() const override { return RHIFormat::RGBA8; }
    std::vector<uint8_t> readTextureRGBA8(ITexture* t, uint32_t mip, int32_t layer = -1) override;
    void* nativeHandle(const char* what) override;

    // internal
    void makeCurrent(bool on);
    int majorVersion() const { return gl_major_; }
    int minorVersion() const { return gl_minor_; }

private:
    DeviceInitInfo cfg_;
    bool headless_ = false;
    SDL_Window* window_ = nullptr;
    SDL_GLContext glctx_ = nullptr;
    BackendInfo binfo_;
#if defined(GENESIS_EGL_HEADLESS)
    EGLDisplay egl_dpy_ = EGL_NO_DISPLAY;
    EGLContext egl_ctx_ = EGL_NO_CONTEXT;
    EGLSurface egl_surf_ = EGL_NO_SURFACE;
#endif
    GLFrameBuffer* screen_fb_ = nullptr;
    GLTexture* screen_tex_ = nullptr;
    uint32_t ubo_align_ = 16;
    GLRenderContext* rc_ = nullptr;
    GLuint read_fbo_ = 0;
    int gl_major_ = 4, gl_minor_ = 1;
    std::map<GLTexture*, std::pair<GLuint,GLuint>> cubeFbos_;

    GLuint compileStage(GLenum stage, const std::string& src,
                        std::unordered_map<std::string, int>* blockBinding,
                        std::vector<std::pair<std::string,int>>* samplerBinding);
    std::string preprocess(const std::string& src,
                           std::unordered_map<std::string, int>& blockBinding,
                           std::vector<std::pair<std::string,int>>& samplerBinding);
    void createScreenTargets();
};

// ---------------------------------------------------------------------------
// Render context
// ---------------------------------------------------------------------------
class GLRenderContext : public IRenderContext {
public:
    GLDevice* dev = nullptr;
    GLPipeline* pipe = nullptr;
    GLuint vbo = 0;
    bool attribsSet = false;

    void setViewport(uint32_t w, uint32_t h) override {
        g_gl.Viewport(0, 0, (GLsizei)w, (GLsizei)h);
    }
    void beginPass(IFrameBuffer* ifb, const ClearValue& c, bool cc, bool cd) override;
    void endPass() override {}
    void bindPipeline(IPipeline* p) override;
    void bindSet(IBindingSet* s, const uint32_t* dynOff, uint32_t dynCount) override;
    void bindVertexBuffer(IBuffer* b, uint32_t binding, size_t offset) override;
    void bindIndexBuffer(IBuffer* b, RHIIndexType t, size_t offset) override;
    void draw(uint32_t n, uint32_t first, uint32_t inst) override;
    void drawIndexed(uint32_t n, uint32_t first, int32_t vbase, uint32_t inst) override;

private:
    void configureAttribs();
    GLuint ibo_ = 0;
    size_t ibo_offset_ = 0;
    GLenum ibo_type_ = GL_UNSIGNED_SHORT;
    bool attribs_enabled_ = false;
};

// ---------------------------------------------------------------------------
// Texture implementation
// ---------------------------------------------------------------------------
void GLTexture::applySamplerParams() {
    GLenum minF = d.filter_min == RHIFilter::Nearest ? GL_NEAREST
                  : (d.mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    GLenum magF = d.filter_mag == RHIFilter::Nearest ? GL_NEAREST : GL_LINEAR;
    g_gl.SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, (GLint)minF);
    g_gl.SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, (GLint)magF);
    GLenum wrap = d.wrap == RHIWrap::Repeat ? GL_REPEAT
                : d.wrap == RHIWrap::ClampBorder ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;
    g_gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, (GLint)wrap);
    g_gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, (GLint)wrap);
    g_gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_R, (GLint)GL_CLAMP_TO_EDGE);
}

void GLTexture::allocateStorage() {
    bool cube = (d.usage & TU_Cube) != 0;
    bool arrayTex = d.layers > 1 && !cube;
    GLenum target = cube ? GL_TEXTURE_CUBE_MAP
                         : (arrayTex ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D);
    GLenum internal, fmt, type;
    glImageFormat(d.format, internal, fmt, type);
    GLenum depthInternal = 0;
    bool isDepth = glDepthFormat(d.format, depthInternal);

    g_gl.BindTexture(target, id);
    if (isDepth) { internal = depthInternal; fmt = GL_DEPTH_COMPONENT; type = GL_FLOAT; }
    bool array = d.layers > 1 && !cube;
    GLsizei w = (GLsizei)d.width, h = (GLsizei)d.height;
    if (array) {
        for (uint32_t m = 0; m < d.mips; ++m) {
            g_gl.TexImage3D(GL_TEXTURE_2D_ARRAY, (GLint)m, (GLint)internal, w, h, d.layers,
                           0, fmt, type, nullptr);
            w = std::max(1, w / 2); h = std::max(1, h / 2);
        }
    }
    for (uint32_t m = 0; m < d.mips; ++m) {
        if (array) break;
        if (cube) {
            for (int face = 0; face < 6; ++face)
                g_gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, (GLint)m,
                                (GLint)internal, w, h, 0, fmt, type, nullptr);
        } else {
            g_gl.TexImage2D(GL_TEXTURE_2D, (GLint)m, (GLint)internal, w, h, 0, fmt, type, nullptr);
        }
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    g_gl.TexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    g_gl.TexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)d.mips - 1);
    applySamplerParams();
}

void GLTexture::uploadPixels(int w, int h, GLenum fmt, GLenum type, const void* px, bool flip) {
    // Convert top-left origin (stb/Vulkan) to bottom-left (GL) on upload.
    if (!flip) {
        g_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, type, px);
        return;
    }
    int bpp = (type == GL_FLOAT) ? ((fmt == GL_RGBA) ? 16 : (fmt == GL_RG ? 8 : 4)) : 4;
    const uint8_t* src = (const uint8_t*)px;
    std::vector<uint8_t> row((size_t)w * bpp);
    for (int y = 0; y < h; ++y) {
        memcpy(row.data(), src + (size_t)(h - 1 - y) * w * bpp, (size_t)w * bpp);
        g_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, y, w, 1, fmt, type, row.data());
    }
}

void GLTexture::uploadRGBA8(const void* px) {
    g_gl.BindTexture(GL_TEXTURE_2D, id);
    uploadPixels((int)d.width, (int)d.height, GL_RGBA, GL_UNSIGNED_BYTE, px, false);
    if (d.mips > 1) g_gl.GenerateMipmap(GL_TEXTURE_2D);
}
void GLTexture::uploadRGBA16F(const void* pixels, uint32_t mip) {
    uint32_t w=std::max(1u,d.width>>mip),h=std::max(1u,d.height>>mip);
    int comps = d.format==RHIFormat::RG16F ? 2 : 4;
    std::vector<uint16_t> hdr((size_t)w*h*comps);
    const float* src=(const float*)pixels;
    for(size_t i=0;i<hdr.size();i++) hdr[i]=f32ToF16(src[i]);
    g_gl.BindTexture(GL_TEXTURE_2D, id);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT,1);
    g_gl.TexImage2D(GL_TEXTURE_2D,(GLint)mip, comps==2?GL_RG16F:GL_RGBA16F, w,h,0,
                    comps==2?GL_RG:GL_RGBA, GL_HALF_FLOAT, hdr.data());
}
void GLTexture::uploadRGBA32F(const void* px) {
    g_gl.BindTexture(GL_TEXTURE_2D, id);
    uploadPixels((int)d.width, (int)d.height, GL_RGBA, GL_FLOAT, px, false);
    if (d.mips > 1) g_gl.GenerateMipmap(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// GLSL preprocessing
// ---------------------------------------------------------------------------
std::string GLDevice::preprocess(const std::string& src,
                                 std::unordered_map<std::string, int>& blockBinding,
                                 std::vector<std::pair<std::string,int>>& samplerBinding) {
    // Split #version line.
    size_t nl = src.find('\n');
    std::string first = src.substr(0, nl);
    std::string body = nl == std::string::npos ? "" : src.substr(nl + 1);

    // strip only the `set = N` token, preserve `binding = N`
    body = std::regex_replace(body, std::regex(R"(\s*set\s*=\s*[0-9]+\s*,?)"), "");

    std::string ver = first;
    if (gl_major_ * 10 + gl_minor_ < 42) {
        // rewrite to 410 and harvest binding assignments
        ver = "#version 410 core";
        // uniform blocks: layout(binding=N) uniform Name {
        std::regex blockRe(R"(layout\s*\(\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+(\w+))");
        for (auto it = std::sregex_iterator(body.begin(), body.end(), blockRe);
             it != std::sregex_iterator(); ++it) {
            blockBinding[(*it)[2].str()] = std::atoi((*it)[1].str().c_str());
        }
        // samplers (possibly comma lists - keep first name capture simple)
        std::regex sampRe(R"(layout\s*\(\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+\w+\s+(\w+))");
        for (auto it = std::sregex_iterator(body.begin(), body.end(), sampRe);
             it != std::sregex_iterator(); ++it) {
            samplerBinding.push_back({(*it)[2].str(), std::atoi((*it)[1].str().c_str())});
        }
        body = std::regex_replace(body, std::regex(R"(\s*layout\s*\(\s*binding\s*=\s*\d+\s*\))"), "");
    }
    return ver + "\n" + body;
}

GLuint GLDevice::compileStage(GLenum stage, const std::string& sourceIn,
                              std::unordered_map<std::string, int>* blockBinding,
                              std::vector<std::pair<std::string,int>>* samplerBinding) {
    std::unordered_map<std::string, int> bb;
    std::vector<std::pair<std::string,int>> sb;
    std::string src = preprocess(sourceIn, bb, sb);
    if (blockBinding) for (auto& kv : bb) (*blockBinding)[kv.first] = kv.second;
    if (samplerBinding) samplerBinding->insert(samplerBinding->end(), sb.begin(), sb.end());

    GLuint shader = g_gl.CreateShader(stage);
    const char* p = src.c_str();
    g_gl.ShaderSource(shader, 1, &p, nullptr);
    g_gl.CompileShader(shader);
    GLint ok = 0;
    g_gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        g_gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        GenesisPrint("[GL] shader compile failed:\n%s\n--- source ---\n%s\n", log, src.c_str());
        return 0;
    }
    return shader;
}

// ---------------------------------------------------------------------------
// Init / context
// ---------------------------------------------------------------------------
void GLDevice::makeCurrent(bool) {}

bool GLDevice::init(const DeviceInitInfo& info) {
    cfg_ = info;
    headless_ = info.headless;
    window_ = (SDL_Window*)info.window_handle;

    auto loadFromSDL = [](const char* name) -> void* {
        return (void*)SDL_GL_GetProcAddress(name);
    };

    if (headless_) {
#if defined(GENESIS_EGL_HEADLESS)
        egl_dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj = 0, min = 0;
        if (!eglInitialize(egl_dpy_, &maj, &min)) { GenesisPrint("[GL] eglInitialize failed\n"); return false; }
        eglBindAPI(EGL_OPENGL_API);
        EGLint cfgAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                             EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
        EGLConfig cfg; EGLint n = 0;
        eglChooseConfig(egl_dpy_, cfgAttrs, &cfg, 1, &n);
        EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 4,
                             EGL_CONTEXT_MINOR_VERSION, 3,
                             EGL_CONTEXT_OPENGL_PROFILE_MASK,
                             EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        egl_ctx_ = eglCreateContext(egl_dpy_, cfg, EGL_NO_CONTEXT, ctxAttrs);
        EGLint pbAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        egl_surf_ = eglCreatePbufferSurface(egl_dpy_, cfg, pbAttrs);
        if (!eglMakeCurrent(egl_dpy_, egl_surf_, egl_surf_, egl_ctx_)) {
            GenesisPrint("[GL] eglMakeCurrent failed 0x%x\n", eglGetError()); return false;
        }
        loadGLProcs([](const char* n) -> void* { return (void*)eglGetProcAddress(n); });
#else
        GenesisPrint("[GL] headless requires EGL (Linux)\n");
        return false;
#endif
    } else {
        glctx_ = SDL_GL_CreateContext(window_);
        if (!glctx_) { GenesisPrint("[GL] SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return false; }
        SDL_GL_MakeCurrent(window_, glctx_);
        if (info.vsync) SDL_GL_SetSwapInterval(1);
        loadGLProcs(loadFromSDL);
    }

    const GLubyte* verstr = g_gl.GetString(GL_VERSION);
    int mj = 4, mn = 1;
    if (verstr) sscanf((const char*)verstr, "%d.%d", &mj, &mn);
    gl_major_ = mj; gl_minor_ = mn;
    GenesisPrint("[GL] context: %s\n", verstr ? (const char*)verstr : "?");

    GLint align = 16;
    g_gl.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    ubo_align_ = (uint32_t)align;

    binfo_.backend = RHIBackend::OpenGL;
    binfo_.name = "OpenGL";
    binfo_.device = (const char*)g_gl.GetString(GL_RENDERER);
    binfo_.version = (const char*)(verstr ? verstr : (const GLubyte*)"");

    // single shared VAO (classic attrib pointers are configured at draw time)
    GLuint vao = 0;
    g_gl.GenVertexArrays(1, &vao);
    g_gl.BindVertexArray(vao);

    g_gl.Disable(GL_CULL_FACE);

    rc_ = new GLRenderContext();
    rc_->dev = this;

    g_gl.GenFramebuffers(1, &read_fbo_);
    createScreenTargets();
    return true;
}

void GLDevice::createScreenTargets() {
    if (!headless_) {
        // default framebuffer
        auto* fb = new GLFrameBuffer();
        fb->fbo = 0; fb->w = cfg_.width; fb->h = cfg_.height;
        screen_fb_ = fb;
        screen_tex_ = nullptr;
        return;
    }
    // offscreen LDR target + depth renderbuffer
    TextureDesc td;
    td.width = cfg_.width; td.height = cfg_.height;
    td.format = RHIFormat::RGBA8;
    td.usage = TU_Sampled | TU_RenderTarget;
    td.filter_min = td.filter_mag = RHIFilter::Nearest;
    td.wrap = RHIWrap::ClampEdge;
    auto* tex = (GLTexture*)createTexture(td);
    screen_tex_ = tex;

    auto* fb = new GLFrameBuffer();
    fb->w = cfg_.width; fb->h = cfg_.height; fb->color0 = tex;
    g_gl.GenFramebuffers(1, &fb->fbo);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->id, 0);
    g_gl.GenRenderbuffers(1, &fb->depthRbo);
    g_gl.BindRenderbuffer(GL_RENDERBUFFER, fb->depthRbo);
    g_gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cfg_.width, cfg_.height);
    g_gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthRbo);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    screen_fb_ = fb;
}

void GLDevice::onResize(uint32_t w, uint32_t h) {
    cfg_.width = w; cfg_.height = h;
    if (screen_fb_) { screen_fb_->w = w; screen_fb_->h = h; }
}

bool GLDevice::beginFrame() {
    if (!headless_) SDL_GL_MakeCurrent(window_, glctx_);
    return true;
}
IRenderContext* GLDevice::context() { return rc_; }

void GLDevice::endFrame() {
    if (!headless_) SDL_GL_SwapWindow(window_);
    else g_gl.Flush();
}

void GLDevice::shutdown() {
    if (glctx_) { SDL_GL_DeleteContext(glctx_); glctx_ = nullptr; }
#if defined(GENESIS_EGL_HEADLESS)
    if (egl_dpy_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surf_) eglDestroySurface(egl_dpy_, egl_surf_);
        if (egl_ctx_) eglDestroyContext(egl_dpy_, egl_ctx_);
        eglTerminate(egl_dpy_);
    }
#endif
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------
IBuffer* GLDevice::createBuffer(const BufferDesc& d, const void* data) {
    auto* b = new GLBuffer();
    b->bytes = d.size; b->host = d.host_visible;
    GLenum usage = 0;
    if (d.usage & BU_Vertex) usage |= GL_ARRAY_BUFFER;
    if (d.usage & BU_Index) usage |= GL_ELEMENT_ARRAY_BUFFER;
    if (d.usage & BU_Uniform) usage |= GL_UNIFORM_BUFFER;
    if (!usage) usage = GL_ARRAY_BUFFER;
    g_gl.GenBuffers(1, &b->id);
    g_gl.BindBuffer(usage, b->id);
    g_gl.BufferData(usage, (GLsizeiptr)d.size, nullptr,
                    d.host_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    if (data) g_gl.BufferSubData(usage, 0, (GLsizeiptr)d.size, data);
    if (d.host_visible) {
        b->shadow.resize(d.size);
        if (data) memcpy(b->shadow.data(), data, d.size);
    }
    return b;
}

ITexture* GLDevice::createTexture(const TextureDesc& d) {
    auto* t = new GLTexture();
    t->d = d;
    g_gl.GenTextures(1, &t->id);
    g_gl.GenSamplers(1, &t->sampler);
    t->allocateStorage();
    return t;
}

IBindingSetLayout* GLDevice::createBindingSetLayout(const std::vector<BindingDecl>& d) {
    auto* l = new GLBindingSetLayout(); l->decls = d; return l;
}
IBindingSet* GLDevice::createBindingSet(IBindingSetLayout* l) {
    auto* s = new GLBindingSet();
    if (l) for (auto& d : ((GLBindingSetLayout*)l)->decls)
        if (d.type == BindingType::DynamicUBO) s->slots[d.binding].dynamicUbo = true;
    return s;
}

IShaderProgram* GLDevice::createShaderProgram(const std::string& name, const std::string& vsSrc,
                                              const std::string& fsSrc,
                                              const std::vector<uint32_t>&,
                                              const std::vector<uint32_t>&) {
    std::unordered_map<std::string, int> bb;
    std::vector<std::pair<std::string,int>> sb;
    GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc, &bb, &sb);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc, &bb, &sb);
    if (!vs || !fs) return nullptr;
    GLuint prog = g_gl.CreateProgram();
    g_gl.AttachShader(prog, vs); g_gl.AttachShader(prog, fs);
    g_gl.LinkProgram(prog);
    GLint ok = 0; g_gl.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; g_gl.GetProgramInfoLog(prog, sizeof(log), nullptr, log);
        GenesisPrint("[GL] link failed for %s:\n%s\n", name.c_str(), log);
        return nullptr;
    }
    g_gl.DeleteShader(vs); g_gl.DeleteShader(fs);

    auto* p = new GLProgram(); p->id = prog; p->name = name;
    // explicit block bindings (also harmless on 4.2+)
    for (auto& kv : bb) {
        GLuint idx = g_gl.GetUniformBlockIndex(prog, kv.first.c_str());
        if (idx != GL_INVALID_INDEX) g_gl.UniformBlockBinding(prog, idx, kv.second);
    }
    g_gl.UseProgram(prog);
    for (auto& s : sb) {
        GLint loc = g_gl.GetUniformLocation(prog, s.first.c_str());
        if (loc >= 0) { g_gl.Uniform1i(loc, s.second); p->samplerUnits.push_back({loc, s.second}); }
    }
    return p;
}

static GLenum glCompare(RHICompare c) {
    switch (c) {
    case RHICompare::Never: return GL_NEVER;
    case RHICompare::Less: return GL_LESS;
    case RHICompare::LEqual: return GL_LEQUAL;
    case RHICompare::Greater: return GL_GREATER;
    case RHICompare::GEqual: return GL_GEQUAL;
    case RHICompare::Equal: return GL_EQUAL;
    default: return GL_ALWAYS;
    }
}

IPipeline* GLDevice::createPipeline(const PipelineDesc& d) {
    auto* p = new GLPipeline();
    p->prog = (GLProgram*)d.program;
    p->layout = d.vertex_layout;
    p->topology = d.topology; p->cull = d.cull;
    p->depthTest = d.depth_test; p->depthWrite = d.depth_write;
    p->depthFunc = d.depth_func; p->blend = d.blend_alpha;
    return p;
}

IFrameBuffer* GLDevice::createFrameBuffer(const FrameBufferDesc& d) {
    auto* fb = new GLFrameBuffer();
    fb->w = d.width; fb->h = d.height;
    auto* t0 = (GLTexture*)d.colors[0].texture;
    fb->color0 = t0; fb->color0Mip = d.colors[0].mip;
    bool cube = (t0->d.usage & TU_Cube) != 0;
    bool layered = (!cube && t0->d.layers > 1);
    fb->cubeFace = cube; fb->arrayFace = layered; fb->cubeLayer = d.colors[0].layer;
    // One GL framebuffer per color texture; the exact mip/layer is rebound
    // in beginPass (llvmpipe renders only the first attachment otherwise).
    auto it = cubeFbos_.find(t0);
    GLuint fbo, rbo;
    if (it == cubeFbos_.end()) {
        g_gl.GenFramebuffers(1, &fbo);
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g_gl.GenRenderbuffers(1, &rbo);
        g_gl.BindRenderbuffer(GL_RENDERBUFFER, rbo);
        g_gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, d.width, d.height);
        g_gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        g_gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        cubeFbos_[t0] = {fbo, rbo};
    } else {
        fbo = it->second.first; rbo = it->second.second;
    }
    fb->fbo = fbo; fb->depthRbo = rbo;
    return fb;
}

void GLDevice::generateMipmaps(ITexture* it) {
    auto* t = (GLTexture*)it;
    bool cube = (t->d.usage & TU_Cube) != 0;
    g_gl.BindTexture(cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, t->id);
    g_gl.GenerateMipmap(cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
}

void GLDevice::textureSamplingBarrier(ITexture*) {
    // FBO render -> texture fetch ordering within a frame.
    g_gl.Finish();
}

void GLDevice::copyLayerToCube(ITexture* isrc, ITexture* idst, uint32_t size,
                               uint32_t mip, uint32_t layer) {
    // glCopyImageSubData requires GL 4.3 (unavailable on macOS 4.1).
    // IBL is computed on CPU in that case, so this is simply a no-op.
    if (!g_gl.CopyImageSubData) return;
    auto* src = (GLTexture*)isrc; auto* dst = (GLTexture*)idst;
    g_gl.CopyImageSubData(src->id, GL_TEXTURE_2D, mip, 0, 0, 0,
                          dst->id, GL_TEXTURE_CUBE_MAP, mip, 0, 0, layer,
                          size, size, 1);
    if (GLenum e = g_gl.GetError())
        GenesisPrint("[GL] copy subdata err 0x%x layer=%u size=%u\n", e, layer, size);
}

std::vector<uint8_t> GLDevice::readTextureRGBA8(ITexture* it, uint32_t mip, int32_t layer) {
    auto* t = (GLTexture*)it;
    uint32_t w = std::max(1u, t->d.width >> mip);
    uint32_t h = std::max(1u, t->d.height >> mip);
    std::vector<uint8_t> px((size_t)w * h * 4);
    bool cube = (t->d.usage & TU_Cube) != 0;
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, read_fbo_);
    if (cube) {
        GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (layer >= 0 ? layer : 0);
        g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, face, t->id, (GLint)mip);
    } else if (t->d.layers > 1) {
        g_gl.FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t->id, (GLint)mip,
                                     layer >= 0 ? layer : 0);
    } else {
        g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, (GLint)mip);
    }
    g_gl.Viewport(0, 0, w, h);
    g_gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    // GL gives bottom-first; flip to top-left to match the Vulkan readback.
    std::vector<uint8_t> out(px.size());
    for (uint32_t y = 0; y < h; ++y)
        memcpy(out.data() + (size_t)y * w * 4, px.data() + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    return out;
}

void* GLDevice::nativeHandle(const char* what) {
    if (strcmp(what, "ubo_alignment") == 0) return (void*)(uintptr_t)ubo_align_;
    if (strcmp(what, "sdl_window") == 0) return window_;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Context commands
// ---------------------------------------------------------------------------
static GLenum glTopology(RHITopology t) {
    switch (t) {
    case RHITopology::Lines: return GL_LINES;
    case RHITopology::TriangleStrip: return GL_TRIANGLE_STRIP;
    default: return GL_TRIANGLES;
    }
}

void GLRenderContext::beginPass(IFrameBuffer* ifb, const ClearValue& c, bool cc, bool cd) {
    auto* fb = (GLFrameBuffer*)ifb;
    if (fb && fb->color0) {
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
        if (fb->cubeFace)
            g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + fb->cubeLayer, fb->color0->id, fb->color0Mip);
        else if (fb->arrayFace)
            g_gl.FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                fb->color0->id, fb->color0Mip, fb->cubeLayer);
        else
            g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, fb->color0->id, fb->color0Mip);
        g_gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, fb ? fb->fbo : 0);
    g_gl.Viewport(0, 0, fb ? (GLsizei)fb->w : 1, fb ? (GLsizei)fb->h : 1);
    if (cc) g_gl.ClearColor(c.color[0], c.color[1], c.color[2], c.color[3]);
    if (cd) g_gl.ClearDepth((GLdouble)c.depth);
    GLbitfield bits = 0;
    if (cc) bits |= GL_COLOR_BUFFER_BIT;
    if (cd) bits |= GL_DEPTH_BUFFER_BIT;
    if (bits) g_gl.Clear(bits);
}

void GLRenderContext::bindPipeline(IPipeline* pp) {
    pipe = (GLPipeline*)pp;
    g_gl.UseProgram(pipe->prog->id);
    for (auto& su : pipe->prog->samplerUnits) g_gl.Uniform1i(su.first, su.second);

    if (pipe->cull == RHICull::None) g_gl.Disable(GL_CULL_FACE);
    else {
        g_gl.Enable(GL_CULL_FACE);
        g_gl.CullFace(pipe->cull == RHICull::Front ? GL_FRONT : GL_BACK);
        g_gl.FrontFace(GL_CCW);
    }
    if (pipe->depthTest) {
        g_gl.Enable(GL_DEPTH_TEST);
        g_gl.DepthFunc(glCompare(pipe->depthFunc));
    } else g_gl.Disable(GL_DEPTH_TEST);
    g_gl.DepthMask(pipe->depthWrite ? GL_TRUE : GL_FALSE);

    if (pipe->blend) {
        g_gl.Enable(GL_BLEND);
        g_gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else g_gl.Disable(GL_BLEND);
    attribsSet = false;
}

void GLRenderContext::configureAttribs() {
    for (uint32_t i = 0; i < 16; ++i) g_gl.DisableVertexAttribArray(i);
    for (auto& a : pipe->layout.attribs) {
        g_gl.EnableVertexAttribArray(a.location);
        GLint comps = a.type == RHIAttrib::Float ? 1 : a.type == RHIAttrib::Float2 ? 2
                   : a.type == RHIAttrib::Float3 ? 3 : 4;
        g_gl.VertexAttribPointer(a.location, comps, GL_FLOAT, GL_FALSE,
                                 (GLsizei)pipe->layout.stride, (void*)(uintptr_t)a.offset);
    }
    attribsSet = true;
}

void GLRenderContext::bindVertexBuffer(IBuffer* b, uint32_t, size_t) {
    vbo = ((GLBuffer*)b)->id;
    g_gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    attribsSet = false;
    if (pipe && !attribsSet) configureAttribs();
}

void GLRenderContext::bindIndexBuffer(IBuffer* b, RHIIndexType t, size_t offset) {
    ibo_ = ((GLBuffer*)b)->id;
    ibo_type_ = t == RHIIndexType::UInt32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    ibo_offset_ = offset;
    g_gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
}

void GLRenderContext::bindSet(IBindingSet* iset, const uint32_t* dynOff, uint32_t dynCount) {
    auto* set = (GLBindingSet*)iset;
    // Buffers first, in ascending binding order so dynamic offsets line up
    // with descriptor declaration order (unordered_map iteration is random).
    std::vector<uint32_t> bufBindings;
    for (auto& kv : set->slots) if (kv.second.buf) bufBindings.push_back(kv.first);
    std::sort(bufBindings.begin(), bufBindings.end());
    uint32_t dyn = 0;
    for (uint32_t binding : bufBindings) {
        auto& s = set->slots[binding];
        size_t off = s.offset, range = s.range;
        if (s.dynamicUbo && dyn < dynCount) { off = dynOff[dyn++]; range = s.range; }
        if (!range) range = s.buf->bytes - off;
        if (s.buf->host) {
            g_gl.BindBuffer(GL_UNIFORM_BUFFER, s.buf->id);
            g_gl.BufferSubData(GL_UNIFORM_BUFFER, (GLintptr)off, (GLsizeiptr)range,
                               s.buf->shadow.data() + off);
        }
        g_gl.BindBufferRange(GL_UNIFORM_BUFFER, binding, s.buf->id,
                             (GLintptr)off, (GLsizeiptr)range);
    }
    for (auto& kv : set->slots) {
        if (!kv.second.tex) continue;
        auto* t = kv.second.tex;
        bool cubeT = (t->d.usage & TU_Cube) != 0;
        GLenum texTgt = cubeT ? GL_TEXTURE_CUBE_MAP
                              : (t->d.layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D);
        g_gl.ActiveTexture(GL_TEXTURE0 + kv.first);
        g_gl.BindTexture(texTgt, t->id);
        g_gl.BindSampler(kv.first, 0);
    }
}

void GLRenderContext::draw(uint32_t n, uint32_t first, uint32_t inst) {
    if (pipe && !attribsSet) configureAttribs();
    if (inst <= 1) g_gl.DrawArrays(glTopology(pipe->topology), (GLint)first, (GLsizei)n);
    else g_gl.DrawArraysInstanced(glTopology(pipe->topology), (GLint)first, (GLsizei)n, (GLsizei)inst);
}
void GLRenderContext::drawIndexed(uint32_t n, uint32_t first, int32_t, uint32_t inst) {
    if (pipe && !attribsSet) configureAttribs();
    GLenum preDraw = g_gl.GetError();
    GLenum topo = glTopology(pipe->topology);
    void* idx = (void*)(uintptr_t)(first * (ibo_type_ == GL_UNSIGNED_INT ? 4 : 2) + ibo_offset_);
    if (inst <= 1) g_gl.DrawElements(topo, (GLsizei)n, ibo_type_, idx);
    else g_gl.DrawElementsInstanced(topo, (GLsizei)n, ibo_type_, idx, (GLsizei)inst);
    GLenum e = g_gl.GetError();
    if (e) GenesisPrint("[GL] draw err 0x%x n=%u pre=0x%x va=%d\n", e, n, preDraw, attribsSet);
}

} // namespace Genesis

Genesis::IDevice* Genesis::createOpenGLDevice() { return new Genesis::GLDevice(); }
