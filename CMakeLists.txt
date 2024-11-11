cmake_minimum_required(VERSION 3.24)
project(VULKAN_RENDERER VERSION 0.1.0)

find_package(Vulkan REQUIRED)

if (UNIX)
find_package(SDL2 REQUIRED)
endif()

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

set_property(TARGET GenesisFoundation PROPERTY CXX_STANDARD 17)

target_compile_definitions(GenesisFoundation PRIVATE
    _CRT_SECURE_NO_WARNINGS

    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(GenesisFoundation PRIVATE
    src
    src/genesis     
)

add_library(GenesisApp STATIC
    src/genesis/application_layer/window.hpp
    src/genesis/application_layer/window.cpp
)

set_property(TARGET GenesisApp PROPERTY CXX_STANDARD 17)

target_compile_definitions(GenesisApp PRIVATE
    _CRT_SECURE_NO_WARNINGS

    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)


target_include_directories(GenesisApp PRIVATE
    src
    src/genesis     
)

target_include_directories(GenesisApp PRIVATE
${SDL2_INCLUDE_DIRS})


add_library(GenesisExternal STATIC
    src/external/imgui/imgui_impl_sdl.cpp
    src/external/imgui/imgui_impl_sdl.h
    src/external/imgui/imgui.cpp
    src/external/imgui/imgui.h
    src/external/imgui/imgui_draw.cpp
    src/external/imgui/imgui_tables.cpp
    src/external/imgui/imgui_widgets.cpp
    src/external/tlsf.c
)

target_compile_definitions(GenesisExternal PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(GenesisExternal PRIVATE
${SDL2_INCLUDE_DIRS})

set_property(TARGET GenesisExternal PROPERTY CXX_STANDARD 17)



add_subdirectory(src/demo1)
