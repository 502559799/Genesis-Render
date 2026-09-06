cmake_minimum_required(VERSION 3.16)
project(GenesisRender LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GENESIS_VULKAN "Build the Vulkan RHI backend" ON)
option(GENESIS_OPENGL "Build the OpenGL RHI backend" ON)
option(GENESIS_ASSIMP "Enable FBX/extra formats via Assimp" ON)
option(GENESIS_HEADLESS_TESTS "Build headless CI test hooks" ON)

# ---------------------------------------------------------------------------
# System dependencies
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)

set(GENESIS_GFX_LIBS "")
set(GENESIS_GFX_DEFS "")

if(GENESIS_VULKAN)
    find_package(Vulkan REQUIRED)
    list(APPEND GENESIS_GFX_LIBS Vulkan::Vulkan)
    list(APPEND GENESIS_GFX_DEFS GENESIS_VULKAN=1)
endif()

if(GENESIS_OPENGL)
    if(APPLE)
        # macOS ships OpenGL 4.1 as a framework (headers in <OpenGL/>)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        list(APPEND GENESIS_GFX_LIBS ${OPENGL_FRAMEWORK})
    else()
        set(OpenGL_GL_PREFERENCE GLVND)
        find_package(OpenGL REQUIRED)
        list(APPEND GENESIS_GFX_LIBS OpenGL::GL)
    endif()
    list(APPEND GENESIS_GFX_DEFS GENESIS_OPENGL=1)
    if(UNIX AND NOT APPLE)
        find_library(EGL_LIBRARY EGL REQUIRED)
        list(APPEND GENESIS_GFX_LIBS ${EGL_LIBRARY})
        list(APPEND GENESIS_GFX_DEFS GENESIS_EGL=1)
    endif()
endif()

