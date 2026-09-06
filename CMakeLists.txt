# Genesis Render Engine v2.0
# 跨平台 (Linux / macOS) 多后端 (Vulkan / OpenGL) 渲染引擎
cmake_minimum_required(VERSION 3.16)
project(GENESIS_RENDERER VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

option(GENESIS_BUILD_DEMO1 "build legacy demo1 (macOS camera demo)" ${APPLE})
option(GENESIS_BUILD_DEMO2 "build demo2 PBR multi-backend demo" ON)

#-----------------------------------------------------------
# 依赖
#-----------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

# assimp (模型导入: fbx/gltf/obj...)
find_package(assimp CONFIG QUIET)
if (NOT assimp_FOUND)
    find_library(ASSIMP_LIBRARY assimp REQUIRED)
    find_path(ASSIMP_INCLUDE_DIR assimp/Importer.hpp REQUIRED)
    set(ASSIMP_LIBRARIES ${ASSIMP_LIBRARY})
    set(ASSIMP_INCLUDE_DIRS ${ASSIMP_INCLUDE_DIR})
else()
    set(ASSIMP_LIBRARIES assimp::assimp)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

#-----------------------------------------------------------
# 基础层
#-----------------------------------------------------------
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
    src/external/tlsf.c
)

target_compile_definitions(GenesisFoundation PRIVATE _CRT_SECURE_NO_WARNINGS)

target_include_directories(GenesisFoundation PUBLIC
    src
    src/genesis
)

#-----------------------------------------------------------
# 应用层 (窗口 + 输入)
#-----------------------------------------------------------
add_library(GenesisApp STATIC
    src/genesis/application_layer/window.hpp
    src/genesis/application_layer/window.cpp
    src/genesis/application_layer/input.hpp
    src/genesis/application_layer/input.cpp
)

target_include_directories(GenesisApp PUBLIC
    src
    src/genesis
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(GenesisApp PUBLIC GenesisFoundation)

#-----------------------------------------------------------
# 第三方 (imgui + lua + tlsf)
#-----------------------------------------------------------
file(GLOB LUA_SOURCES src/external/lua/*.c)

add_library(GenesisExternal STATIC
    src/external/imgui/imgui_impl_sdl.cpp
    src/external/imgui/imgui_impl_vulkan.cpp
    src/external/imgui/imgui_impl_opengl3.cpp
    src/external/imgui/imgui.cpp
    src/external/imgui/imgui_draw.cpp
    src/external/imgui/imgui_tables.cpp
    src/external/imgui/imgui_widgets.cpp
    ${LUA_SOURCES}
)

target_include_directories(GenesisExternal PUBLIC
    src
    src/external
    src/external/sol2
    ${SDL2_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

# imgui_impl_opengl3 使用自带 imgl3w loader
target_compile_definitions(GenesisExternal PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

#-----------------------------------------------------------
# 引擎层 (RHI + ECS + 资源 + 渲染 + 脚本 + UI)
#-----------------------------------------------------------
add_library(GenesisEngine STATIC
    # RHI
    src/genesis/rhi_layer/rhi_types.hpp
    src/genesis/rhi_layer/rhi_types.cpp
    src/genesis/rhi_layer/resource_pool.hpp
    src/genesis/rhi_layer/rhi_device.hpp
    src/genesis/rhi_layer/rhi_factory.cpp
    src/genesis/rhi_layer/vulkan/vulkan_device.hpp
    src/genesis/rhi_layer/vulkan/vulkan_device.cpp
    src/genesis/rhi_layer/opengl/gl_loader.hpp
    src/genesis/rhi_layer/opengl/gl_loader.cpp
    src/genesis/rhi_layer/opengl/opengl_device.hpp
    src/genesis/rhi_layer/opengl/opengl_device.cpp
    # ECS
    src/genesis/ecs_layer/ecs.hpp
    src/genesis/ecs_layer/components.hpp
    src/genesis/ecs_layer/transform_system.cpp
    # 资源
    src/genesis/resource_layer/asset_manager.hpp
    src/genesis/resource_layer/asset_manager.cpp
    # 渲染
    src/genesis/render_layer/shader_library.hpp
    src/genesis/render_layer/shader_library.cpp
    src/genesis/render_layer/ibl_baker.hpp
    src/genesis/render_layer/ibl_baker.cpp
    src/genesis/render_layer/render_system.hpp
    src/genesis/render_layer/render_system.cpp
    # 脚本
    src/genesis/script_layer/script_system.hpp
    src/genesis/script_layer/script_system.cpp
    # UI
    src/genesis/ui_layer/ui_service.hpp
    src/genesis/ui_layer/ui_service.cpp
)

target_include_directories(GenesisEngine PUBLIC
    src
    src/genesis
    src/external
    src/external/sol2
    src/external/lua
    ${Vulkan_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
)

target_link_libraries(GenesisEngine PUBLIC
    GenesisFoundation
    GenesisApp
    GenesisExternal
    ${Vulkan_LIBRARIES}
    ${ASSIMP_LIBRARIES}
)

#-----------------------------------------------------------
# 着色器构建 (双后端: SPIR-V + 扁平 GLSL)
#-----------------------------------------------------------
if (GENESIS_BUILD_DEMO2)
    set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/demo2/shaders)
    set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
    file(GLOB SHADER_SOURCES ${SHADER_SRC_DIR}/*.vert ${SHADER_SRC_DIR}/*.frag)

    set(SHADER_OUTPUTS "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
        set(GLSL ${SHADER_OUT_DIR}/${SHADER_NAME}.glsl)
        add_custom_command(
            OUTPUT ${SPV} ${GLSL}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_shader.py
                    ${SHADER} ${SHADER_OUT_DIR} ${GLSLANG_VALIDATOR}
            DEPENDS ${SHADER} ${SHADER_SRC_DIR}/common.glsl ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_shader.py
            COMMENT "building shader ${SHADER_NAME} (spv + glsl)"
        )
        list(APPEND SHADER_OUTPUTS ${SPV} ${GLSL})
    endforeach()

    add_custom_target(GenesisShaders ALL DEPENDS ${SHADER_OUTPUTS})

    add_subdirectory(src/demo2)
endif()

#-----------------------------------------------------------
# 旧版 demo1 (macOS 相机滤镜)
#-----------------------------------------------------------
if (GENESIS_BUILD_DEMO1 AND APPLE)
    add_subdirectory(src/demo1)
endif()
