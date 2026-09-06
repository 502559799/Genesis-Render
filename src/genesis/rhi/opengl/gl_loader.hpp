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

#endif /* __GENESIS_GL_LOADER_HPP__ */
