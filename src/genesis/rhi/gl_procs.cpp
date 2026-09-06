/* GL function pointer table implementation. */
#include "rhi/gl_procs.hpp"

namespace Genesis {

GLProcs g_gl;

#if defined(__APPLE__)
// OpenGL 4.1 functions are exported directly by the framework.
#define RESOLVE(name) g_gl.name = gl##name
void loadGLProcs(GLLoaderFn) {
#else
#define RESOLVE(name) g_gl.name = (decltype(g_gl.name))loader("gl" #name)
void loadGLProcs(GLLoaderFn loader) {
#endif
    RESOLVE(CreateShader); RESOLVE(ShaderSource); RESOLVE(CompileShader);
    RESOLVE(GetShaderiv); RESOLVE(GetShaderInfoLog); RESOLVE(CreateProgram);
    RESOLVE(AttachShader); RESOLVE(LinkProgram); RESOLVE(GetProgramiv);
    RESOLVE(GetProgramInfoLog); RESOLVE(UseProgram); RESOLVE(DeleteShader);
    RESOLVE(DeleteProgram); RESOLVE(GetUniformLocation); RESOLVE(Uniform1i);
    RESOLVE(Uniform1f); RESOLVE(Uniform3f); RESOLVE(GetActiveUniform);
    RESOLVE(GetActiveUniformName); RESOLVE(GetActiveUniformBlockiv);
    RESOLVE(GetUniformBlockIndex); RESOLVE(UniformBlockBinding);
    // BindImageTexture (GL 4.2) is unavailable on macOS 4.1 -> stays null.
    RESOLVE(CullFace); RESOLVE(GenBuffers); RESOLVE(BindBuffer);
    RESOLVE(BufferData); RESOLVE(BufferSubData); RESOLVE(DeleteBuffers);
    RESOLVE(BindBufferBase); RESOLVE(BindBufferRange); RESOLVE(MapBufferRange);
    RESOLVE(UnmapBuffer);
    RESOLVE(GenVertexArrays); RESOLVE(BindVertexArray); RESOLVE(DeleteVertexArrays);
    RESOLVE(EnableVertexAttribArray); RESOLVE(DisableVertexAttribArray); RESOLVE(VertexAttribPointer);
    RESOLVE(VertexAttribDivisor);
    RESOLVE(GenTextures); RESOLVE(DeleteTextures); RESOLVE(ActiveTexture); RESOLVE(BindTexture);
    RESOLVE(TexParameteri); RESOLVE(TexParameterf); RESOLVE(TexImage2D); RESOLVE(TexImage3D);
    RESOLVE(TexSubImage2D); RESOLVE(TexParameterIiv); RESOLVE(GenerateMipmap);
    RESOLVE(GenSamplers); RESOLVE(DeleteSamplers); RESOLVE(BindSampler);
    RESOLVE(SamplerParameteri); RESOLVE(SamplerParameterf); RESOLVE(SamplerParameterIiv);
    RESOLVE(GetTexImage); RESOLVE(PixelStorei);
    RESOLVE(GenFramebuffers); RESOLVE(BindFramebuffer); RESOLVE(DeleteFramebuffers);
    RESOLVE(FramebufferTexture2D); RESOLVE(FramebufferTextureLayer);
    RESOLVE(GenRenderbuffers); RESOLVE(DeleteRenderbuffers); RESOLVE(BindRenderbuffer);
    RESOLVE(RenderbufferStorage); RESOLVE(FramebufferRenderbuffer);
    RESOLVE(CheckFramebufferStatus); RESOLVE(GetFramebufferAttachmentParameteriv); RESOLVE(DrawBuffer);
    RESOLVE(Viewport); RESOLVE(ClearColor); RESOLVE(ClearDepth); RESOLVE(Clear); RESOLVE(GetError);
    RESOLVE(Enable); RESOLVE(Disable); RESOLVE(DepthFunc); RESOLVE(DepthMask);
    RESOLVE(FrontFace); RESOLVE(BlendFunc); RESOLVE(BlendEquation); RESOLVE(ColorMask);
    RESOLVE(DrawArrays); RESOLVE(DrawElements); RESOLVE(DrawArraysInstanced);
    RESOLVE(DrawElementsInstanced); RESOLVE(ReadPixels); RESOLVE(Finish);
    RESOLVE(Flush); RESOLVE(GetIntegerv); RESOLVE(GetString);
    RESOLVE(BlitFramebuffer);
    // DebugMessageCallback (4.3) and CopyImageSubData (4.3) stay null on macOS.
#if !defined(__APPLE__)
    RESOLVE(BindImageTexture);
    RESOLVE(DebugMessageCallback);
    RESOLVE(CopyImageSubData);
#endif
}
#undef RESOLVE

} // namespace Genesis
