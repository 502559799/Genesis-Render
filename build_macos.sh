#!/bin/bash
# Genesis Engine v2.0 macOS 构建脚本
# 依赖: cmake, SDL2(brew install sdl2), VulkanSDK/MoltenVK, assimp(brew install assimp),
#       glslang(brew install glslang), python3
set -e
cd "$(dirname "$0")"
BUILD_DIR=build-v2
cmake -S . -B $BUILD_DIR -DCMAKE_BUILD_TYPE=${1:-RelWithDebInfo}
cmake --build $BUILD_DIR -j$(sysctl -n hw.ncpu)
echo ""
echo "构建完成: $BUILD_DIR/src/demo2/demo2_pbr"
echo "运行示例: ./$BUILD_DIR/src/demo2/demo2_pbr --backend vulkan --assets ./assets --shaders ./$BUILD_DIR/shaders"
echo "           (macOS 上 Vulkan 经由 MoltenVK; OpenGL 为 4.1 core profile)"