if(GENESIS_ASSIMP)
    find_package(assimp CONFIG QUIET)
    find_package(PkgConfig QUIET)
    if(TARGET assimp::assimp)
        set(ASSIMP_TARGET assimp::assimp)
        set(GENESIS_HAVE_ASSIMP ON)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(ASSIMP assimp)
        if(ASSIMP_FOUND)
            set(ASSIMP_TARGET ${ASSIMP_LIBRARIES})
            set(GENESIS_HAVE_ASSIMP ON)
        endif()
    endif()
    if(GENESIS_HAVE_ASSIMP)
        message(STATUS "Genesis: Assimp enabled -> ${ASSIMP_TARGET}")
        list(APPEND GENESIS_GFX_DEFS GENESIS_WITH_ASSIMP=1)
    else()
        message(WARNING "Genesis: Assimp not found - FBX import disabled (glTF still available)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Foundation layer
# ---------------------------------------------------------------------------
add_library(GenesisFoundation STATIC
    src/genesis/foundation_layer/platform.hpp
    src/genesis/foundation_layer/service.hpp
    src/genesis/foundation_layer/serviceManager.hpp
    src/genesis/foundation_layer/serviceManager.cpp
    src/genesis/foundation_layer/serialization_blob.hpp
    src/genesis/foundation_layer/serialization_blob.cpp
    src/genesis/foundation_layer/log.hpp
    src/genesis/foundation_layer/log.cpp
    src/genesis/foundation_layer/file.hpp
    src/genesis/foundation_layer/file.cpp
    src/genesis/foundation_layer/assert.hpp
    src/genesis/foundation_layer/array.hpp
    src/genesis/foundation_layer/memory.hpp
    src/genesis/foundation_layer/memory.cpp
)
target_include_directories(GenesisFoundation PUBLIC src src/genesis)
target_compile_features(GenesisFoundation PUBLIC cxx_std_17)
if(MSVC)
    target_compile_definitions(GenesisFoundation PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Third party
# ---------------------------------------------------------------------------
set(IMGUI_DIR src/external/imgui)
add_library(GenesisImGui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_impl_sdl.cpp
)
target_include_directories(GenesisImGui PUBLIC ${IMGUI_DIR} src src/external/imgui_gl3)
if(TARGET SDL2::SDL2)
    target_link_libraries(GenesisImGui PUBLIC SDL2::SDL2)
else()
    target_include_directories(GenesisImGui PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(GenesisImGui PUBLIC ${SDL2_LIBRARIES})
endif()

if(GENESIS_VULKAN)
    target_sources(GenesisImGui PRIVATE ${IMGUI_DIR}/imgui_impl_vulkan.cpp)
    target_link_libraries(GenesisImGui PUBLIC Vulkan::Vulkan)
    target_compile_definitions(GenesisImGui PUBLIC GENESIS_VULKAN=1)
endif()
if(GENESIS_OPENGL)
    target_sources(GenesisImGui PRIVATE src/external/imgui_gl3/imgui_impl_opengl3.cpp)
    target_compile_definitions(GenesisImGui PUBLIC GENESIS_OPENGL=1)
endif()

# Lua 5.4 (vendored sources, public domain)
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/external/lua/src/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*(lua|luac)\\.c$")
add_library(GenesisLua STATIC ${LUA_SOURCES})
target_include_directories(GenesisLua PUBLIC src/external/lua/src)
if(NOT MSVC)
    target_compile_options(GenesisLua PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
set(ENGINE_SOURCES
    src/genesis/application_layer/window.cpp
    src/genesis/core/math.hpp
    src/genesis/ecs/registry.hpp
    src/genesis/ecs/scene.cpp
    src/genesis/ecs/components.hpp
    src/genesis/resource/mesh_data.hpp
    src/genesis/resource/texture_loader.cpp
    src/genesis/resource/model_loader.cpp
    src/genesis/resource/model_loader_gltf.cpp
    src/genesis/resource/asset_manager.cpp
    src/genesis/script/lua_engine.cpp
    src/genesis/rhi/gl_procs.cpp
    src/genesis/rhi/half.hpp
    src/genesis/render/shader_library.cpp
    src/genesis/render/pbr_renderer.cpp
    src/genesis/render/ibl_baker.cpp
    src/genesis/render/primitives.cpp
    src/genesis/ui/editor_ui.cpp
)
if(GENESIS_VULKAN)
    list(APPEND ENGINE_SOURCES src/genesis/rhi/rhi_vulkan.cpp)
endif()
if(GENESIS_OPENGL)
    list(APPEND ENGINE_SOURCES src/genesis/rhi/rhi_opengl.cpp)
endif()
if(GENESIS_HAVE_ASSIMP)
    list(APPEND ENGINE_SOURCES src/genesis/resource/model_loader_assimp.cpp)
endif()

add_library(GenesisEngine STATIC ${ENGINE_SOURCES})
if(APPLE)
    target_compile_definitions(GenesisEngine PRIVATE GL_SILENCE_DEPRECATION)
endif()
target_include_directories(GenesisEngine PUBLIC
    src src/genesis src/external src/external/cgltf src/external/stb)
target_compile_definitions(GenesisEngine PUBLIC ${GENESIS_GFX_DEFS})
target_link_libraries(GenesisEngine PUBLIC
    GenesisFoundation GenesisImGui GenesisLua
    ${GENESIS_GFX_LIBS} pthread
)
if(TARGET SDL2::SDL2)
    target_link_libraries(GenesisEngine PUBLIC SDL2::SDL2)
else()
    target_include_directories(GenesisEngine PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(GenesisEngine PUBLIC ${SDL2_LIBRARIES})
endif()
if(GENESIS_HAVE_ASSIMP)
    target_link_libraries(GenesisEngine PUBLIC ${ASSIMP_TARGET})
endif()
if(APPLE)
    target_link_libraries(GenesisEngine PUBLIC
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# ---------------------------------------------------------------------------
# Shaders (GLSL -> SPIR-V for Vulkan; GL backend compiles the same GLSL)
# ---------------------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator)
file(GLOB_RECURSE ENGINE_SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.vert
                                ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.frag)
if(GLSLANG_VALIDATOR AND ENGINE_SHADERS)
    set(SPV_OUTPUTS "")
    foreach(SHADER ${ENGINE_SHADERS})
        get_filename_component(SNAME ${SHADER} NAME)
        set(OUT ${CMAKE_BINARY_DIR}/assets/shaders/${SNAME}.spv)
        add_custom_command(
            OUTPUT ${OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/shaders
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${OUT}
            DEPENDS ${SHADER}
            COMMENT "glslang -> SPIR-V ${SNAME}")
        list(APPEND SPV_OUTPUTS ${OUT})
    endforeach()
    add_custom_target(GenesisShaders ALL DEPENDS ${SPV_OUTPUTS})
    add_dependencies(GenesisEngine GenesisShaders)
else()
    message(WARNING "glslangValidator not found; Vulkan shaders will be missing")
endif()

# copy assets next to the build outputs
add_custom_target(GenesisAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets)
add_dependencies(GenesisEngine GenesisAssets)

# ---------------------------------------------------------------------------
# Demo / test executable
# ---------------------------------------------------------------------------
add_executable(GenesisDemo src/demo/main.cpp)
target_link_libraries(GenesisDemo PRIVATE GenesisEngine)
set_target_properties(GenesisDemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
