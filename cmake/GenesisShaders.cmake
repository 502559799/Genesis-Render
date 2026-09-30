# ============================================================================
#  Genesis Render - shader header generation
#
#  Shaders are authored once as GLSL in src/genesis/renderer/shaders and turned
#  into two variants by scripts/build_shaders.py:
#     * GLSL 410 source  -> embedded as a C string   (OpenGL backend)
#     * SPIR-V (Vulkan 1.1) -> embedded as uint32_t[] (Vulkan backend)
#     * MSL 2.1 (SPIRV-Cross) -> embedded as a C string   (Metal backend)
#
#  The generated files are committed to the repository, so a machine without
#  Python or the Vulkan SDK can still build the engine.  Set
#  GENESIS_REBUILD_SHADERS=ON to regenerate them at configure/build time.
# ============================================================================

function(genesis_configure_shaders)
    set(GENESIS_SHADER_DIR ${CMAKE_SOURCE_DIR}/src/genesis/renderer/shaders)
    set(GENESIS_SHADER_GEN_DIR ${GENESIS_SHADER_DIR}/generated)

    # Interface target that exposes the generated headers.
    add_library(GenesisShaderHeaders INTERFACE)
    target_include_directories(GenesisShaderHeaders INTERFACE
        ${GENESIS_SHADER_GEN_DIR}
        ${GENESIS_SHADER_DIR})

    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang glslc
                 HINTS ENV VULKAN_SDK PATH_SUFFIXES bin Bin)
    find_program(SPIRV_CROSS_EXECUTABLE NAMES spirv-cross
                 HINTS ENV VULKAN_SDK PATH_SUFFIXES bin Bin)
    set(_spirv_cross_args "")
    if(SPIRV_CROSS_EXECUTABLE)
        set(_spirv_cross_args --spirv-cross ${SPIRV_CROSS_EXECUTABLE})
    endif()

    set(_want_rebuild OFF)
    if(GENESIS_REBUILD_SHADERS STREQUAL "ON")
        set(_want_rebuild ON)
    elseif(GENESIS_REBUILD_SHADERS STREQUAL "AUTO")
        if(Python3_FOUND AND GLSLANG_VALIDATOR)
            set(_want_rebuild ON)
        endif()
    endif()

    if(_want_rebuild)
        if(NOT Python3_FOUND)
            message(FATAL_ERROR "GENESIS_REBUILD_SHADERS requires Python3")
        endif()
        if(NOT GLSLANG_VALIDATOR)
            message(FATAL_ERROR "GENESIS_REBUILD_SHADERS requires glslangValidator or glslc (Vulkan SDK)")
        endif()
        message(STATUS "Shaders: regenerating with ${GLSLANG_VALIDATOR}")

        # Run once at configure time so that the GLOB in the main CMakeLists
        # picks up newly generated files immediately.
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
                    --shader-dir ${GENESIS_SHADER_DIR}
                    --out-dir ${GENESIS_SHADER_GEN_DIR}
                    --glslang ${GLSLANG_VALIDATOR} ${_spirv_cross_args}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _shader_result
            OUTPUT_VARIABLE _shader_output
            ERROR_VARIABLE _shader_error)
        if(NOT _shader_result EQUAL 0)
            message(WARNING "Shader generation failed (using committed headers):\n${_shader_output}\n${_shader_error}")
        else()
            message(STATUS "${_shader_output}")
        endif()

        # Also expose a build target so shaders can be regenerated explicitly.
        file(GLOB_RECURSE GENESIS_SHADER_SOURCES CONFIGURE_DEPENDS
             ${GENESIS_SHADER_DIR}/*.vert ${GENESIS_SHADER_DIR}/*.frag
             ${GENESIS_SHADER_DIR}/*.comp ${GENESIS_SHADER_DIR}/*.h)
        add_custom_target(GenesisShaders ALL
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
                    --shader-dir ${GENESIS_SHADER_DIR}
                    --out-dir ${GENESIS_SHADER_GEN_DIR}
                    --glslang ${GLSLANG_VALIDATOR} ${_spirv_cross_args}
            DEPENDS ${GENESIS_SHADER_SOURCES} ${CMAKE_SOURCE_DIR}/scripts/build_shaders.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Regenerating shader headers (GLSL + SPIR-V)"
            VERBATIM)
    else()
        message(STATUS "Shaders: using committed generated headers "
                       "(set -DGENESIS_REBUILD_SHADERS=ON to regenerate)")
        if(NOT EXISTS ${GENESIS_SHADER_GEN_DIR}/genesis_shaders_generated.cpp)
            message(WARNING "Generated shader sources are missing: run "
                            "python3 scripts/build_shaders.py")
        endif()
    endif()
endfunction()
