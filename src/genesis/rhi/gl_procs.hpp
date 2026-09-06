/*
 * Minimal OpenGL 3.3/4.x loader for Genesis.
 * Core 1.1 symbols come from libGL; everything newer is resolved at runtime
 * from the active context (SDL_GL_GetProcAddress / eglGetProcAddress).
 * Keeping our own loader avoids a glad/glew dependency and works on macOS 4.1.
 */
#pragma once

// Platform OpenGL headers.
//   macOS : GL 4.1 core via <OpenGL/gl3.h>; GL functions are exported
//           directly by the framework (no GetProcAddress needed), and the
//           PFNGL*PROC pointer typedefs are missing from Apple's headers,
//           so we derive them with decltype.
//   Linux/Windows: standard <GL/gl.h> + <GL/glext.h> PFN typedefs.
#if defined(__APPLE__)
  #include <OpenGL/gl3.h>
  #define GENESIS_GL_APPLE 1

  // GL 4.2/4.3 entry points do not exist on the 4.1 framework. Declare
  // their pointer types ourselves; they stay null after loading.
  typedef void (*PFNGLBINDIMAGETEXTUREPROC)(GLuint unit, GLint level, GLboolean layered,
                                            GLint layer, GLenum access, GLenum format);
  typedef void (*GLDEBUGPROC)(GLenum, GLenum, GLuint, GLenum, GLsizei,
                              const GLchar*, const void*);
  typedef void (*PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback, const void* userParam);
  typedef void (*PFNGLCOPYIMAGESUBDATAPROC)(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                                            GLint srcX, GLint srcY, GLint srcZ,
                                            GLuint dstName, GLenum dstTarget, GLint dstLevel,
                                            GLint dstX, GLint dstY, GLint dstZ,
                                            GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);

  // Every GL function we use (core up to 4.1) is declared in gl3.h.
  // Derive the matching PFN pointer typedef from the framework symbol.
  #define GENESIS_GLPFN(u, name) using PFNGL##u##PROC = decltype(&gl##name);
  GENESIS_GLPFN(CREATESHADER, CreateShader)
  GENESIS_GLPFN(SHADERSOURCE, ShaderSource)
  GENESIS_GLPFN(COMPILESHADER, CompileShader)
  GENESIS_GLPFN(GETSHADERIV, GetShaderiv)
  GENESIS_GLPFN(GETSHADERINFOLOG, GetShaderInfoLog)
  GENESIS_GLPFN(CREATEPROGRAM, CreateProgram)
  GENESIS_GLPFN(ATTACHSHADER, AttachShader)
  GENESIS_GLPFN(LINKPROGRAM, LinkProgram)
  GENESIS_GLPFN(GETPROGRAMIV, GetProgramiv)
  GENESIS_GLPFN(GETPROGRAMINFOLOG, GetProgramInfoLog)
  GENESIS_GLPFN(USEPROGRAM, UseProgram)
  GENESIS_GLPFN(DELETESHADER, DeleteShader)
  GENESIS_GLPFN(DELETEPROGRAM, DeleteProgram)
  GENESIS_GLPFN(GETUNIFORMLOCATION, GetUniformLocation)
  GENESIS_GLPFN(UNIFORM1I, Uniform1i)
  GENESIS_GLPFN(UNIFORM1F, Uniform1f)
  GENESIS_GLPFN(UNIFORM3F, Uniform3f)
  GENESIS_GLPFN(GETACTIVEUNIFORM, GetActiveUniform)
  GENESIS_GLPFN(GETACTIVEUNIFORMNAME, GetActiveUniformName)
  GENESIS_GLPFN(GETACTIVEUNIFORMBLOCKIV, GetActiveUniformBlockiv)
  GENESIS_GLPFN(GETUNIFORMBLOCKINDEX, GetUniformBlockIndex)
  GENESIS_GLPFN(UNIFORMBLOCKBINDING, UniformBlockBinding)
  GENESIS_GLPFN(CULLFACE, CullFace)
  GENESIS_GLPFN(GENBUFFERS, GenBuffers)
  GENESIS_GLPFN(BINDBUFFER, BindBuffer)
  GENESIS_GLPFN(BUFFERDATA, BufferData)
  GENESIS_GLPFN(BUFFERSUBDATA, BufferSubData)
  GENESIS_GLPFN(DELETEBUFFERS, DeleteBuffers)
  GENESIS_GLPFN(BINDBUFFERBASE, BindBufferBase)
  GENESIS_GLPFN(BINDBUFFERRANGE, BindBufferRange)
  GENESIS_GLPFN(MAPBUFFERRANGE, MapBufferRange)
  GENESIS_GLPFN(UNMAPBUFFER, UnmapBuffer)
  GENESIS_GLPFN(GENVERTEXARRAYS, GenVertexArrays)
  GENESIS_GLPFN(BINDVERTEXARRAY, BindVertexArray)
  GENESIS_GLPFN(DELETEVERTEXARRAYS, DeleteVertexArrays)
  GENESIS_GLPFN(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray)
  GENESIS_GLPFN(DISABLEVERTEXATTRIBARRAY, DisableVertexAttribArray)
  GENESIS_GLPFN(VERTEXATTRIBPOINTER, VertexAttribPointer)
  GENESIS_GLPFN(VERTEXATTRIBDIVISOR, VertexAttribDivisor)
  GENESIS_GLPFN(GENTEXTURES, GenTextures)
  GENESIS_GLPFN(DELETETEXTURES, DeleteTextures)
  GENESIS_GLPFN(ACTIVETEXTURE, ActiveTexture)
  GENESIS_GLPFN(BINDTEXTURE, BindTexture)
  GENESIS_GLPFN(TEXPARAMETERI, TexParameteri)
  GENESIS_GLPFN(TEXPARAMETERF, TexParameterf)
  GENESIS_GLPFN(TEXIMAGE2D, TexImage2D)
  GENESIS_GLPFN(TEXIMAGE3D, TexImage3D)
  GENESIS_GLPFN(TEXSUBIMAGE2D, TexSubImage2D)
  GENESIS_GLPFN(TEXPARAMETERIIV, TexParameterIiv)
  GENESIS_GLPFN(GENERATEMIPMAP, GenerateMipmap)
  GENESIS_GLPFN(GENSAMPLERS, GenSamplers)
  GENESIS_GLPFN(DELETESAMPLERS, DeleteSamplers)
  GENESIS_GLPFN(BINDSAMPLER, BindSampler)
  GENESIS_GLPFN(SAMPLERPARAMETERI, SamplerParameteri)
  GENESIS_GLPFN(SAMPLERPARAMETERF, SamplerParameterf)
  GENESIS_GLPFN(SAMPLERPARAMETERIIV, SamplerParameterIiv)
  GENESIS_GLPFN(GETTEXIMAGE, GetTexImage)
  GENESIS_GLPFN(PIXELSTOREI, PixelStorei)
  GENESIS_GLPFN(GENFRAMEBUFFERS, GenFramebuffers)
  GENESIS_GLPFN(BINDFRAMEBUFFER, BindFramebuffer)
  GENESIS_GLPFN(DELETEFRAMEBUFFERS, DeleteFramebuffers)
  GENESIS_GLPFN(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D)
  GENESIS_GLPFN(FRAMEBUFFERTEXTURELAYER, FramebufferTextureLayer)
  GENESIS_GLPFN(GENRENDERBUFFERS, GenRenderbuffers)
  GENESIS_GLPFN(DELETERENDERBUFFERS, DeleteRenderbuffers)
  GENESIS_GLPFN(BINDRENDERBUFFER, BindRenderbuffer)
  GENESIS_GLPFN(RENDERBUFFERSTORAGE, RenderbufferStorage)
  GENESIS_GLPFN(FRAMEBUFFERRENDERBUFFER, FramebufferRenderbuffer)
  GENESIS_GLPFN(CHECKFRAMEBUFFERSTATUS, CheckFramebufferStatus)
  GENESIS_GLPFN(GETFRAMEBUFFERATTACHMENTPARAMETERIV, GetFramebufferAttachmentParameteriv)
  GENESIS_GLPFN(DRAWBUFFER, DrawBuffer)
  GENESIS_GLPFN(VIEWPORT, Viewport)
  GENESIS_GLPFN(CLEARCOLOR, ClearColor)
  GENESIS_GLPFN(CLEARDEPTH, ClearDepth)
  GENESIS_GLPFN(CLEAR, Clear)
  GENESIS_GLPFN(GETERROR, GetError)
  GENESIS_GLPFN(ENABLE, Enable)
  GENESIS_GLPFN(DISABLE, Disable)
  GENESIS_GLPFN(DEPTHFUNC, DepthFunc)
  GENESIS_GLPFN(DEPTHMASK, DepthMask)
  GENESIS_GLPFN(FRONTFACE, FrontFace)
  GENESIS_GLPFN(BLENDFUNC, BlendFunc)
  GENESIS_GLPFN(BLENDEQUATION, BlendEquation)
  GENESIS_GLPFN(COLORMASK, ColorMask)
  GENESIS_GLPFN(DRAWARRAYS, DrawArrays)
  GENESIS_GLPFN(DRAWELEMENTS, DrawElements)
  GENESIS_GLPFN(DRAWARRAYSINSTANCED, DrawArraysInstanced)
  GENESIS_GLPFN(DRAWELEMENTSINSTANCED, DrawElementsInstanced)
  GENESIS_GLPFN(READPIXELS, ReadPixels)
  GENESIS_GLPFN(FINISH, Finish)
  GENESIS_GLPFN(FLUSH, Flush)
  GENESIS_GLPFN(GETINTEGERV, GetIntegerv)
  GENESIS_GLPFN(GETSTRING, GetString)
  GENESIS_GLPFN(BLITFRAMEBUFFER, BlitFramebuffer)
  #undef GENESIS_GLPFN
