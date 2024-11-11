project(DEMO1)


if (VULKAN_DEBUG_REPORT)
    add_definitions(-DVULKAN_DEBUG_REPORT)
endif()

add_executable(DEMO1 
graphics/genesis_imgui.hpp
graphics/genesis_imgui.cpp
graphics/gpu_device.hpp
graphics/gpu_device.cpp
graphics/gpu_resources.hpp
graphics/gpu_enum.hpp
main.cpp)

set_property(TARGET DEMO1 PROPERTY CXX_STANDARD 17)

target_compile_definitions(DEMO1 PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(DEMO1 PRIVATE
    .
    ..
    ../genesis
    ${Vulkan_INCLUDE_DIRS}
)

target_include_directories(DEMO1 PRIVATE
        ${SDL2_INCLUDE_DIRS})


target_link_libraries(DEMO1 PRIVATE
        GenesisFoundation
        GenesisExternal
        GenesisApp
        ${Vulkan_LIBRARIES}
    )

    target_link_libraries(DEMO1 PRIVATE
    dl
    pthread
    SDL2::SDL2)