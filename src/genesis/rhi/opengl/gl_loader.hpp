/*
 * Genesis Render - OpenGL headers / minimal loader
 *
 * The engine targets OpenGL 4.1 core profile, which is the highest version
 * Apple ships on macOS.  Every GL entry point used by the backend therefore
 * exists as a real exported symbol on both Linux (libGL / libglvnd) and macOS
 * (OpenGL.framework), so we link directly instead of pulling in GLAD/GLEW.
 */
#ifndef __GENESIS_GL_LOADER_HPP__
#define __GENESIS_GL_LOADER_HPP__

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

// GL_RGBA16F / GL_R16F etc. are part of gl3ext on Apple.
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH32F_STENCIL8
#define GL_DEPTH32F_STENCIL8 0x8CAD
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#endif

// OpenGL 4.3 compute / SSBO constants.  Apple's GL 4.1 headers do not have
// them; the symbols are still declared so that the backend compiles, while
// dispatch reports that compute is unavailable on that platform.
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_TRANSFER_SRC_BARRIER_BIT
#define GL_TRANSFER_SRC_BARRIER_BIT 0x00000400
#endif
#ifndef GL_TRANSFER_DST_BARRIER_BIT
#define GL_TRANSFER_DST_BARRIER_BIT 0x00000800
#endif

#endif /* __GENESIS_GL_LOADER_HPP__ */
