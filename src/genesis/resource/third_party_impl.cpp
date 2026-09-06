/*
 * Genesis Render - third party single header implementations
 *
 * Exactly one translation unit in the engine defines the implementation macros
 * so that headers can be included everywhere else without duplicate symbols.
 */
#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "external/cgltf/cgltf.h"
