#!/bin/bash
# Genesis Engine v2.0 Linux 构建脚本
# 依赖: cmake>=3.16, ninja(可选), SDL2, VulkanSDK(or libvulkan-dev), assimp, glslang-tools, python3
set -e
cd "$(dirname "$0")"
BUILD_DIR=build-v2
cmake -S . -B $BUILD_DIR -DCMAKE_BUILD_TYPE=${1:-RelWithDebInfo}
cmake --build $BUILD_DIR -j$(nproc)
echo ""
echo "构建完成: $BUILD_DIR/src/demo2/demo2_pbr"
echo "运行示例: ./$BUILD_DIR/src/demo2/demo2_pbr --backend vulkan --assets ./assets --shaders ./$BUILD_DIR/shaders"
echo "完整验证: bash tools/verify.sh"
