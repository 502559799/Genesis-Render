#!/usr/bin/env python3
"""
Genesis 渲染正确性自动验证:
  1. 双后端一致性: Vulkan vs OpenGL 同场景截图对比 (PSNR / 平均绝对差)
  2. 直接光模型差异: PBR vs Blinn-Phong 必须有可测差异
  3. 间接光贡献: IBL on vs off 必须有可测差异 (且 on 更亮)
  4. 天空盒: on vs off 差异集中在天空区域
输出全部指标并给出 PASS/FAIL 判定
"""
import math
import os
import sys

from PIL import Image
import numpy as np

V = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "verify")


def stats(path):
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    return img


def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    if mse < 1e-10:
        return 99.0
    return 10 * math.log10(1.0 / mse)


def mean_abs_diff(a, b):
    return float(np.mean(np.abs(a - b)))


def mean_luma(a, region=None):
    if region:
        x0, y0, x1, y1 = region
        a = a[y0:y1, x0:x1]
    return float(np.mean(a @ [0.299, 0.587, 0.114]))


def non_bg_ratio(a):
    # 非近黑像素比例
    return float(np.mean(np.any(a > 0.05, axis=2)))


def main():
    results = []

    vk = stats(f"{V}/vulkan_pbr.png")
    gl = stats(f"{V}/opengl_pbr.png")
    vk_bp = stats(f"{V}/vulkan_blinnphong.png")
    vk_noibl = stats(f"{V}/vulkan_noibl.png")
    vk_nosky = stats(f"{V}/vulkan_noskybox.png")
    vk_pbr_noibl = stats(f"{V}/vulkan_pbr_noibl.png")
    vk_bp_noibl = stats(f"{V}/vulkan_blinnphong_noibl.png")

    #---------------- 1. 双后端一致性 ----------------#
    p = psnr(vk, gl)
    mad = mean_abs_diff(vk, gl)
    nbg_vk = non_bg_ratio(vk)
    ok = p > 20.0 and nbg_vk > 0.5
    results.append(("双后端一致性 (PSNR>20dB, 有效像素>50%)", f"PSNR={p:.2f}dB MAD={mad:.4f} 有效像素={nbg_vk*100:.1f}%", ok))

    #---------------- 2. PBR vs Blinn-Phong (直接光隔离) ----------------#
    band = (240, 260, 960, 660)
    d_bp = np.abs(vk_pbr_noibl[band[1]:band[3], band[0]:band[2]] -
                  vk_bp_noibl[band[1]:band[3], band[0]:band[2]]).mean(axis=2)
    strong = int((d_bp > 0.1).sum())
    ok = strong > 2000 and float(d_bp.max()) > 0.2
    results.append(("直接光模型可区分 (PBR vs BlinnPhong, 无IBL)", f"强差异像素={strong} (阈值>2000) max={d_bp.max():.3f}", ok))

    #---------------- 3. IBL 贡献 ----------------#
    luma_ibl = mean_luma(vk, (200, 200, 1000, 500))   # 球体区域
    luma_noibl = mean_luma(vk_noibl, (200, 200, 1000, 500))
    mad_ibl = mean_abs_diff(vk, vk_noibl)
    ok = luma_ibl > luma_noibl + 0.01 and mad_ibl > 0.01
    results.append(("IBL 间接光有贡献且提亮", f"luma(ibl)={luma_ibl:.3f} luma(no-ibl)={luma_noibl:.3f} MAD={mad_ibl:.4f}", ok))

    #---------------- 4. 天空盒 ----------------#
    sky_luma_on = mean_luma(vk, (0, 0, 1280, 150))
    sky_luma_off = mean_luma(vk_nosky, (0, 0, 1280, 150))
    ok = sky_luma_on > sky_luma_off + 0.05
    results.append(("天空盒渲染生效 (天空区域变亮)", f"sky(on)={sky_luma_on:.3f} sky(off)={sky_luma_off:.3f}", ok))

    print("=" * 72)
    print("Genesis 渲染功能验证报告")
    print("=" * 72)
    all_ok = True
    for name, detail, ok in results:
        verdict = "PASS" if ok else "FAIL"
        all_ok &= ok
        print(f"[{verdict}] {name}\n        {detail}")
    print("=" * 72)
    print("VERDICT:", "ALL PASS" if all_ok else "FAILED")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
