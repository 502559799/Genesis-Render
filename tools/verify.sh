#!/bin/bash
# Genesis 引擎完整验证脚本: 编译 -> 自检 -> 双后端渲染 -> 图像对比
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BUILD=$ROOT/build-v2
VERIFY=$ROOT/verify
XVFB=(xvfb-run -a -s "-screen 0 1400x900x24")
DEMO="$BUILD/src/demo2/demo2_pbr --assets $ROOT/assets --shaders $BUILD/shaders"

echo "==== [1/4] 编译 ===="
cmake --build $BUILD 2>&1 | tail -1 || exit 1

echo "==== [2/4] 逻辑自检 (ECS/模型加载/纹理/脚本/数学) ===="
$DEMO --selftest || exit 1

echo "==== [3/4] 双后端渲染验证 ===="
mkdir -p $VERIFY
run_demo() {
    # $1: 名称后缀, 其余: 参数
    local name=$1; shift
    "${XVFB[@]}" $DEMO "$@" --headless --frames 30 --screenshot $VERIFY/$name.png > $VERIFY/$name.log 2>&1
    if [ -s "$VERIFY/$name.png" ]; then
        echo "  [OK] $name.png"
    else
        echo "  [FAIL] $name 未生成截图, 见 $VERIFY/$name.log"
        tail -5 $VERIFY/$name.log
        exit 1
    fi
}

run_demo vulkan_pbr --backend vulkan
run_demo opengl_pbr --backend opengl
run_demo vulkan_blinnphong --backend vulkan --lighting blinnphong
run_demo vulkan_noibl --backend vulkan --no-ibl
run_demo vulkan_noskybox --backend vulkan --no-skybox
# 直接光隔离对比对 (PBR vs BlinnPhong, 无 IBL)
run_demo vulkan_pbr_noibl --backend vulkan --no-ibl
run_demo vulkan_blinnphong_noibl --backend vulkan --no-ibl --lighting blinnphong

echo "==== [4/4] 图像指标对比 ===="
python3 $ROOT/tools/compare_screenshots.py
