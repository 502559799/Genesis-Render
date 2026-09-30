# ============================================================================
#  Genesis Render 2.0 - cross platform build (Linux / macOS)
#
#  Options
#    GENESIS_BACKEND_OPENGL   build the OpenGL 4.1 backend            (default ON)
#    GENESIS_BACKEND_VULKAN   build the Vulkan backend                (default ON)
#    GENESIS_BACKEND_METAL    build the Metal backend                 (default ON on macOS)
#    GENESIS_BUILD_DEMO       build the demo application              (default ON)
#    GENESIS_BUILD_TESTS      build the unit / render test suite      (default ON)
#    GENESIS_LEGACY_DEMO1     build the original macOS camera demo    (default OFF, APPLE only)
#    GENESIS_REBUILD_SHADERS  re-run scripts/build_shaders.py at build time (AUTO|ON|OFF)
#    GENESIS_VULKAN_VALIDATION enable Vulkan validation layers in the demo  (default OFF)
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(GenesisRender
        VERSION 2.0.0
        DESCRIPTION "Genesis Render - cross platform ECS rendering engine (OpenGL + Vulkan + Metal, PBR/IBL)"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GENESIS_BACKEND_OPENGL "Build the OpenGL backend" ON)
option(GENESIS_BACKEND_VULKAN "Build the Vulkan backend" ON)
option(GENESIS_BUILD_DEMO "Build the demo application" ON)
option(GENESIS_BUILD_TESTS "Build the test suite" ON)
option(GENESIS_LEGACY_DEMO1 "Build the legacy macOS camera demo" OFF)
option(GENESIS_VULKAN_VALIDATION "Enable Vulkan validation layers by default" OFF)
set(GENESIS_REBUILD_SHADERS "AUTO" CACHE STRING "Regenerate shader headers: AUTO|ON|OFF")

# Metal is only available on macOS; force it to OFF elsewhere and turn it on
# by default on Apple so the three backends are available there.
if(APPLE)
    set(_metal_default ON)
else()
    set(_metal_default OFF)
endif()
option(GENESIS_BACKEND_METAL "Build the Metal backend (macOS only)" ${_metal_default})
if(NOT APPLE)
    set(GENESIS_BACKEND_METAL OFF CACHE BOOL "Build the Metal backend (macOS only)" FORCE)
endif()

if(NOT GENESIS_BACKEND_OPENGL AND NOT GENESIS_BACKEND_VULKAN AND NOT GENESIS_BACKEND_METAL)
    message(FATAL_ERROR "At least one rendering backend must be enabled")
endif()

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
set(GENESIS_PLATFORM_MACOS FALSE)
set(GENESIS_PLATFORM_LINUX FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(GENESIS_PLATFORM_MACOS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GENESIS_PLATFORM_LINUX TRUE)
else()
    message(WARNING "Genesis Render is validated on Linux and macOS; building for ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "----------------------------------------------------------")
message(STATUS " Genesis Render ${PROJECT_VERSION}")
message(STATUS "   platform : ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "   compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "   build    : ${CMAKE_BUILD_TYPE}")
message(STATUS "   backends : OpenGL=${GENESIS_BACKEND_OPENGL} Vulkan=${GENESIS_BACKEND_VULKAN} Metal=${GENESIS_BACKEND_METAL}")
message(STATUS "----------------------------------------------------------")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# SDL2: try the CMake package first, then pkg-config, then a manual search.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2_PC QUIET sdl2)
    endif()
    if(SDL2_PC_FOUND)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_PC_INCLUDE_DIRS})
        set(SDL2_LIBRARIES ${SDL2_PC_LIBRARIES})
        message(STATUS "SDL2 found via pkg-config: ${SDL2_LIBRARIES}")
    else()
        find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 include/SDL2)
        find_library(SDL2_LIBRARY NAMES SDL2)
        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            set(SDL2_FOUND TRUE)
            set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
            set(SDL2_LIBRARIES ${SDL2_LIBRARY})
            message(STATUS "SDL2 found manually: ${SDL2_LIBRARY}")
        endif()
    endif()
endif()
if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 is required. Linux: apt install libsdl2-dev | macOS: brew install sdl2")
endif()

if(TARGET SDL2::SDL2)
    set(GENESIS_SDL_TARGET SDL2::SDL2)
