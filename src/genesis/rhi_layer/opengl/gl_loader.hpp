/*
 * @Descripttion: 极简 OpenGL (core profile) 函数加载器
 *               仅包含引擎所需的 ~60 个入口, 通过 SDL_GL_GetProcAddress 解析,
 *               跨平台 (Linux/macOS) 不依赖平台 GL 头文件
 * @version: v1.0
 */
#ifndef __GENESIS_GL_LOADER_H__
#define __GENESIS_GL_LOADER_H__

#include <stddef.h>
#include <stdint.h>

//----------------[基本类型 (Khronos)]----------------//
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int8_t GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;

//----------------[常量]----------------//
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_MAX_LOD 0x813B
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_REPEAT 0x2901
#define GL_MIRRORED_REPEAT 0x8370
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_RG 0x8227
#define GL_RED 0x1903
#define GL_FRONT 0x0404
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_RGBA8 0x8058
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RG16F 0x822F
#define GL_RGBA16F 0x881A
#define GL_RGBA32F 0x8814
#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_GEQUAL 0x0206
#define GL_EQUAL 0x0202
#define GL_ALWAYS 0x0207
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_BACK_LEFT 0x0402
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D

//----------------[函数指针声明]----------------//
// 宏参数: 大写名, 小写变量名, 返回类型, 参数列表
#define GENESIS_GL_LIST(X)                                                   \
    X(GLVIEWPORT, glViewport, void, (GLint, GLint, GLsizei, GLsizei))        \
    X(GLSCISSOR, glScissor, void, (GLint, GLint, GLsizei, GLsizei))          \
    X(GLCLEARCOLOR, glClearColor, void, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(GLCLEARDEPTH, glClearDepth, void, (GLdouble))                          \
    X(GLCLEAR, glClear, void, (GLbitfield))                                  \
    X(GLENABLE, glEnable, void, (GLenum))                                    \
    X(GLDISABLE, glDisable, void, (GLenum))                                  \
    X(GLDEPTHFUNC, glDepthFunc, void, (GLenum))                              \
    X(GLDEPTHMASK, glDepthMask, void, (GLboolean))                           \
    X(GLCULLFACE, glCullFace, void, (GLenum))                                \
    X(GLFRONTFACE, glFrontFace, void, (GLenum))                              \
    X(GLBLENDFUNC, glBlendFunc, void, (GLenum, GLenum))                      \
    X(GLGETERROR, glGetError, GLenum, (void))                                \
    X(GLGETSTRING, glGetString, const GLubyte *, (GLenum))                   \
    X(GLGETINTEGERV, glGetIntegerv, void, (GLenum, GLint *))                 \
    X(GLPIXELSTOREI, glPixelStorei, void, (GLenum, GLint))                   \
    X(GLFINISH, glFinish, void, (void))                                      \
    X(GLREADPIXELS, glReadPixels, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(GLACTIVETEXTURE, glActiveTexture, void, (GLenum))                      \
    X(GLDRAWBUFFERS, glDrawBuffers, void, (GLsizei, const GLenum *))         \
    X(GLGENBUFFERS, glGenBuffers, void, (GLsizei, GLuint *))                 \
    X(GLDELETEBUFFERS, glDeleteBuffers, void, (GLsizei, const GLuint *))     \
    X(GLBINDBUFFER, glBindBuffer, void, (GLenum, GLuint))                    \
    X(GLBUFFERDATA, glBufferData, void, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(GLBUFFERSUBDATA, glBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(GLBINDBUFFERBASE, glBindBufferBase, void, (GLenum, GLuint, GLuint))    \
    X(GLGENVERTEXARRAYS, glGenVertexArrays, void, (GLsizei, GLuint *))       \
    X(GLDELETEVERTEXARRAYS, glDeleteVertexArrays, void, (GLsizei, const GLuint *)) \
    X(GLBINDVERTEXARRAY, glBindVertexArray, void, (GLuint))                  \
    X(GLENABLEVERTEXATTRIBARRAY, glEnableVertexAttribArray, void, (GLuint))  \
    X(GLVERTEXATTRIBPOINTER, glVertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(GLGENTEXTURES, glGenTextures, void, (GLsizei, GLuint *))               \
    X(GLDELETETEXTURES, glDeleteTextures, void, (GLsizei, const GLuint *))   \
    X(GLBINDTEXTURE, glBindTexture, void, (GLenum, GLuint))                  \
    X(GLTEXIMAGE2D, glTexImage2D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(GLTEXSUBIMAGE2D, glTexSubImage2D, void, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(GLTEXPARAMETERI, glTexParameteri, void, (GLenum, GLenum, GLint))       \
    X(GLTEXPARAMETERF, glTexParameterf, void, (GLenum, GLenum, GLfloat))     \
    X(GLGENERATEMIPMAP, glGenerateMipmap, void, (GLenum))                    \
    X(GLGENFRAMEBUFFERS, glGenFramebuffers, void, (GLsizei, GLuint *))       \
    X(GLDELETEFRAMEBUFFERS, glDeleteFramebuffers, void, (GLsizei, const GLuint *)) \
    X(GLBINDFRAMEBUFFER, glBindFramebuffer, void, (GLenum, GLuint))          \
    X(GLFRAMEBUFFERTEXTURE2D, glFramebufferTexture2D, void, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLFRAMEBUFFERTEXTURELAYER, glFramebufferTextureLayer, void, (GLenum, GLenum, GLuint, GLint, GLint)) \
    X(GLCHECKFRAMEBUFFERSTATUS, glCheckFramebufferStatus, GLenum, (GLenum))  \
    X(GLCREATESHADER, glCreateShader, GLuint, (GLenum))                      \
    X(GLSHADERSOURCE, glShaderSource, void, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(GLCOMPILESHADER, glCompileShader, void, (GLuint))                      \
    X(GLGETSHADERIV, glGetShaderiv, void, (GLuint, GLenum, GLint *))         \
    X(GLGETSHADERINFOLOG, glGetShaderInfoLog, void, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLDELETESHADER, glDeleteShader, void, (GLuint))                        \
    X(GLCREATEPROGRAM, glCreateProgram, GLuint, (void))                      \
    X(GLATTACHSHADER, glAttachShader, void, (GLuint, GLuint))                \
    X(GLLINKPROGRAM, glLinkProgram, void, (GLuint))                          \
    X(GLGETPROGRAMIV, glGetProgramiv, void, (GLuint, GLenum, GLint *))       \
    X(GLGETPROGRAMINFOLOG, glGetProgramInfoLog, void, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLUSEPROGRAM, glUseProgram, void, (GLuint))                            \
    X(GLDELETEPROGRAM, glDeleteProgram, void, (GLuint))                      \
    X(GLGETUNIFORMLOCATION, glGetUniformLocation, GLint, (GLuint, const GLchar *)) \
    X(GLUNIFORM1I, glUniform1i, void, (GLint, GLint))                        \
    X(GLGETUNIFORMBLOCKINDEX, glGetUniformBlockIndex, GLuint, (GLuint, const GLchar *)) \
    X(GLUNIFORMBLOCKBINDING, glUniformBlockBinding, void, (GLuint, GLuint, GLuint)) \
    X(GLDRAWARRAYS, glDrawArrays, void, (GLenum, GLint, GLsizei))            \
    X(GLDRAWELEMENTS, glDrawElements, void, (GLenum, GLsizei, GLenum, const void *)) \
    X(GLDRAWELEMENTSBASEVERTEX, glDrawElementsBaseVertex, void, (GLenum, GLsizei, GLenum, const void *, GLint))

// 生成 typedef + extern 变量声明
#define GENESIS_GL_DECLARE(UPPER, LOWER, RET, ARGS) \
    typedef RET(*PFNGL##UPPER##PROC) ARGS;          \
    extern PFNGL##UPPER##PROC LOWER;
GENESIS_GL_LIST(GENESIS_GL_DECLARE)
#undef GENESIS_GL_DECLARE

namespace Genesis
{
    namespace rhi
    {
        // 解析全部函数指针; 返回 false 表示上下文不可用
        bool glLoadAll();
    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_GL_LOADER_H__
