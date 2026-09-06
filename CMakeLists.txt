# =============================================================================
# Genesis Render Engine
# Cross-platform (Linux / macOS / Windows), dual RHI backends (Vulkan, OpenGL),
# ECS, Assimp model import (glTF/FBX/OBJ), PBR + runtime IBL, ImGui editor, Lua.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(GenesisRender LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(GENESIS_VULKAN "Build the Vulkan RHI backend" ON)
option(GENESIS_OPENGL "Build the OpenGL RHI backend" ON)
option(GENESIS_ASSIMP "Import glTF/FBX/OBJ via Assimp" ON)
# On macOS, build for the host architecture by default (e.g. Apple Silicon
# machines usually only have arm64 Homebrew dylibs). Enable this option only
# when universal (x86_64+arm64) SDL2/assimp libraries are installed.
option(GENESIS_MAC_UNIVERSAL "Build a universal x86_64+arm64 macOS binary" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(APPLE)
    if(GENESIS_MAC_UNIVERSAL)
        set(GENESIS_MAC_ARCHS "x86_64;arm64" CACHE STRING "macOS target architectures")
    else()
        # Native build: matches the architecture of the installed Homebrew libs.
        set(GENESIS_MAC_ARCHS "${CMAKE_HOST_SYSTEM_PROCESSOR}" CACHE STRING "macOS target architectures")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${GENESIS_MAC_ARCHS}" CACHE STRING "" FORCE)
    message(STATUS "Genesis macOS architectures: ${CMAKE_OSX_ARCHITECTURES}")
endif()

# ---------------------------------------------------------------- dependencies
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

if(GENESIS_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING "Vulkan SDK not found - Vulkan backend disabled")
        set(GENESIS_VULKAN OFF)
    endif()
endif()
if(GENESIS_OPENGL)
    find_package(OpenGL QUIET)
    if(NOT OpenGL_FOUND)
        set(GENESIS_VULKAN OFF)
    endif()
endif()
if(GENESIS_ASSIMP)
    find_package(assimp QUIET CONFIG)
    if(NOT assimp_FOUND)
        find_library(ASSIMP_LIB assimp REQUIRED)
        find_path(ASSIMP_INC assimp/Importer.hpp)
    endif()
endif()

if(NOT GENESIS_VULKAN AND NOT GENESIS_OPENGL)
    message(FATAL_ERROR "At least one RHI backend is required")
endif()

# ----------------------------------------------------------- third-party libs
set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/src/external)

# ---- Dear ImGui (vendored 1.86 + downloaded OpenGL3 backend)
set(IMGUI_SOURCES
    ${EXT}/imgui/imgui.cpp
    ${EXT}/imgui/imgui_draw.cpp
    ${EXT}/imgui/imgui_tables.cpp
    ${EXT}/imgui/imgui_widgets.cpp
    ${EXT}/imgui/imgui_demo.cpp
    ${EXT}/imgui/imgui_impl_sdl.cpp
    ${EXT}/imgui/imgui_impl_vulkan.cpp
    ${EXT}/imgui/imgui_impl_opengl3.cpp)
add_library(genesis_imgui STATIC ${IMGUI_SOURCES})
target_include_directories(genesis_imgui PUBLIC ${EXT}/imgui ${SDL2_INCLUDE_DIRS})
target_link_libraries(genesis_imgui PUBLIC SDL2::SDL2)
if(GENESIS_VULKAN)
    target_compile_definitions(genesis_imgui PUBLIC GENESIS_ENABLE_VULKAN)
    target_link_libraries(genesis_imgui PUBLIC Vulkan::Vulkan)
endif()

# ---- glad GL loader
add_library(genesis_glad STATIC ${EXT}/glad_gen/src/glad.c)
target_include_directories(genesis_glad PUBLIC ${EXT}/glad_gen/include)

# ---- Lua 5.4 (vendored)
file(GLOB LUA_SOURCES ${EXT}/lua/l*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "lua\\.c$|luac\\.c$|ltests\\.c$")
add_library(genesis_lua STATIC ${LUA_SOURCES})
target_include_directories(genesis_lua PUBLIC ${EXT}/lua)
if(UNIX)
    target_compile_definitions(genesis_lua PUBLIC LUA_USE_LINUX)
endif()
set_target_properties(genesis_lua PROPERTIES C_STANDARD 99)

