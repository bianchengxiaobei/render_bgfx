# SG-7 全 Target

> 目标：在 Lit/Unlit 之外落地 Fullscreen / Canvas(UI) / CustomRT，并把无渲染支撑的 Terrain/UITK/VFX 明确标注为不支持。
> 前置：SG-6（节点覆盖）、SG-3（图材质运行时）。
> 参考：Unity Target 实现；hdrp-bgfx 的 PostProcessChain/RenderGraph/UI pass。

---

## 1. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG7-1 | Target 注册表与能力元数据 | `target_registry.cpp`、`/api/catalog/targets` | `targets.md` §1/§7 | UI 能正确禁用不支持 Target |
| SG7-2 | Fullscreen Target：全屏材质 pass | `targets/fullscreen.cpp`、`shaders/shadergraph/fullscreen_*` | `Editor/Generation/Targets/Fullscreen/FullscreenSubTarget.cs:99/132/294/513` | 自定义全屏效果截图 |
| SG7-3 | Fullscreen 接入 PostProcessChain | `post_process_chain.*` 扩展点 | `src/render/postprocess/post_process_chain.h:71/86/353`、`src/render/graph/render_graph.h:121` | 插入位置/开关可配置；关闭零成本 |
| SG7-4 | Canvas/UI Target：2D quad/sprite 绘制 | `targets/canvas.cpp`、`shaders/shadergraph/ui_*` | `Editor/Generation/Targets/Canvas/CanvasSubTarget.cs:26/87/96-98`、`BuiltInCanvasSubTarget.cs:8/23` | alpha blend/裁剪截图 |
| SG7-5 | Canvas 绘制路径 | `src/render/ui/*`（新增） | `src/app/debug_ui.cpp:320`、`shaders/ui/imgui.*`、`PassId::Ui` | quad/sprite 正确排序/混合 |
| SG7-6 | CustomRT Target：离屏 RT 资产 | `targets/crt.cpp`、`CrtSystem` | `Editor/Generation/Targets/CustomRenderTexture/CustomTextureSubTarget.cs:10/24/37-38` | 单 pass 渲染到 RT 截图 |
| SG7-7 | CRT 更新模式/清屏/预览 | `CrtSystem` | `render_graph.h:104/121/122` | realtime/on-demand；双缓冲 |
| SG7-8 | Target 设置/校验/预算 | `target_registry.cpp` | `targets.md` §2/§9 | 非法设置/超预算报错 |
| SG7-9 | 不支持目标：Terrain/UITK/VFX | UI gating + Known limits | `Editor/Generation/Targets/UITK/UISubTarget.cs:20/74`、`Terrain/ITerrainSubTarget.cs`、`VFXTarget.cs:17/46` | 无法选择；原因可见 |
| SG7-10 | 每 Target 测试/golden | `tests/shadergraph/targets/` | `AGENTS.md` | D3D11/Vulkan 截图/回归 |
| SG7-11 | 文档/清单回填 | `targets.md`/checklist | - | 能力矩阵与实测一致 |

## 2. Fullscreen

- Blocks：Base Color、Alpha；可选 Eye Depth（写深度时）。
- 输入：UV/ScreenPosition；可采样 scene color/depth（按 Target 设置）。
- 渲染状态：ZTest/ZWrite/Blend/Stencil 由 Target/材质设置决定。
- 接入：在 `PostProcessChain::record` 的可配置位置插入一个自定义全屏 pass；不改变现有 effects-off 基线（开关关闭时零 pass）。
- 需求：全屏 quad/三角形、`u_global_screenSize`、分辨率/resize 处理。
- 限制：不支持任意多 pass、MRT、Compute；v1 单 pass。

## 3. Canvas / UI

- Blocks：Base Color、Emission、Alpha。
- 现有渲染器只有 ImGui 的 UI 程序（`PassId::Ui` + `shaders/ui/imgui.*`），不是通用 2D 绘制；需要新增最小 UI 绘制路径（quad/sprite、纹理、颜色、UV、混合）。
- 队列：透明、按提交顺序；ZWrite off；alpha blend；裁剪矩形（可选）。
- 限制：不实现 UGUI/UITK 组件语义、遮罩/Stencil 完整兼容、9-slice；文档写明。

## 4. Custom Render Texture

- 需要新增：
  - RT 资产描述：尺寸、格式（RGBA8/RGBA16F）、mip、wrap/filter、更新模式（realtime/on-demand）、初始清屏色。
  - `CrtSystem`：创建/更新/双缓冲、每帧或按需渲染、读取/预览。
  - RenderGraph pass：图材质渲染到 RT；不与 swapchain 冲突。
- 限制：v1 只支持 2D 单 pass；不支持 3D/array/cubemap、feedback 多重更新、材质动画。

## 5. 不支持目标

| Target | 处理 |
|---|---|
| Terrain | Target 注册为 `unsupported`，UI 置灰，reason: 渲染器无地形系统 |
| UI Toolkit | `unsupported`，reason: Unity 运行时专属 |
| VFX | `deferred`，reason: 现有 VFX 使用独立 shading pack；后续评估 |

- 直接编辑 JSON 选择不支持 Target → 加载报 `SG_TARGET_UNSUPPORTED`，不崩溃。
- 文档 Known limits 必须列出。

## 6. 验收

```powershell
# Fullscreen
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\targets\fullscreen.gltf --frames=3 --screenshot=fs_d3d.png

# Canvas
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=tests\shadergraph\targets\ui.gltf --frames=3 --screenshot=ui_d3d.png

# CRT
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=tests\shadergraph\targets\crt.gltf --frames=3 --screenshot=crt_d3d.png
```

通过标准：

- 每个可用 Target 有 D3D11/Vulkan 截图与验收记录。
- 关闭新 Target 时现有场景 golden 0 diff。
- 不支持 Target 无法选择；JSON 直接使用时报错可读。
- 预算/stage/变体校验在各 Target 生效。

## 7. Known limits（初始）

- Fullscreen 单 pass；无 MRT/Compute。
- Canvas 仅最小 2D 绘制；无 UGUI/UITK 兼容。
- CRT 仅 2D 单 pass；无 3D/array/cubemap。
- Terrain/UITK/VFX 不支持。
- 各 Target 的高级材质特性（Decal/ClearCoat 等）不在 v1。

## 8. 对齐参考

- Unity Target：`Editor/Generation/Targets/Fullscreen/FullscreenSubTarget.cs:99/132/294/513`、`Canvas/CanvasSubTarget.cs:26/87/96-98`、`BuiltIn/.../BuiltInCanvasSubTarget.cs:8/23`、`CustomRenderTexture/CustomTextureSubTarget.cs:10/24/37-38`、`UITK/UISubTarget.cs:20/74/85-87`、`Terrain/ITerrainSubTarget.cs`、`VFXTarget.cs:17/46`。
- Block 定义：`Editor/Generation/TargetResources/BlockFields.cs:9/21`。
- hdrp-bgfx：`src/render/postprocess/post_process_chain.h:71/86/353`、`src/render/graph/render_graph.h:104/121/122`、`src/app/debug_ui.cpp:320`、`shaders/ui/imgui.vs.sc`/`imgui.fs.sc`、`src/render/material/shader_pass.cpp:44`。
