project(demo2_pbr)

add_executable(demo2_pbr main.cpp)

target_include_directories(demo2_pbr PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/genesis
    ${CMAKE_SOURCE_DIR}/src/external
    ${CMAKE_SOURCE_DIR}/src/external/sol2
    ${SDL2_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(demo2_pbr PRIVATE
    GenesisEngine
    GenesisApp
    GenesisExternal
    GenesisFoundation
    ${Vulkan_LIBRARIES}
    ${ASSIMP_LIBRARIES}
)

if (UNIX AND NOT APPLE)
    target_link_libraries(demo2_pbr PRIVATE dl pthread SDL2::SDL2)
else()
    target_link_libraries(demo2_pbr PRIVATE SDL2::SDL2)
endif()

add_dependencies(demo2_pbr GenesisShaders)