# -------------------------------------------------------------- shader embed
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
file(GLOB GENESIS_SHADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/glsl/*)
set(SHADERS_EMBED_CPP ${CMAKE_BINARY_DIR}/generated/shaders/shaders_embedded.cpp)
add_custom_command(
    OUTPUT ${SHADERS_EMBED_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/shaders
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.py
            --shader-dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/glsl
            --out ${SHADERS_EMBED_CPP}
            --glslang ${GLSLANG_VALIDATOR}
    DEPENDS ${GENESIS_SHADER_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.py
    COMMENT "Embedding shaders (GLSL + SPIR-V)")

# ------------------------------------------------------------------ engine lib
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis)
set(ENGINE_SOURCES
    ${ENGINE_DIR}/foundation_layer/log.cpp
    ${ENGINE_DIR}/foundation_layer/file.cpp
    ${ENGINE_DIR}/foundation_layer/memory.cpp
    ${ENGINE_DIR}/foundation_layer/serialization_blob.cpp
    ${ENGINE_DIR}/foundation_layer/serviceManager.cpp
    ${ENGINE_DIR}/application_layer/window.cpp
    ${ENGINE_DIR}/ecs/scene.cpp
    ${ENGINE_DIR}/rhi/rhi.cpp
    ${ENGINE_DIR}/rhi/glsl_preprocess.cpp
    ${ENGINE_DIR}/rhi/opengl/gl_rhi.cpp
    ${ENGINE_DIR}/resource/image.cpp
    ${ENGINE_DIR}/resource/procgen.cpp
    ${ENGINE_DIR}/resource/model_loader.cpp
    ${ENGINE_DIR}/resource/asset_manager.cpp
    ${ENGINE_DIR}/render/renderer.cpp
    ${ENGINE_DIR}/script/script_system.cpp
    ${ENGINE_DIR}/ui/ui_service.cpp
    ${EXT}/tlsf.c
    ${SHADERS_EMBED_CPP})
if(GENESIS_VULKAN)
    list(APPEND ENGINE_SOURCES ${ENGINE_DIR}/rhi/vulkan/vk_rhi.cpp)
endif()

add_library(genesis_engine STATIC ${ENGINE_SOURCES})
target_include_directories(genesis_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis
    ${CMAKE_BINARY_DIR}/generated
    ${EXT}
    ${EXT}/sol2
    ${EXT}/lua
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(genesis_engine PUBLIC
    genesis_imgui genesis_glad genesis_lua
    Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(genesis_engine PUBLIC GENESIS_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(GENESIS_VULKAN)
    target_compile_definitions(genesis_engine PUBLIC GENESIS_ENABLE_VULKAN)
    target_link_libraries(genesis_engine PUBLIC Vulkan::Vulkan)
endif()
if(GENESIS_OPENGL)
    target_link_libraries(genesis_engine PUBLIC OpenGL::GL)
endif()
if(GENESIS_ASSIMP)
    if(TARGET assimp::assimp)
        target_link_libraries(genesis_engine PUBLIC assimp::assimp)
    else()
        target_include_directories(genesis_engine PUBLIC ${ASSIMP_INC})
        target_link_libraries(genesis_engine PUBLIC ${ASSIMP_LIB})
    endif()
    target_compile_definitions(genesis_engine PUBLIC GENESIS_USE_ASSIMP)
endif()
if(APPLE)
    target_link_libraries(genesis_engine PUBLIC
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    set_target_properties(genesis_engine genesis_lua genesis_imgui genesis_glad PROPERTIES
        OSX_ARCHITECTURES "${GENESIS_MAC_ARCHS}")
endif()

# ------------------------------------------------------------- FBX asset tool
add_executable(gen_fbx tools/gen_fbx.cpp)
if(TARGET assimp::assimp)
    target_link_libraries(gen_fbx PRIVATE assimp::assimp)
else()
    target_include_directories(gen_fbx PRIVATE ${ASSIMP_INC})
    target_link_libraries(gen_fbx PRIVATE ${ASSIMP_LIB})
endif()
if(APPLE)
    set_target_properties(gen_fbx PROPERTIES OSX_ARCHITECTURES "${GENESIS_MAC_ARCHS}")
endif()

# runtime assets: copy tree into build dir and generate an FBX inside it
set(ASSET_OUT ${CMAKE_BINARY_DIR}/assets)
add_custom_target(GenesisAssets ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${ASSET_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets ${ASSET_OUT}
    COMMAND $<TARGET_FILE:gen_fbx> ${ASSET_OUT}/models/genesis_cube.fbx
    DEPENDS gen_fbx
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Deploying assets + generating genesis_cube.fbx")

# ---------------------------------------------------------------------- demos
add_executable(GenesisPBR src/demo/pbr_demo/main.cpp)
target_link_libraries(GenesisPBR PRIVATE genesis_engine)
add_dependencies(GenesisPBR GenesisAssets)
if(APPLE)
    set_target_properties(GenesisPBR PROPERTIES OSX_ARCHITECTURES "${GENESIS_MAC_ARCHS}")
endif()

# --------------------------------------------------------------------- install
install(TARGETS GenesisPBR RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION .)