else()
    add_library(GenesisSDL2 INTERFACE)
    target_include_directories(GenesisSDL2 INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(GenesisSDL2 INTERFACE ${SDL2_LIBRARIES})
    set(GENESIS_SDL_TARGET GenesisSDL2)
endif()

# Vulkan (optional - the build degrades to OpenGL only when missing)
if(GENESIS_BACKEND_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h)
        find_library(VULKAN_LIBRARY NAMES vulkan vulkan-1)
        if(VULKAN_INCLUDE_DIR AND VULKAN_LIBRARY)
            set(Vulkan_FOUND TRUE)
            set(Vulkan_INCLUDE_DIRS ${VULKAN_INCLUDE_DIR})
            set(Vulkan_LIBRARIES ${VULKAN_LIBRARY})
        endif()
    endif()
    if(NOT Vulkan_FOUND)
        message(WARNING "Vulkan SDK not found - disabling the Vulkan backend")
        set(GENESIS_BACKEND_VULKAN OFF)
    else()
        message(STATUS "Vulkan found: ${Vulkan_LIBRARIES}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Shader code generation (GLSL -> SPIR-V header + GLSL header)
# ---------------------------------------------------------------------------
include(GenesisShaders)
genesis_configure_shaders()

# ---------------------------------------------------------------------------
# Third party sources compiled into the engine
# ---------------------------------------------------------------------------
set(GENESIS_EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/external)

set(IMGUI_SOURCES
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui.cpp
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui_draw.cpp
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui_tables.cpp
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui_widgets.cpp
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui_demo.cpp
    ${GENESIS_EXTERNAL_DIR}/imgui/imgui_impl_sdl.cpp
)
# ImGui is rendered by the engine's own RHI renderer (src/genesis/ui), so no
# graphics API specific ImGui backend is compiled.

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${GENESIS_EXTERNAL_DIR}/lua/*.c)

# ---------------------------------------------------------------------------
# Engine sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GENESIS_ENGINE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis/*.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis/*.h)

set(GENESIS_GL_SOURCES "")
set(GENESIS_VK_SOURCES "")
set(GENESIS_COMMON_SOURCES "")
foreach(src ${GENESIS_ENGINE_SOURCES})
    if(src MATCHES "/rhi/opengl/")
        list(APPEND GENESIS_GL_SOURCES ${src})
    elseif(src MATCHES "/rhi/vulkan/")
        list(APPEND GENESIS_VK_SOURCES ${src})
    else()
        list(APPEND GENESIS_COMMON_SOURCES ${src})
    endif()
endforeach()

set(GENESIS_ENGINE_ALL_SOURCES ${GENESIS_COMMON_SOURCES})
if(GENESIS_BACKEND_OPENGL)
    list(APPEND GENESIS_ENGINE_ALL_SOURCES ${GENESIS_GL_SOURCES})
endif()
if(GENESIS_BACKEND_VULKAN)
    list(APPEND GENESIS_ENGINE_ALL_SOURCES ${GENESIS_VK_SOURCES})
endif()

# Metal backend is Objective-C++ (.mm) and only on macOS; it is not picked
# up by the *.cpp glob, so add it explicitly.
if(GENESIS_BACKEND_METAL)
    list(APPEND GENESIS_ENGINE_ALL_SOURCES
         ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis/rhi/metal/metal_backend.mm)
endif()

add_library(GenesisEngine STATIC ${GENESIS_ENGINE_ALL_SOURCES} ${IMGUI_SOURCES} ${LUA_SOURCES})

# The Metal backend is Objective-C++ with ARC: every Metal object lives in a
# strong id<> member of a C++ class defined inside metal_backend.mm, so the
# rest of the engine never sees Objective-C.
if(GENESIS_BACKEND_METAL)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis/rhi/metal/metal_backend.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
endif()

target_include_directories(GenesisEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/lua
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(GenesisEngine PUBLIC
    GENESIS_RHI_OPENGL=$<BOOL:${GENESIS_BACKEND_OPENGL}>
    GENESIS_RHI_VULKAN=$<BOOL:${GENESIS_BACKEND_VULKAN}>
    GENESIS_RHI_METAL=$<BOOL:${GENESIS_BACKEND_METAL}>
    GENESIS_PLATFORM_MACOS=$<BOOL:${GENESIS_PLATFORM_MACOS}>
    GENESIS_PLATFORM_LINUX=$<BOOL:${GENESIS_PLATFORM_LINUX}>
    GENESIS_ENGINE_VERSION="${PROJECT_VERSION}"
)

target_compile_definitions(GenesisEngine PRIVATE
    _CRT_SECURE_NO_WARNINGS
)
if(GENESIS_PLATFORM_LINUX)
    target_compile_definitions(GenesisEngine PRIVATE LUA_USE_LINUX)
elseif(GENESIS_PLATFORM_MACOS)
    target_compile_definitions(GenesisEngine PRIVATE LUA_USE_MACOSX GL_SILENCE_DEPRECATION)
endif()

target_link_libraries(GenesisEngine PUBLIC
    ${GENESIS_SDL_TARGET}
    Threads::Threads
    ZLIB::ZLIB
    GenesisShaderHeaders
)

if(GENESIS_BACKEND_VULKAN)
    target_include_directories(GenesisEngine PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(GenesisEngine PUBLIC ${Vulkan_LIBRARIES})
endif()

if(GENESIS_PLATFORM_MACOS)
    set(GENESIS_MACOS_FRAMEWORKS
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreFoundation")
    if(GENESIS_BACKEND_OPENGL)
        list(APPEND GENESIS_MACOS_FRAMEWORKS "-framework OpenGL")
    endif()
    if(GENESIS_BACKEND_METAL)
        list(APPEND GENESIS_MACOS_FRAMEWORKS "-framework Metal")
    endif()
    target_link_libraries(GenesisEngine PUBLIC ${GENESIS_MACOS_FRAMEWORKS})
else()
    target_link_libraries(GenesisEngine PUBLIC ${CMAKE_DL_LIBS})
    if(GENESIS_BACKEND_OPENGL)
        find_library(GENESIS_GL_LIBRARY NAMES GL)
        if(GENESIS_GL_LIBRARY)
            target_link_libraries(GenesisEngine PUBLIC ${GENESIS_GL_LIBRARY})
        else()
            message(WARNING "libGL not found; the OpenGL backend may fail to link")
        endif()
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(GenesisEngine PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Vendored third party code is compiled without our warning settings
set_source_files_properties(${IMGUI_SOURCES} ${LUA_SOURCES} PROPERTIES COMPILE_FLAGS "-w")

# ---------------------------------------------------------------------------
# Demo
# ---------------------------------------------------------------------------
if(GENESIS_BUILD_DEMO)
    file(GLOB GENESIS_DEMO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/demo/*.cpp)
    if(GENESIS_DEMO_SOURCES)
        add_executable(GenesisDemo ${GENESIS_DEMO_SOURCES})
        target_link_libraries(GenesisDemo PRIVATE GenesisEngine)
        target_include_directories(GenesisDemo PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis)
        target_compile_definitions(GenesisDemo PRIVATE
            GENESIS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
            GENESIS_VULKAN_VALIDATION_DEFAULT=$<BOOL:${GENESIS_VULKAN_VALIDATION}>)
        set_target_properties(GenesisDemo PROPERTIES OUTPUT_NAME "genesis_demo")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GENESIS_BUILD_TESTS)
    file(GLOB GENESIS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    if(GENESIS_TEST_SOURCES)
        add_executable(GenesisTests ${GENESIS_TEST_SOURCES})
        target_link_libraries(GenesisTests PRIVATE GenesisEngine)
        target_include_directories(GenesisTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/genesis)
        target_compile_definitions(GenesisTests PRIVATE
            GENESIS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
            GENESIS_VULKAN_VALIDATION_DEFAULT=$<BOOL:${GENESIS_VULKAN_VALIDATION}>)
        set_target_properties(GenesisTests PROPERTIES OUTPUT_NAME "genesis_tests")
        enable_testing()
        add_test(NAME genesis_unit_tests COMMAND genesis_tests --no-window)
    endif()
endif()

# ---------------------------------------------------------------------------
# Legacy macOS camera demo (original v1 code, kept for reference)
# ---------------------------------------------------------------------------
if(GENESIS_LEGACY_DEMO1)
    if(GENESIS_PLATFORM_MACOS)
        add_subdirectory(src/demo1)
    else()
        message(STATUS "GENESIS_LEGACY_DEMO1 requires macOS (AVFoundation) - skipped")
    endif()
endif()

message(STATUS "----------------------------------------------------------")
message(STATUS " Configure complete. Build with:")
message(STATUS "   cmake --build ${CMAKE_BINARY_DIR} -j")
message(STATUS "----------------------------------------------------------")
