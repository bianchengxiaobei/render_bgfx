# hdrp-bgfx M0 实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 本文件只覆盖 M0.1–M0.7；M1 见 [m1-checklist.md](m1-checklist.md)。
> 总体方案与数据契约见 [implementation-plan.md](implementation-plan.md)。
> 源码优先：实现前先读 HDRP/Core/bgfx 源码。

## 文档结构

- `m0-checklist.md`：工程骨架、gfx 封装、IDL、glTF、Forward PBR、基础光照/阴影、调试设施。
- [m1-checklist.md](m1-checklist.md)：RenderGraph、FrameSettings、ViewState、DrawList、Prepass、光照 v1、阴影 v1、Sky/IBL、最终输出、Debug。
- M2 及后续模块新建 `m2-checklist.md` 等独立清单，不回写到 M0/M1。

## 状态

| 里程碑 | 状态 | 摘要 |
|---|---|---|
| M0.1 工程/窗口 | `[x]` | CMake、GLFW、D3D11/Vulkan 窗口、resize、caps 日志 |
| M0.2 gfx 封装 | `[x]` | 句柄池、Texture/Buffer/Uniform/Program/FrameBuffer/Encoder/ResourceManager、两帧延迟销毁、泄漏告警 |
| M0.3 IDL/codegen | `[x]` | `config/uniforms/*.idl` → C++ binder + shader 声明；`hbg_idl_check` |
| M0.4 glTF 资产层 | `[x]` | cgltf、贴图、材质、节点/相机/`KHR_lights_punctual`、MikkTSpace、sidecar |
| M0.5 Forward PBR | `[x]` | manifest shader、MaterialSystem、HDR target、ACES tonemap、`--scene/--render-test/--frames/--screenshot` |
| M0.6 光照/阴影 v0 | `[x]` | LightData 4×32、1 方向光 + 31 punctual、方向光阴影、PCF 3×3 |
| M0.7 调试/验收设施 | `[x]` | Dear ImGui 面板、Profiler、wireframe、F1/F11/F12、双后端截图 |

## 当前验证

- `hbg_tests` 86 个用例、15842 assertions；`hbg_idl_check` 通过。
- D3D11/Vulkan `--gfx-test`：`draw=2 triangles=24`；DamagedHelmet 基础前向渲染通过。
- M0.5–M0.7 的资产、光照/阴影 v0、DebugUI、截图和 ResourceManager 泄漏检查见下节。

## M0.5–M0.7 验证细节

- D3D11/Vulkan `--gfx-test`：`draw=2 triangles=24`。
- DamagedHelmet：`draw=4 textures=13 triangles=15453`（DebugUI 开启时 draw +1）。
- 阴影场景覆盖方向光、PCF、double-sided；点光/聚光外部 glTF 场景通过。
- DebugUI 面板显示 backend/caps、FPS/CPU/GPU、draw/compute、texture/FBO/program 内存、Shadows/Wireframe/Freeze time/Render scale/Debug view、截图、RenderDoc 状态。
- RenderDoc 堵塞：`renderdoc.dll` 未加载，F11 仅告警；本机存在 `E:\Learn\renderdoc` 源码/工具目录但未接入运行环境。
- 外部临时场景：`%TEMP%\hbg_shadow_scene\shadow.gltf`、`point.gltf`、`spot.gltf`、`shadow_instanced.gltf`；`%TEMP%\hbg_damaged_helmet\DamagedHelmet.gltf`。

## 共享约定

- M1 的渲染契约、HDRP `file:line` 索引、已知限制和完整回归命令见 [m1-checklist.md](m1-checklist.md)。
- 构建/测试命令统一维护在 [m1-checklist.md#常用命令](m1-checklist.md#常用命令)，避免两份文档漂移。
