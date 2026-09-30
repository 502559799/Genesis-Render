# 功能验证说明（怎么验证 + 效果图）

三层验证：

1. **自动化测试**：`./build.sh --test [--validation]`。像素级断言覆盖每项能力，GPU 用例在 OpenGL / Vulkan / Metal 上各跑一遍，最后两组场景做跨后端逐像素比对，结果见 `docs/TEST_REPORT.md`。
2. **效果图**：由引擎自己的截图路径生成（`Engine::saveScreenshot`，含 UI），位于 `docs/images/`。
3. **交互验证**：`genesis_demo` 的菜单 Scene 与各功能面板。

## 1. 逐能力验证方法

| 能力 | 交互入口 | 自动化用例 | 效果图 |
|---|---|---|---|
| 三后端 | `--backend gl\|vulkan\|metal`；Engine 面板显示后端、设备、深度格式、深度范围 | 全部 GPU 用例 × 3；`cross-backend` pbr + features | `features/<gl\|vk\|mtl>_*` |
| macOS Vulkan 启动 | `genesis_demo --backend vulkan` | `rhi.device_creation_and_limits` | `features/vk_*` |
| 纯脚本建景（RTTI） | 菜单 Scene → Built by Lua；`--scene lua`；Lua console 示例 | `lua.script_builds_and_renders_a_scene_through_rtti`、`lua.bundled_scene_scripts_run_without_errors`、`reflection.*` | `features/*_lua_none.png` |
| CommandBuffer 自由绘制 | CommandBuffer lab；Lua console "Persistent overlay command buffer" | `commandbuffer.render_texture_draw_mesh_and_image_orientation`、`commandbuffer.rect_2d_uses_top_left_pixel_coordinates` | `features/*_showcase_commandbuffer.png` |
| 可编程渲染管线 | CommandBuffer lab → "replace the forward pipeline"（F5），效果 A/B、画中画、分屏；`--scene commandbuffer` 按 C/E/M | `commandbuffer.custom_pipeline_replaces_the_forward_pipeline`、`commandbuffer.blit_effects_transform_the_image` | `features/*_showcase_custom.png`、`features/*_commandbuffer_none.png` |
| 逻辑 / 渲染分离 | 所有 lab 命令都在 `DemoApp::tick` 录制、渲染阶段执行 | `command_buffer.recording_copies_arguments_and_names_roundtrip`、`reflection.methods_record_command_buffer_commands` | — |
| 渲染到纹理 | 安防摄像头（显示器网格 + 面板缩略图） | `material.reflected_edits_textures_and_render_textures`、`commandbuffer.sampling_the_current_target_is_skipped` | 所有 showcase 截图左侧显示器 |
| 材质系统 + 实时编辑预览 | Material editor（选择 Gold / Car paint / Frosted glass / Neon / Checker / Toon，拖动参数，换贴图或 RT，Assign to selected） | `material.reflected_edits_textures_and_render_textures`、`render.live_material_edit_updates_pixels`、`reflection.properties_enums_and_read_only` | `features/*_showcase_material.png` |
| SDF 文字 2D | SDF text lab（内容、字号、描边、对齐、字号阶梯） | `text.sdf_2d_text_is_placed_and_outlined` | `features/*_showcase_text.png` |
| SDF 文字 3D | SDF text lab（公告牌 / 旋转 / 深度测试）；场景内材质球标签（TextComponent） | `text.sdf_3d_text_billboard_depth_and_components` | 同上 |
| 反射 Inspector | Hierarchy / Inspector（任意组件编辑、增删组件） | `reflection.*` | `features/*_showcase_inspector.png` |
| UI 方向 | 三后端截图 UI 均正向 | `ui.imgui_is_upright_in_captured_frames` | 所有 features 截图 |
| PBR / IBL / tonemap / 模型导入 | Render settings；showcase 场景 | `pbr.*`、`ibl.*`、`render.*`、`gltf.*`、`fbx.*` | `<gl\|vk\|mtl>_pbr_grid_*`、`*_tonemap_*`、`*_model_*`、`*_skybox_ibl_env` |

## 2. 效果图清单

### 2.1 功能截图（`docs/images/features/`，每个后端 7 张）

| 文件 | 生成命令 | 内容 |
|---|---|---|
| `*_showcase_material.png` | `--scene showcase --lab material` | showcase 场景 + 材质编辑器（实时预览 RT、反射参数） |
| `*_showcase_commandbuffer.png` | `--lab commandbuffer` | 安防摄像头 RT、后处理与自定义管线控制、命令缓冲列表 |
| `*_showcase_custom.png` | `--lab custom` | 自定义管线：edge 效果链 + 画中画原图 + 管线说明文字 |
| `*_showcase_text.png` | `--lab text` | 2D 文字（描边、字号阶梯）、旋转 3D 文字、SDF 图集 |
| `*_showcase_inspector.png` | `--lab inspector` | 实体树 + 头盔的反射组件编辑 |
| `*_lua_none.png` | `--scene lua` | 完全由 `scene_builder.lua` 通过 RTTI 搭建的场景 + Lua HUD |
| `*_commandbuffer_none.png` | `--scene commandbuffer` | `commandbuffer_demo.lua`：RT 显示器 + 画中画 + HUD |

重新生成：

```bash
for b in gl vulkan metal; do
  ./build/bin/genesis_demo --backend $b --scene showcase --lab material --screenshot out_$b.png
done
```

### 2.2 能力证明图（`docs/images/<gl|vk|mtl>_*.png`，每个后端 15 张）

`./build/bin/genesis_demo --backend <b> --showcase docs/images` 以 headless 方式生成：PBR 材质网格（Cook-Torrance / Blinn-Phong × 直接光 / IBL）、三种色调映射、天空盒 + IBL、调试线、glTF / GLB / FBX 模型特写。

## 3. 自己复现

```bash
./build.sh --clean --test                    # 编译 + 124 个测试 + 跨后端比对
./build.sh --test --validation               # 三后端校验层
./build/bin/genesis_demo --backend vulkan --selftest   # 自动走一遍所有场景 / 面板 / 交互路径
./build/bin/genesis_demo --backend vulkan    # 交互验证（菜单 Scene / Windows）
./build/bin/genesis_demo --backend metal --scene lua
./build/bin/genesis_demo --backend gl --scene commandbuffer
```

交互检查建议：

1. Material editor 选 "Gold"，拖动 roughness，预览与场景中的金球同步变化；shading 改为 unlit、blend 改为 transparent 立即生效；base_color 槽选 "render texture: security camera"，球面上出现实时画面。
2. CommandBuffer lab 勾选 "replace the forward pipeline"，切换 effect A/B，打开 split view，左半为原图、右半为效果。
3. SDF text lab 调字号到 160 px，边缘保持锐利；3D 文字取消 depth test 后可穿过物体显示。
4. Lua console 选择示例 "Spawn a sphere (RTTI)" 运行，场景中出现带标签的新球，Inspector 中可继续编辑。
5. 菜单 Scene 在四个场景之间切换，Engine 面板中 lua errors 保持为 0。