#else
  #include <GL/gl.h>
  #include <GL/glext.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

namespace Genesis {

// Function pointer table, populated by loadGLProcs() after a context is live.
struct GLProcs {
    // shaders / programs
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLGETACTIVEUNIFORMPROC GetActiveUniform;
    PFNGLGETACTIVEUNIFORMNAMEPROC GetActiveUniformName;
    PFNGLGETACTIVEUNIFORMBLOCKIVPROC GetActiveUniformBlockiv;
    PFNGLGETUNIFORMBLOCKINDEXPROC GetUniformBlockIndex;
    PFNGLUNIFORMBLOCKBINDINGPROC UniformBlockBinding;
    PFNGLBINDIMAGETEXTUREPROC BindImageTexture;
    // buffers
    void (*CullFace)(GLenum);
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLBUFFERSUBDATAPROC BufferSubData;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLBINDBUFFERBASEPROC BindBufferBase;
    PFNGLBINDBUFFERRANGEPROC BindBufferRange;
    PFNGLMAPBUFFERRANGEPROC MapBufferRange;
    PFNGLUNMAPBUFFERPROC UnmapBuffer;
    // vao
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLVERTEXATTRIBDIVISORPROC VertexAttribDivisor;
    // textures
    void (*GenTextures)(GLsizei, GLuint*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexParameterf)(GLenum, GLenum, GLfloat);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    PFNGLTEXPARAMETERIIVPROC TexParameterIiv;
    PFNGLGENERATEMIPMAPPROC GenerateMipmap;
    PFNGLGENSAMPLERSPROC GenSamplers;
    PFNGLDELETESAMPLERSPROC DeleteSamplers;
    PFNGLBINDSAMPLERPROC BindSampler;
    PFNGLSAMPLERPARAMETERIPROC SamplerParameteri;
    PFNGLSAMPLERPARAMETERFPROC SamplerParameterf;
    PFNGLSAMPLERPARAMETERIIVPROC SamplerParameterIiv;
    void (*GetTexImage)(GLenum, GLint, GLenum, GLenum, void*);
    void (*PixelStorei)(GLenum, GLint);
    // fbo
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLFRAMEBUFFERTEXTURELAYERPROC FramebufferTextureLayer;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC GetFramebufferAttachmentParameteriv;
    void (*DrawBuffer)(GLenum);
    // state
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*ClearDepth)(GLdouble);
    void (*Clear)(GLbitfield);
    GLenum (*GetError)(void);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*DepthFunc)(GLenum);
    void (*DepthMask)(GLboolean);
    void (*FrontFace)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);
    PFNGLBLENDEQUATIONPROC BlendEquation;
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    // draw
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
    PFNGLDRAWARRAYSINSTANCEDPROC DrawArraysInstanced;
    PFNGLDRAWELEMENTSINSTANCEDPROC DrawElementsInstanced;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*Finish)(void);
    void (*Flush)(void);
    void (*GetIntegerv)(GLenum, GLint*);
    const GLubyte* (*GetString)(GLenum);
    PFNGLDEBUGMESSAGECALLBACKPROC DebugMessageCallback;
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer;
    PFNGLCOPYIMAGESUBDATAPROC CopyImageSubData;
};

extern GLProcs g_gl;

using GLLoaderFn = void* (*)(const char*);
void loadGLProcs(GLLoaderFn loader);

} // namespace Genesis
