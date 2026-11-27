# hdrp-bgfx M2 实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 本文件只覆盖 M2.0–M2.9；M0 见 [m0-checklist.md](m0-checklist.md)，M1 见 [m1-checklist.md](m1-checklist.md)。
> 总体方案与 M3+ 路线见 [implementation-plan.md](implementation-plan.md)。
> 源码优先：实现前先读 HDRP/Core/bgfx 源码；参考根：HDRP `E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`；Core `E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`；bgfx `build\vs2022\_deps\bgfx-src`。

## 文档结构

- `m2-checklist.md`：Deferred 基础、GBuffer、Deferred Lighting、Cluster、SSAO/SSR/SSGI、Decals、Reflection Probes、验收。
- M3 及后续模块新建 `m3-checklist.md` 等独立清单，不回写到 M2。
- M1 的 forward 回归、HDRP `file:line`、已验证边界继续以 [m1-checklist.md](m1-checklist.md) 为准。

## 状态

| 里程碑 | 状态 | 摘要 |
|---|---|---|
| M2.0 Deferred 基础 | `[x]` | LitShaderMode/双路径、GBuffer 资源、MRT/stencil、CLI、帧顺序 |
| M2.1 GBuffer | `[x]` | 4 RT、Lit GBuffer pass、深度 prepass、stencil、清屏 |
| M2.2 Deferred Lighting | `[x]` | 全屏光照、GBuffer 解码、阴影/IBL、forward-only 物体 |
| M2.3 Cluster LightList | `[x]` | 64×64×32 深度簇、CPU 构建、透明/Forward 使用 |
| M2.4 SSAO | `[x]` | 半分辨率 GTAO-lite、空间去噪、全分辨率 bilateral upsample、ambient 应用 |
| M2.5 SSR | `[x]` | 全分辨率 trace、IBL fallback、stencil 合成、SSR debug |
| M2.6 SSGI | `[x]` | 半分辨率屏幕空间间接漫反射、fallback、合成 |
| M2.7 Decals | `[x]` | DBuffer 3RT、sidecar decal、GBuffer 合成 |
| M2.8 Reflection Probes | `[x]` | 静态探针、box projection、2 探针混合、天空 fallback |
| M2.9 验收/性能 | `[x]` | Debug、双后端、图验证、性能、文档回填 |

## 当前验证

- M2.0/M2.1/M2.2/M2.3/M2.4/M2.5/M2.6/M2.7/M2.8/M2.9 已实现；当前 `hbg_tests` 120 cases / 17465 assertions，`ctest` 2/2，`hbg_idl_check` 通过；M1 forward 基线为 86/15842。
- M2.1 已验证 D3D11/Vulkan 4RT GBuffer、normal/normal-map/double-sided、alpha 裁剪、bad-tangent derivative fallback、`--render-scale=0.5`。
- M2.2 已验证 D3D11/Vulkan deferred 直光/IBL/方向光+point/spot 阴影、transparent、unlit/forward-only、0.5 render scale；forward/deferred 多光源场景 PSNR 58.46 dB，Uffizi+IBL 64.00 dB，基础球 67.39 dB。
- M2.3 已验证 64×64×32 CPU cluster、随机灯/FPTL 覆盖对照、1080p 纹理 990×544（2,154,240 B）、transparent/forward-only 使用 cluster 且与 FPTL 逐像素一致；D3D11/Vulkan debug slice 截图一致。
- M2.0 开始后每个任务必须补测试、HDRP `file:line`、边界和验证结果；未完成前保持 `[ ]`。
- M2 的首个可验收目标不是全功能，而是：Deferred 基础路径在 D3D11/Vulkan 上无 validation error，且 forward 回归不退化。

## 关键架构事实

- **双路径**：M2 保留 M1 forward 路径作为回归基线；`pipeline.json` 支持 `both`，`FrameSettings::LitShaderMode` 选择路径。M2.9 验收后 `config/frame_settings.json` sample/default 已切 `deferred`，`--lit-shader-mode=forward` 继续保留回归。
- **Deferred 帧顺序（规划）**：

```text
Shadow
Scene Clear（color + depth + stencil）
Depth Prepass（deferred 下 depth-only；normal 由 GBuffer 写）
Depth Pyramid
Objects Motion Vectors
Camera Motion Vectors
Sky（项目 sky pass 无深度测试，放在 GBuffer 前避免覆盖已有着色）
DBuffer Clear（启用 Decals 时）
Decal 0..N（每个 decal 一次全屏 box-clip draw）
GBuffer Clear
GBuffer
SSAO
Deferred Lighting
SSGI Trace
SSGI Denoise
SSGI Composite
SSR Trace + Composite
Forward-Only Opaque
Transparent
Wireframe
Presentation / Tonemap / Debug
```

- **GBuffer 格式（M2.1 最终契约）**：

| RT | 格式 | 内容 |
|---|---|---|
| RT0 | RGBA8 | baseColor.rgb + alpha |
| RT1 | RGBA8 | metallic, ambientOcclusion, materialFlags, 0 |
| RT2 | RGBA8 | 888 oct normal.rgb + perceptualRoughness.a |
| RT3 | RGBA16F | emissive.rgb + 0 |
| Depth | D24S8 | depth + stencil（deferred v1 必需） |

- **Deferred v1 强制 full depth prepass**：Lit alpha-test 在 `DepthOnly` prepass 完成；GBuffer 使用 depth equal、no depth write、不重复 alpha clip。M2 v1 将 `DepthPrepassWithDeferredRendering` 视为始终开启；关闭该字段不会切 partial prepass（partial prepass 顺延到 M2 之后）。
- **Stencil**：GBuffer 通过 depth test 的像素写 stencil ref bit0（requires deferred lighting）；Deferred Lighting 全屏 pass 只对 stencil==1 的像素执行，Forward-Only Opaque、Sky、Transparent 不被重复光照。
- **MRT 限制**：GBuffer 需要 4 color + depth；M2.0 必须检查 `caps.limits.maxFBAttachments`，不足时 fallback forward。D32F 无 stencil，deferred v1 不支持，fallback forward。
- **不使用 StorageBuffer/compute 依赖**：M2 先用 fullscreen fragment/CPU 构建（SSAO/SSR/SSGI、Cluster），不复制 HDRP 的 compute + UAV 路径；后续 M6 再评估 compute/GPU cluster。
- **采样器预算（deferred lighting）**：0–4 GBuffer/depth，5 lightData，6 方向阴影，7 lightList，8 shadowContext，9 punctual shadow，10 environment，11 AO，12/13 预留，14/15 probes；deferred shader 不得 include `material/surface_data.sc` 的 0–4 材质 sampler 声明，必须使用独立 GBuffer decode。
- **曝光**：Deferred Lighting 输出线性 HDR；曝光/tonemap 仍在 M1.9 的最终 pass，不在 deferred lighting 中重复应用。
- **颜色/天空**：项目 sky pass 当前无深度测试；deferred 下必须放在 GBuffer 前。若后续改成 depth-tested sky，再调整为 HDRP 的“opaque 后 sky”顺序。
- **HDRP 不是逐行移植**：HDRP 的 GBuffer2/SSS/NormalBuffer/DBuffer 4RT、compute light list、temporal SSR/SSGI、realtime probe 不在 M2 v1 范围内；M2 以双后端可验证、forward 对齐优先。

## 任务依赖

- `M2.0 → M2.1 → M2.2`：Deferred 基础、GBuffer、Deferred Lighting 必须按顺序。
- `M2.0 → M2.3`：Cluster 可在 Deferred 主路径之外并行。
- `M2.1 → M2.7`：Decals 依赖 GBuffer 与 full prepass。
- `M2.2 → M2.4/M2.5/M2.6/M2.8`：屏幕空间效果和探针需要已工作的 Deferred Lighting。
- `M2.9` 最后执行；每个功能完成时先补对应 debug/验证，不等 M2.9。

## M2.0 Deferred 基础

### 实现计划

- `config/pipeline.json`：`supportedLitShaderMode` 增加/启用 `both`；`config/frame_settings.json` 的 `litShaderMode` 仍为 `forward`，M2.9 再切默认。
- `ForwardRenderer` 增加 `LitShaderMode` 分支（M2 先不强行拆类）：`render()` 选择 `recordForwardFrame()` 或 `recordDeferredFrame()`；公共的 shadow/prepass/motion/depth-pyramid/sky/presentation 复用。
- 新增 GBuffer 资源容器：`std::array<gfx::TextureHandle, 4> gbuffer`、复用 scene depth、GBuffer clear FBO；`resize()` 释放/重建，`shutdown()` 无泄漏。
- `render_graph` 验证 4 color attachments + depth 的 FBO；`gfx::FrameBuffer` 检查 `maxFBAttachments`，不满足时记录 warning 并 fallback forward。
- 补 `RenderGraphBuilder::setViewSize(width,height)` / pass viewport：`RenderGraphContext::width/height` 使用 pass 尺寸而不是 graph 尺寸，供 SSAO/SSGI 半分辨率与 SSAO blur 使用；默认仍为 graph 尺寸。
- Deferred 下 `selectDepthFormat()` 强制 `D24S8`；`D32F`/`D16` 或无 stencil 时 fallback forward。
- 新增 `--lit-shader-mode=forward|deferred`（可加 `--deferred` 别名）；CLI 覆盖 `FrameSettings::litShaderMode`，无 deferred 能力时打印 fallback 原因。
- 新 `PassId`/`ShaderKey`/manifest 骨架：`GBuffer`、`GBufferClear`、`DeferredLighting`；M2.1/M2.2 再补 shader。
- 统一 deferred 帧顺序（见“关键架构事实”）；forward 帧顺序保持 M1，不改变 M1 金图。

### 验收

- [x] `pipeline.json=both` + `--lit-shader-mode=deferred` 能进入 deferred 分支；`--lit-shader-mode=forward` 仍走 M1 路径。
- [x] D3D11/Vulkan deferred 空场景/基础场景无 validation error；FBO 4RT+depth 创建成功。
- [x] `RenderGraph` pass viewport 可独立于 graph 尺寸；半分辨率 texture + view rect 正确（为 M2.4/M2.6 铺路）。
- [x] 不支持 MRT/stencil 时自动 fallback forward，并打印一次 warning。
- [x] M1 forward 相关用例全部通过；新增 M2 测试后总数增加，M1 行为不退化；`--gfx-test` 双后端通过。
- [x] Graph diagnostics 显示 deferred pass 顺序；无资源泄漏。

### 实现记录（2026-11-10）

- `config/pipeline.json` 改为 `supportedLitShaderMode=both`；`config/frame_settings.json` 仍为 `litShaderMode=forward`。
- `ForwardRenderer::deferredUnavailableReason()`：`src/render/passes/forward_renderer.cpp:123`，检查 lit mode、MSAA、`maxFBAttachments>=5`、`D24S8`、4 RT 格式。
- GBuffer 资源：`ensureGBufferTextures()`/`releaseGBufferTextures()`：`forward_renderer.cpp:159`、`:202`；RT0–2 `RGBA8`，RT3 `RGBA16F`，复用 scene depth。
- Deferred 分支：`render()` 判定 `forward_renderer.cpp:1804`；`recordDeferredFrame()` `:964`；`GBuffer Clear` `:899`；M2.0 时只清 GBuffer（4 color + depth），M2.1 已在其后插入 GBuffer 几何。
- Deferred 帧顺序：`Shadow -> Scene Clear -> Depth Prepass -> Depth Pyramid -> Objects MV -> Camera MV -> Sky -> GBuffer Clear -> GBuffer -> Deferred Lighting -> Forward Only Opaque -> Transparent -> Wireframe -> Tonemap`；Forward Only Opaque 仅在存在 unlit/ForwardOnly 物体时录制。
- `RenderGraphBuilder::setViewSize()`：`src/render/graph/render_graph.h:85`、`render_graph.cpp:82`；pass 上下文使用 pass 尺寸 `render_graph.cpp:866`；`passNames()` `:815` 供 deferred 顺序日志。
- CLI：`parseLitShaderMode()` `src/app/backend.cpp:95`，`--lit-shader-mode`/`--deferred` `:240`；`Application::loadSettings()` 应用到 FrameSettings `src/app/application.cpp:169`。
- Shader 骨架：`PassId::GBuffer/GBufferClear/DeferredLighting` `src/shader/shader_key.h:31`；manifest 可选读取 `src/shader/shader_library.cpp:166`、switch `:315`。
- 验证：`hbg_tests` 90 cases / 15862 assertions；`ctest` 2/2；`hbg_idl_check` 通过；D3D11/Vulkan deferred 2 帧、`--render-scale=0.5`、D32F fallback 均无 validation error/资源泄漏。
- M2.0 限制（已由 M2.1/M2.2 补上）：当时 deferred 仅验证 GBuffer 资源/FBO/顺序，画面为 scene clear；M2.1 已接入 GBuffer shader，M2.2 已接入光照。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.RenderGraph.cs:181-236`：AO/SSR/SSGI、deferred lighting、forward opaque、sky 的实际顺序。
- HDRP `RenderPipeline/Settings/FrameSettings.cs:116-120, 315-324, 371-372, 668-707, 769-781`：`DepthPrepassWithDeferredRendering`、`SSR/SSAO/SSGI/ReflectionProbe`、lit mode sanitize。
- HDRP `Material/Lit/Lit.shader:435-519`：GBuffer pass 入口与 depth/stencil 设置。
- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:482-645`：deferred 的 depth prepass/alpha test 分支。

### 简化边界

- M2 v1 不做 partial prepass、MSAA、deferred split lighting/SSS、D32F stencil 替代方案。
- 不重命名 `ForwardRenderer`；若 M2.2 后文件超过可维护阈值，再单独制定重构任务，不在 M2 内顺手重构。

## M2.1 GBuffer

### 实现计划

- 新增 `shaders/passes/gbuffer_clear.fs.sc`：fullscreen MRT clear；RT0=黑、RT1=(0,1,0,0)、RT2=`hbgEncodeNormalBuffer((0,0,1), 1.0)`、RT3=黑；禁止同时清深度/颜色。
- 新增 `shaders/passes/gbuffer.fs.sc`：复用 `forward.vs.sc` 的 varying；从 `hbgMakeSurfaceData` 取 surface；输出 4 个 MRT；不计算光照。
- MRT 写入：RT0 baseColor/alpha；RT1 metallic/AO/materialFlags；RT2 `hbgEncodeNormalBuffer(normalWS, perceptualRoughness)`；RT3 emissive。
- `ShaderPass::GBuffer` 的 opaque draw list；Lit/alphaMode 生成默认 pass mask 已包含 `GBuffer`/`DepthOnly`，M2 只需补录制和过滤。
- deferred 下 prepass 改为 depth-only：`normal` 不再由 prepass 写；GBuffer 的 RT2 就是后续 SSAO/SSGI/normal debug 的 normal buffer。
- GBuffer pass：depth test equal、no depth write、no blend；通过 depth test 的 fragment 写 stencil bit0；alpha-test 已在 full prepass 完成，GBuffer 不重复 `discard`。
- `ClearGBuffers` 仅控制 GBuffer clear；M2 v1 默认始终 clear。DBuffer 是独立 RT，不在 GBuffer clear 中处理。
- 16 个现有材质 variant（normal/mask/alpha clip/double sided）统一编译；deferred GBuffer 可复用同 key，但 alpha clip bit 不改变执行路径（由 prepass 决定）。

### 验收

- [x] D3D11/Vulkan GBuffer 4RT 输出无格式/附件错误；normal decode 在双侧法线、normal map、无 tangent fallback 下正确。
- [x] Alpha-test 物体在 prepass 裁剪后 GBuffer 不再写入背景像素；opaque 无 alpha clip 泄漏。
- [x] Depth equal/no write 生效；GBuffer 之后深度值不改变。
- [x] Forward-Only/Unlit opaque 不进入 GBuffer，留给 M2.2 的 forward-only pass。
- [x] `--render-scale=0.5` GBuffer 尺寸使用 render 尺寸，presentation 使用 target 尺寸。
- [x] 新增 GBuffer 单测/图像验证；`hbg_idl_check` 和现有测试通过。

### 实现记录（2026-11-10）

- `shaders/passes/gbuffer_clear.fs.sc`：fullscreen MRT clear；RT0/RT3 清零、RT1 `(0,1,0,0)`、RT2 `hbgEncodeNormalBuffer((0,0,1),1.0)`；`src/render/passes/forward_renderer.cpp:899` 录制，`renderGBufferClear()` `:1238`。
- `shaders/passes/gbuffer.fs.sc`：复用 `forward.vs.sc` varying 与 `hbgMakeSurfaceData`；RT0 baseColor/alpha、RT1 metallic/AO/0/0、RT2 `hbgEncodeNormalBuffer`、RT3 emissive；不计算光照、不重复 alpha clip。
- `tools/shader_build.py:111` 编译 16 个 GBuffer 变体与 `gbufferClear`，manifest key `gbuffer`/`gbufferClear`。
- GBuffer draw list：`forward_renderer.cpp:680`，只收 `ShaderPass::GBuffer` 的 Lit opaque（含 alpha mask/double sided），排除 Unlit/ForwardOnly/transparent。
- GBuffer pass：`addGBufferPass()` `:927`、`renderGBufferPass()` `:1408`；4 color + depth，depth equal/no write，stencil bit0 replace（`src/gfx/state.h:27`、`encoder.cpp:54`）。
- Deferred prepass 改为 depth-only：`addPrepassPass()` `:772`、`renderPrepass()` `:1285`，不再分配 normal buffer；GBuffer RT2 作为 `--debug-view=normal` 来源（`normalSource` `:1052`）。
- `shaders/passes/forward.vs.sc:11`、`forward_instanced.vs.sc:12` 对零 tangent 不再直接 `normalize`，保留零值以触发 `surface_data.sc` 的 derivative fallback；bad-tangent normal-map 球体在 GBuffer RT2 中恢复正确扰动。
- M2.2 接入时修正 `shaders/material/normal_buffer.sc` 的 888 解码高低 4bit 顺序（`hiY = high >> 4`、`hiX = high & 15`）；此前 normal buffer 会把世界法线错误重排，导致 deferred 出现三角形色块。
- GBuffer 清屏在 sky 后、GBuffer 前：`recordDeferredFrame()` `:964`（调用 `:971`）；deferred 顺序日志包含 `GBuffer Clear -> GBuffer`。
- 验证：D3D11/Vulkan 4RT GBuffer、球体 normal/normal map/double-sided、bad-tangent derivative fallback、quad alpha 半裁剪、alpha factor 0、`--render-scale=0.5`（640×360 GBuffer / 1280×720 presentation）；deferred/forward depth 截图逐像素一致；`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；`hbg_idl_check` 通过。
- 当前限制：M2.1 本身尚未接光照；该限制已由 M2.2 解决，`--debug-view=normal` 仍可查看 RT2。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:721-793`：`SetupGBufferTargets`、MRT 绑定、normal buffer/GBuffer2/3、clear。
- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:827-855`：`RenderGBuffer` 只对 deferred/opaque 执行。
- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:482-645`：full depth prepass、alpha test、motion vector 顺序。
- HDRP `Material/Lit/Lit.shader:435-519`；`Material/MaterialGBufferMacros.hlsl`；`Core/ShaderLibrary/Packing.hlsl:57/71` 法线/材质打包。

### 简化边界

- M2 v1 只支持 Lit opaque GBuffer；不写 clear coat/SSS/light layers/decal layers。
- 无法线贴图/无 tangent 的 mesh 沿用 M1 derivative fallback；不新增材质类型。
- GBuffer 不输出 motion vector；运动矢量仍由 M1 prepass/motion pass 负责。

## M2.2 Deferred Lighting

### 实现计划

- 新增 `shaders/passes/deferred_lighting.fs.sc`：复用 fullscreen triangle；从 depth 重建 world position，从 GBuffer 解码 `SurfaceData`；用 stencil 跳过非 deferred 像素。
- 抽出/复用 direct light 评估：方向光、FPTL LightData、ShadowContext、PCF；ambient 由 SH9/IBL 提供；M2.2 使用 GBuffer RT1.g 的材质 AO，SSAO 的 stage 11 在 M2.4 接入。SSGI 是 M2.6 的后置合成，不进入 deferred lighting。
- 输出到 HDR sceneColor（线性 HDR）；不在此 pass 做 exposure/tonemap；透明物体不进入 deferred lighting。
- Deferred 下 opaque Lit 只走 GBuffer + deferred；opaque ForwardOnly/Unlit 在 deferred lighting 之后用 forward pass 重绘；transparent 继续走 M1 forward transparent。
- `ForwardRenderer::buildDrawLists` 拆分：GBuffer list、forward-only opaque list、transparent list、prepass list；避免 Lit opaque 同时进入 forward 和 GBuffer。
- 场景颜色顺序：Sky（GBuffer 前）→ GBuffer → deferred lighting → SSGI trace/denoise/composite → SSR trace/composite → forward-only opaque → transparent；sky pass 当前无深度测试，必须保持这个顺序。
- 新增 deferred graph pass 和资源依赖：`deferredLighting` 读取 GBuffer/depthPyramid/lightData/shadow/lightList/environment，写入 sceneColor；AO/SSGI 分别在 M2.4/M2.6 接入。

### 验收

- [x] 无 SSAO/SSR/SSGI/Decals/Probes 时，deferred 与 forward 在同一场景的 direct lighting 差异在约定阈值内：基础球 67.39 dB / RMSE 0.109；多光源+阴影 58.46 dB / RMSE 0.304；Uffizi+IBL 64.00 dB / RMSE 0.161；normal-map 球 67.34 dB / RMSE 0.109；0.5 render scale 59.46 dB / RMSE 0.271。
- [x] 方向光 + point/spot + 级联/点光阴影在 deferred 下正确；stencil 不照到 forward-only/unlit/sky。多光源场景 11 个 shadow request（4 方向级联 + 6 point face + 1 spot），forward/deferred 58.46 dB。
- [x] Deferred 下 opaque Lit、transparent、unlit 均可见；无重复光照/漏光。transparent 场景 forward/deferred 逐像素一致；unlit pair 58.77 dB，GBuffer=1、ForwardOnly=1。
- [x] 曝光/tonemap 只在最终 pass 执行；HDR 值范围正常。deferred lighting 输出线性 HDR，不采样/应用 exposure 参数。
- [x] D3D11/Vulkan graph 顺序和截图一致；无 shader/uniform 错误。两后端 pass order 相同；light scene D3D/Vulkan 92.38 dB，sky 57.72 dB（IBL 过滤差异）。

### 实现记录（2026-11-10）

- 新增 `shaders/passes/deferred_lighting.fs.sc`：fullscreen triangle；GBuffer 解码后复用 `hbgEvaluateLightLoop`；输出 `lighting + emissive` 到 HDR sceneColor；不做 exposure/tonemap。
- 采样约定：GBuffer 用 `vec2(u, 1-v)` 采样（匹配 3D pass 的 GBuffer 行）；depth pyramid 用 `(u,v)` 采样；world position 用 `(u,v)` 的 NDC 重建。该组合在 D3D11/Vulkan、0.5 render scale 下与 forward 对齐；不要单独改动其中一个 UV。
- 为避免 deferred shader 引用材质 sampler 0–4：拆出 `shaders/material/surface_data_types.sc`、`shaders/lighting/direct_lighting.sc`、`shaders/lighting/gbuffer_decode.sc`；stage 0–3 GBuffer、4 depth pyramid、5 lightData、6 方向阴影、7 lightList、8 shadowContext、9 punctual shadow、10 environment，和 M2 契约一致。
- Stencil：`src/gfx/state.h:37` 新增 `kDeferredLightingStencil`（`TEST_EQUAL`、ref/mask bit0、失败/通过都 KEEP）；`renderDeferredLighting()` `forward_renderer.cpp:1329` 只 attach depth 作 stencil、采样 depthPyramid，避免 D3D11 DSV/SRV 冲突。
- Graph：`addDeferredLightingPass()` `forward_renderer.cpp:965` 读 GBuffer/depthPyramid/lightData/lightList/shadow/environment，写 sceneColor，depth attach；`recordDeferredFrame()` `:1038` 顺序 `GBuffer -> Deferred Lighting -> Forward Only Opaque -> Transparent`。
- Draw list：新增 `m_forwardOpaqueDrawList`（`kForwardOnlyOpaquePassMask`，`shader_pass.h:35`）；`addForwardPass(..., forwardOnly=true)` 在 deferred lighting 后重绘 unlit/ForwardOnly；修正 transparent list 重建前重置 passTags 的顺序问题。
- Shader manifest：`tools/shader_build.py` 编译 `deferredLighting`（D3D11/Vulkan 各 1 个），`ShaderLibrary::loadManifest()` 将 `deferredLighting` 视为必需 entry；manifest 26 passes/backend，无缺失文件。
- 同时修正 `shaders/material/normal_buffer.sc` 888 解码高低 4bit 顺序；该 bug 会让 GBuffer normal 在 deferred 中产生三角形色块。
- 验证：`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；`hbg_idl_check` 通过；双后端 `--gfx-test` 通过。D3D11/Vulkan deferred 2 帧、空场景/基础球/多光源+shadow/transparent/unlit/normal-map/0.5 scale 截图通过；pass order 与 forward/deferred parity 见验收。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.LightLoop.cs:884-993`：`RenderDeferredLighting` 的 GBuffer、depth、shadow、light list 绑定。
- HDRP `Lighting/LightLoop/Deferred.compute:150-194`：从 GBuffer 解码、LightLoop、写 diffuse/specular lighting。
- HDRP `RenderPipeline/HDRenderPipeline.RenderGraph.cs:221-236`：deferred lighting → forward opaque → sky 的顺序。
- HDRP `Lighting/LightLoop/LightLoopDef.hlsl`、`LightLoop.hlsl`：FPTL 取样、阴影/环境光评估。

### 简化边界

- 不做 HDRP 的 split diffuse/specular、SSS、clear coat、light layers、shadow mask。
- 不做 compute deferred；fullscreen fragment 是 M2 的正式路径。
- Deferred v1 只对 deferred stencil 像素光照；forward-only/transparent 仍由 M1 forward 处理。
- M2 v1 不新增真正的 Unlit shader；现有 unlit material 仍复用 forward lit shader，但走 `Forward Only Opaque`，不进入 GBuffer，也不会被 Deferred Lighting 重复照亮。

## M2.3 Cluster LightList

### 实现计划

- 新增 `config/uniforms/cluster.idl`：`params`（tileSize=64、tileCountX、tileCountY、numSlices=32）与 `depthParams`（near、far、logBase、enabled）；不改 M1 `PassUniforms` 240B/9。
- `LightListBuilder` 增加 cluster 构建：64×64 tile × 32 log-depth slices；每个 cluster 1 count + 最多 32 个绝对 GpuLight index；`R32_UINT` 2D atlas，`width=33*tileCountX`、`height=tileCountY*numSlices`。
- CPU 构建使用 light 的屏幕 AABB + view-space depth range；方向光继续排除；point/spot 按 range 与 cluster 深度相交裁剪。log slice 使用 `logBase=1.0404`（HDRP 64-slice `1.02` 在 32-slice 下的等效几何分布）。
- `Shaders/light_data.sc`/`light_loop.sc` 增加 cluster 取样：屏幕 tile 64×64，`hbgLinearDepth()` 映射到 log slice；stage 7 复用 lightList sampler；`u_cluster_depthParams.w` 切换 FPTL/cluster 布局。
- 使用策略：Deferred/Forward opaque 继续 FPTL；forward transparent 和 forward-only opaque 使用 cluster；`--no-light-cluster` 强制二者回退 FPTL，用于回归对照。
- Debug `LightCluster` 为 64×64×32 cluster 占用/切片视图：`-2` FPTL 16×16 legacy、`-1` 每像素深度 slice、`0..31` 固定 slice；通过 `--debug-light-cluster-slice=N` 或 ImGui slider 切换。

### 验收

- [x] CPU cluster 与逐灯暴力求交结果一致（随机 16 灯/FPTL 64×64 覆盖 union + 每 slice depth range 单测）。
- [x] 1080p cluster 纹理尺寸/内存符合预算：`30×17 tiles × 32 slices`，纹理 `990×544`，`2,154,240 B`；每 cluster 最多 32 灯，日志输出 `overflow clusters`（v1 总灯上限 32，实际为 0）。
- [x] Transparent scene 使用 cluster 后灯数减少但视觉与 brute-force 参考一致；D3D11/Vulkan cluster vs `--no-light-cluster` 均逐像素一致；forward opaque/deferred 仍为 FPTL，M2.2 多光源 forward/deferred 仍逐像素一致。
- [x] Debug LightCluster 可切换 slice/legacy tile；D3D11/Vulkan 同 slice 截图逐像素一致。
- [x] 无 cluster 时透明路径回退 FPTL，不崩溃（`--no-light-cluster`）。

### 实现记录

- `src/render/lighting/gpu_light_data.h`：`kClusterTileSize=64`、`kClusterSliceCount=32`、`GpuLightClusterData`。
- `src/render/lighting/light_list_builder.*`：FPTL + cluster 同一 build；cluster texture 布局、`clusterSliceForDepth()`、`clusterLogBase()=1.0404`、随机覆盖单测。
- `config/uniforms/cluster.idl` / generated binders：`u_cluster_params`、`u_cluster_depthParams`。
- `src/render/passes/forward_renderer.*`：`ensureClusterLightListTexture()`、`uploadClusterLightList()`；transparent/forward-only 绑定 cluster，其它 pass 绑定 FPTL；`setLightClusterEnabled()`；debug slice 控制。
- `shaders/common/light_data.sc`：`hbgClusterSliceForDepth()`、`hbgLightClusterTileCoord()`、cluster count/index。
- `shaders/lighting/light_loop.sc`：cluster branch 优先，FPTL 次之，最后 full punctual fallback。
- `shaders/passes/debug_light_cluster.fs.sc`：per-pixel/fixed slice + legacy FPTL 视图。
- 测试：`tests/render/test_light_list.cpp` 新增 cluster 尺寸、depth slice、随机 FPTL union 对照；`tests/gfx/test_uniform_layout.cpp`、`tests/app/test_backend.cpp` 更新。
- 验证：`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；D3D11/Vulkan `--gfx-test`；1080p cluster 日志；transparent cluster/FPTL 两后端逐像素一致；debug slice D3D/Vulkan 逐像素一致。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.LightLoop.cs:665-700`：`BuildGPULightList` 入口。
- HDRP `Lighting/LightLoop/LightDefinitions`（`LightDefinitions.cs.hlsl:57`）、`LightLoop.cs:101-110`：`TILE_SIZE_CLUSTERED=32`、`TILE_SIZE_BIG_TILE=64`、`k_Log2NumClusters=6`。
- HDRP `Lighting/LightLoop/lightlistbuild-clustered.compute:256-500`：深度 backplane、cluster depth slice、coarse list 与 layered offset。
- HDRP `Lighting/LightLoop/LightLoopDef.hlsl:265-320`：cluster index、count/start、light 取值。
- HDRP `Lighting/LightLoop/ClusteredUtils.hlsl:6-111`：`SnapToClusterIdxFlex`、`ClusterIdxToZFlex`、`GenerateLayeredOffsetBufferIndex`。
- HDRP `Lighting/LightLoop/LightLoop.cs:561-565,1941-1949`：`k_Log2NumClusters`、`k_ClustLogBase=1.02`、`g_fClustScale` 上传。

### 简化边界

- CPU cluster，不是 HDRP compute cluster；无 coarse light grid、depth fine pruning、per-voxel clusters、light categories、layered offset buffer。
- 固定 32 depth slices、64×64 tile、最多 32 灯/cluster；不做动态分辨率/异步构建。
- HDRP 实际 clustered tile 为 32×32、64 slices、log base 1.02；hbg v1 用 64×64、32 slices、1.0404 保持同类的近密远疏分布，并按 M2 计划固定。
- Deferred opaque 仍使用 FPTL；不在 M2 中把 deferred 改成 cluster。

## M2.4 SSAO

### 实现计划

- 新增 `config/uniforms/ssao.idl`：`params=(radius,intensity,bias,thickness)`、`params2=(falloff,stepCount,halfRes,enabled)`。
- 新增 `shaders/passes/ssao.fs.sc`：半分辨率 GTAO-lite，8 directions × 6 steps（每方向正负各 3 步），depth + normal 重建 view position，按 normal-plane horizon occupancy 累积遮蔽；使用 M2.0 的 pass viewport 输出单通道 AO。
- 新增 `shaders/passes/ssao_blur.fs.sc`：半分辨率 5×5 bilateral spatial denoise（depth + normal 权重）；不做 temporal/history。
- 新增 `shaders/passes/ssao_upsample.fs.sc`：全分辨率 2×2 bilateral upsample，输出 full-res AO；Deferred Lighting 以 stage 11 采样该 AO。这样可以避免直接双线性上采样在深度/法线边缘产生 bleeding。
- AO 乘到 ambient diffuse/specular/IBL；forward-only/transparent M2 v1 不加 SSAO。
- 支持 `pipeline.json supportSSAO`、`FrameSettingsField::SSAO`、`--ssao`/`--no-ssao`；未启用时绑定 1×1 白纹理，deferred shader 使用 AO=1。
- Debug view `SSAO`（已接入 `DebugDisplayMode` 与 `--debug-view=ssao`）。

### 验收

- [x] 半分辨率 AO 上采样无深度/法线 bleeding（全分辨率 bilateral upsample 生效）；远景/近景 radius 合理。boxes 场景的接触线在 debug view 中为连续灰线，未出现跨轮廓 halo。
- [x] AO 只影响 ambient/indirect，不影响直接光；关闭 SSAO 时 deferred lighting 走 `u_ssao_params2.w=0`、AO=1 分支，不执行 SSAO graph pass。
- [x] 无 NaN/黑屏；depth/normal 边界处有 clamp；D3D11/Vulkan AO debug 差异约 91.34 dB；AO on/off 差值图 D3D/Vulkan 约 98.08 dB。
- [x] `supportSSAO=false` 或 `FrameSettings::SSAO=false` 时不执行 SSAO pass，graph 数减少：640×360 时 30 pass → 27 pass；320×180 时 28 → 25。
- [x] `--ssao` CLI、`ssao_settings`/uniform/CLI 单测、boxes/D3D11/Vulkan 图像验证。

### 实现记录（2026-11-10）

- `config/uniforms/ssao.idl` 与 `src/render/postprocess/ssao_settings.*`：AO 参数、默认值（radius 2、intensity 2、bias 0.01、thickness 2、step 6、half-res）、sanitize 与 `ssaoResolution()`。
- `shaders/passes/ssao.fs.sc`：半分辨率；从 depthPyramid mip0 用 `invViewProj` + `view` 重建 view position，从 GBuffer RT2 解 normal；8 方向 × 6 步屏幕深度采样；normal-plane occupancy 累积，输出 R8 visibility。`texture2DLod(...,0)` 避免动态循环的导数告警。
- `shaders/passes/ssao_blur.fs.sc`：半分辨率 5×5 bilateral；depth 用相对容差、normal 用 `smoothstep(0.80,0.98,dot)`，避免跨表面模糊。
- `shaders/passes/ssao_upsample.fs.sc`：全分辨率 2×2 bilateral upsample；depth/normal 权重来自 full-res depth + GBuffer normal；输出 full-res R8 AO。
- `shaders/passes/deferred_lighting.fs.sc`：stage 11 `s_ssao`；`surfaceData.ambientOcclusion *= ssao`，所以只影响 `hbgEvaluateAmbient` 的 ambient/IBL，不进入 direct light。
- `src/render/passes/forward_renderer.*`：`addSSAOPasses()` 生成 `SSAO -> SSAO Blur -> SSAO Upsample`，位于 `GBuffer` 后、`Deferred Lighting` 前；`R8`（不可用时 RGBA8）；`m_ssaoFallback` 1×1 白纹理；`--ssao`/`--no-ssao`；`--debug-view=ssao`。
- `src/app/*`、`src/render/debug/debug_display.*`：SSAO 开关、debug 枚举与 CLI；`--debug-view=ssao` 自动打开 SSAO。
- `tools/shader_build.py`/`shaders/manifest.json`：新增 `ssao`、`ssaoBlur`、`ssaoUpsample`、`debugSsao` entry；`ShaderLibrary` 将它们视为必需 pass。
- 验证：`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；双后端 `--gfx-test`；1080p AO 为 960×540 trace + 1920×1080 upsample；boxes 场景 640×360 D3D11/Vulkan AO debug 91.34 dB、on/off 差值 98.08 dB；640×360 graph 30（开）/27（关），320×180 28/25；D3D/Vulkan 无 shader/uniform/validation 错误。
- 实现边界：参考 HDRP GTAO 的 depth/normal、半分辨率、bilateral 和 upsample 结构，但 v1 用 normal-plane occupancy 代替完整 horizon arc integration；不做 HDRP temporal/history、bent normal、RTAO。

### HDRP/Core 源码索引

- HDRP `Lighting/ScreenSpaceLighting/HDRenderPipeline.AmbientOcclusion.cs:147-188`：AO 总入口/history。
- HDRP 同文件 `:204-250`：GTAO horizon search/integration；`:253-312` spatial denoise；`:410-450` bilateral upsample。
- HDRP `Lighting/ScreenSpaceLighting/GTAO.compute`、`GTAOCommon.hlsl`、`GTAOBlurAndUpsample.compute`：GTAO 采样与去噪。
- HDRP `ShaderVariablesAmbientOcclusion.cs/.hlsl`：AO 参数布局。

### 简化边界

- 半分辨率 trace + 5×5 空间去噪 + 全分辨率 bilateral upsample；无 temporal accumulation，不做 HDRP 的 history、GTAO bent normal、ray-traced AO。
- GTAO-lite 是屏幕方向深度/法线采样的 normal-plane occupancy，不是 HDRP 完整 horizon arc integration；保留 8×6 采样和 depth thickness。hbg 的 `stepCount` 是每像素总步数（正负各一半），HDRP 的 `stepCount` 是单侧步数。
- 只影响 deferred opaque 的 ambient；forward-only/transparent M2 v1 不加 AO。
- 不实现 SSAO 的 volume 参数、逐材质强度、specular occlusion；AO 强度是统一参数。

## M2.5 SSR

### 实现计划

- 新增 `config/uniforms/ssr.idl`：maxDistance、maxSteps、thickness、roughnessFade、edgeFade、intensity、enabled。
- `shaders/passes/ssr_trace.fs.sc`：从 deferred opaque 后的 sceneColor 读取颜色，从 depthPyramid/GBuffer normal 重建世界空间射线；32 步 world-distance march + 深度 crossing/thickness 命中（M2 v1 不实现 HDRP Hi-Z 全逻辑）。
- `shaders/passes/ssr_composite.fs.sc`：按 fresnel/roughness/屏幕边缘权重把 reflectionColor 以 additive blend 写到 sceneColor；不采样 sceneColor，避免读写同一纹理；miss 保持已有 deferred IBL，不额外叠加环境（避免双计）；trace/composite 都用 stencil 跳过 forward-only/非 deferred 像素。
- SSR 在 Deferred Lighting 之后、forward-only/transparent 之前执行；读取 sceneColor 但写入独立 reflection RT，避免读写同一纹理。
- 支持 `pipeline.json supportSSR`、`FrameSettingsField::SSR`、`--ssr`/`--no-ssr`；关闭时跳过 trace/composite 和 SSR 纹理分配。
- Debug view `SSR`；M2.5 已接入 `DebugDisplayMode` 与 `--debug-view=ssr`。

### 验收

- [x] 金属/光滑材质能看到屏幕内反射；屏幕外/背面由现有 deferred IBL 承接，不出现黑洞。
- [x] 射线命中深度验证、thickness/crossing、屏幕边缘 fade 与 roughness fade 生效；无 temporal，无历史依赖。
- [x] Transparent 不进入 SSR（M2 边界）；opaque forward-only 不支持 SSR；unlit pair on/off 逐像素一致。
- [x] SSR 关闭时 graph/pass 完全跳过：Uffizi 场景 43 pass（开）/41 pass（关）；deferred 与 M2.2/M2.4 基线一致。
- [x] D3D11/Vulkan SSR debug 约 90.24 dB；roughness 0.65 场景 SSR on/off 逐像素一致（无命中/无 fallback 额外项）；无 NaN/越界。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.LightLoop.cs:1118-1450`：SSR 入口、depth/normal/color pyramid 绑定、trace/reprojection/accumulation。
- HDRP `Lighting/ScreenSpaceLighting/ScreenSpaceReflections.compute:139-227, 279-460, 640-671`：sample weight、hit infos、depth pyramid 射线、accumulation。
- HDRP `Lighting/ScreenSpaceLighting/ScreenSpaceTracing.hlsl:36-...`：hit 结构/proxy raycast 约定。

### 实现记录

- `config/uniforms/ssr.idl`：`u_ssr_params=(maxDistance,maxSteps,thickness,roughnessFade)`、`u_ssr_params2=(edgeFade,intensity,enabled,unused)`；32 B/2 descriptors。
- `src/render/postprocess/ssr_settings.*`：默认 maxDistance=40、maxSteps=32、thickness=0.6、roughnessFade=0.25、edgeFade=0.1、intensity=1；sanitize 到 1..64 steps 等边界。
- `shaders/passes/ssr_trace.fs.sc`：全分辨率；从 GBuffer0/1/2 取 baseColor/metallic/roughness/normal，从 depthPyramid + invViewProj 重建世界位置；反射方向按 `reflect(-V,N)`，32 步 world-distance march，`delta>0 && delta<thickness` 或前后深度 crossing 命中；命中采样 sceneColor。法线/金属/粗糙度/SSAO AO 输入与 deferred 一致。
- `shaders/passes/ssr_composite.fs.sc`：additive blend 到 sceneColor；shader 只读独立 SSR RT，不读 sceneColor；`ssr_trace`/`ssr_composite` 都绑定 depth 并 `kDeferredLightingStencil` 跳过 forward-only/透明/天空。
- `src/render/passes/forward_renderer.*`：`addSSRPasses()` 生成 `SSR -> SSR Composite`，位于 Deferred Lighting 后、Forward Only Opaque 前；SSR RT 全分辨率、`selectHdrFormat()`；新增 `m_ssrSampler`、`m_ssrBinder`、`m_ssrUniforms`、`setSsrSettings`。
- `src/app/*`、`src/render/debug/debug_display.*`、`src/shader/*`、`tools/shader_build.py`、`shaders/manifest.json`：`--ssr`/`--no-ssr`、`--debug-view=ssr` 自动开启、SSR/SSRComposite/DebugSSR shader 与 manifest。
- 验证：Uffizi smooth metal 场景 D3D11 SSR on/off RMSE 3.282 / 37.81 dB（反射可见），D3D11/Vulkan debug 90.24 dB，on 图 D3D/VK 58.39 dB；roughness 0.65 boxes on/off **0 RMSE**；unlit pair on/off **0 RMSE**；D3D11 graph 43（开）/41（关），D3D11/Vulkan `--gfx-test` 通过。

### 简化边界

- 无 HDRP 的前帧 color pyramid、reprojection、temporal accumulation、SSR speed rejection。
- 不做透明 SSR、clear coat、SSR 与反射探针的完整层级；additive composite 不替换现有 deferred IBL，miss 直接保留 IBL，仅命中额外加屏幕反射。
- 线性/深度 crossing 的 ray march，不等价于 HDRP 的完整 Hi-Z；无 blue noise/roughness lobe 重要性采样；后续 M6 再优化。
- 全分辨率 trace，不是 HDRP 的 Hi-Z compute；`maxDistance=40` 与 32 步在近场会产生步长/厚度误差，必要时通过 `ForwardRenderer::setSsrSettings()` 调整。

## M2.6 SSGI

### 实现计划

- 新增 `config/uniforms/ssgi.idl`：`params=(intensity,maxDistance,maxSteps,rayCount)`、`params2=(falloff,thickness,halfRes,enabled)`、`params3=(ambientBias,0,0,0)`；`SsgiSettings` 默认 intensity=1、maxDistance=30、maxSteps=8、rayCount=32、falloff=1、thickness=0.5、ambientBias=0.5、halfRes=true。
- 新增 `shaders/passes/ssgi_trace.fs.sc`：半分辨率 diffuse ray march，32 条 cosine-weighted rays × 8 步；深度/法线从 depthPyramid/GBuffer2 重建；从当前帧 deferred lighting 后的 sceneColor 取 hit radiance。
- 新增 `shaders/passes/ssgi_denoise.fs.sc`：半分辨率 9×9 depth-aware 空间去噪；`shaders/passes/ssgi_composite.fs.sc` 做全分辨率 9×9 空间/深度加权 upsample，再对 deferred stencil 像素 additive 写 sceneColor；不采样 sceneColor，输出 clamp 8。
- fallback：有 sky 时用 SH9；无 sky 时用 `u_pass_ambientColor`。trace 输出 `max(averageRadiance - ambient * ambientBias, 0)`，默认 ambientBias=0.5，避免完全双计 ambient，同时开放区域仍有半幅 fallback、不产生黑块。
- 顺序：`Deferred Lighting -> SSGI Trace -> SSGI Denoise -> SSGI Composite -> SSR Trace -> SSR Composite -> Forward Only Opaque -> Transparent`；SSGI 只加 deferred 像素，forward-only/transparent/sky 不加。
- 支持 `pipeline.json supportSSGI`、`FrameSettingsField::SSGI`、`--ssgi`/`--no-ssgi`；关闭时跳过三个 pass 和 raw/denoise 纹理分配。
- Debug view `SSGI`（`--debug-view=ssgi` 自动开启）已在 M2.6 实现；当前 debug 模式表见 `src/render/debug/debug_display.h`。

### 验收

- [x] 有遮挡的室内/角落出现间接漫反射；红墙/白 box 场景可见红色 bounce，开放区域回退 ambient/SH 不产生黑块。
- [x] 半分辨率 trace + 9×9 denoise + 9×9 upsample 无严重边缘泄漏；final clamp 生效；无 NaN。
- [x] SSGI 只加 deferred 像素，不影响 sky/transparent/forward-only；forward-only stencil 不通过。
- [x] 关闭 SSGI 时 graph 跳过；deferred 基线保持。
- [x] D3D11/Vulkan 差异、性能记录：1280×720 on/off RMSE 0.06597 / 23.61 dB；D3D/VK on 图 69.59 dB、debug 69.46 dB、off 102.13 dB；graph 42（开）/39（关），SSR+SSGI 44，forward 37（on/off 逐像素一致）；1080p trace 960×540，gpuFrame=5。

### HDRP/Core 源码索引

- HDRP `Lighting/ScreenSpaceLighting/ScreenSpaceGlobalIllumination.compute:26-329`：`SampleHemisphereCosine`、`RayMarch`、hit reproject、fallback hierarchy。
- HDRP `Lighting/ScreenSpaceLighting/RayMarching.hlsl:5-139`：depth-pyramid ray march、thickness/crossing、screen bounds。
- HDRP `Lighting/ScreenSpaceLighting/HDRenderPipeline.ScreenSpaceGlobalIllumination.cs:172-313`：trace/project/upscale 结构；`:400-468` temporal + diffuse denoise；`:470-517` `RenderSSGI`/`RenderScreenSpaceIndirectDiffuse`。
- HDRP `Lighting/ScreenSpaceLighting/RayMarchingFallbackHierarchy.cs:9-30`：ReflectionProbes/Sky/None fallback。
- HDRP `Lighting/GlobalIllumination.cs:21-151`：SSGI enable/half-resolution/ray steps/denoise/fallback 设置。

### 实现记录

- `config/uniforms/ssgi.idl`、`src/render/postprocess/ssgi_settings.*`、`SsgiUniformBinder`/`SsgiUniforms`（48B/3 descriptors）。
- `shaders/passes/ssgi_trace.fs.sc`：32 rays × 8 steps，rayCount 相比计划的 8 提高以压低无 temporal 时的噪声；`ssgi_denoise.fs.sc` + `ssgi_composite.fs.sc` 使用 9×9 空间核（计划 5×5 的扩展），无 normal 依赖的边缘加权由 depthPyramid 提供。
- `shaders/passes/debug_ssgi.fs.sc`；`DebugDisplayMode::SSGI`，debug 模式总数 15。
- `ForwardRenderer`：`m_ssgiBinder`/`m_ssgiRawSampler`/`m_ssgiSampler`、`ssgiRaw`/`ssgi` 纹理、`SSGI Trace/Denoise/Composite` pass、`addSSGIPasses()`、`renderSSGITracePass()`/`renderSSGIDenoisePass()`/`renderSSGICompositePass()`。
- 验证：`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；`hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test`；SSGI on/off、debug、SSR+SSGI、SSAO+SSGI 截图；forward 下 `--ssgi` 不创建 SSGI pass 且 on/off 逐像素一致。

### 简化边界

- 无 temporal accumulation、无历史验证、无 HDRP temporal/diffuse denoiser 多 pass；M2 用半分辨率 9×9 空间去噪 + 9×9 upsample。
- 后置加性合成，`ambientBias=0.5` 部分扣除已有 ambient，不完整替换 SH ambient；可能与 HDRP 的前帧 SSGI 视觉不同。
- 32 rays × 8 steps 为 v1 固定预算；无 blue-noise 采样、ray-traced GI、APV/probe volume、多层反射/多次反弹。
- 不做 reflection probe fallback hierarchy；有 sky 用 SH9，无 sky 用 pass ambient。

## M2.7 Decals

### 实现计划

- `config/uniforms/decal.idl`：`worldToDecal`/`decalToWorld`、`params=(opacity, angleFadeDegrees, normalBlend, textureFlags)`、`params2=(metallic, AO, roughness, unused)`、`baseColor`；176B/5 descriptors；每个 decal 一次 draw。
- DBuffer 3 RT：DBuffer0 albedo RGBA8、DBuffer1 normal RGBA8、DBuffer2 MAOS RGBA8；clear 到 `(0,0,0,1)`、`(0.5,0.5,0.5,1)`、`(0,0,0,1)`；deferred opaque only。
- `shaders/passes/decal.fs.sc`：全屏 quad，从 depthPyramid 重建世界坐标，按 decal 局部 box `[-0.5,0.5]` clip，投影 UV=`local.xz+0.5`，深度导数重建几何法线做 angle fade；每个 decal 一次全屏 draw。
- GBuffer fragment 采样 DBuffer（stage 11–13）合成 baseColor/normal/metallic/AO/roughness；新增 `GBufferDecals` shader 变体，仅在 decals 可用时选择，避免无 decal 时改变 M2.1。
- sidecar `.hdrp.json` 增加 `decals` 数组（`texture`、可选 `normalTexture`/`maskTexture`、`position`、`rotation`(Euler degrees)、`size`(width,height,depth)、`baseColor`、`opacity`、`angleFade`、`normalBlend`、`metallic`/`ao`/`roughness`、`enabled`）；不修改 glTF 核心格式。
- DBuffer pass 在 full prepass 后、GBuffer 前；DBuffer clear 与 GBuffer clear 独立。
- Debug view `Decals`（DBuffer0 albedo）与 sidecar/box/angle fade 单测；debug 模式总数 15。
- `supportDecals`/`FrameSettings::Decals`/`--decals`/`--no-decals`；`--debug-view=decals` 自动开启。侧车纹理相对 sidecar 目录加载并追加到 `RenderScene::images`。

### 实现记录

- `src/render/scene/decal.h/.cpp`：`Decal` 资产、`decalToWorld`/`decalWorldToDecal`、`decalForwardWorld`、`decalContainsWorld`、`decalAngleFade`；局部轴按 HDRP 习惯：X=width、Y=depth/projection、Z=height，`size` 为 `(width,height,depth)`。
- `src/asset/scene_loader.cpp`：解析 `decals` 数组和 sidecar 相对纹理；纹理走 `TextureLoader::loadFromFile`，追加 `ImageAsset`；`MaterialSystem` 为每个 decal 上传 albedo/normal/mask 三张纹理。
- `shaders/passes/decal.fs.sc`：`u_decal_*`、box clip、`normalTexture` 切线空间变换、`maskTexture` R=metallic/G=AO/B=roughness、coverage=`opacity*angleFade*albedo.a`；normal 权重=`normalBlend`；无 sky/far-depth 写入。
- `shaders/passes/decal_clear.fs.sc`、`shaders/lighting/decal.sc`、`shaders/passes/debug_decals.fs.sc`；`GBufferDecals` 变体（16 material variants）采样 DBuffer stage 11–13。
- `src/render/passes/forward_renderer.*`：`m_decalBinder`/`m_decalSamplers`/`m_decalAlbedoSampler`/`m_decalNormalSampler`/`m_decalMaskSampler`、3 张 DBuffer、`addDecalPasses()`、`renderDecalClear()`、`renderDecalPass()`、`updateDecalUniforms()`；DBuffer 必须在每次 `submit()` 前重新绑定纹理（bgfx `BGFX_DISCARD_ALL` 会丢弃先前绑定的 texture，这是实现中实际踩到的 bug）。
- `shaders/manifest.json`/`tools/shader_build.py`/`shader_library`：`decal`、`decalClear`、`gbufferDecals`、`debugDecals`。
- 验证：`hbg_tests` 112 cases / 17393 assertions；`ctest` 2/2；`hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test`；D3D/VK decal on/off、debug、single/multiple、overlap；forward `--decals` 不创建 DBuffer/Decal pass 且 on/off 逐像素一致。

### 验收

- [x] Sidecar 解析 decal 参数/贴图；无 decal 时 DBuffer 中性、GBuffer 与 M2.1 一致。无 decal `m27_off4` 与 M2.1 基线逐像素一致；sidecar 单测覆盖 3 张纹理和全部参数。
- [x] 单个/多个重叠 decal 的 albedo/normal/MAOS 合成正确；decal 不写到无深度表面。单个 decal graph 41、多个（3 个）graph 43；debug 只显示 DBuffer 覆盖区域，sky/far depth 不写入。
- [x] Deferred opaque 可见；forward-only/transparent 不支持（边界明确）。forward `--decals` graph 37 且 on/off 逐像素一致，明确不创建 DBuffer。
- [x] DBuffer 内存/带宽/clear 成本可接受；D3D11/Vulkan 一致。1280×720 DBuffer `3×1280×720×4=11,059,200 B`，1920×1080 `24,883,200 B`；D3D/VK on 图 101.09 dB、debug 逐像素一致；1080p gpuFrame=5（与关闭相同，粗略记录）。
- [x] 单测覆盖 sidecar、box 投影、角度 fade。`test_gltf`、`test_decal`、`test_uniform_layout`、`test_backend`、`test_debug_display` 覆盖。

### 指标

- D3D11/Vulkan decal on/off：RMSE `0.053227` / `25.4774 dB`；D3D/VK on 图 `101.0862 dB`，off 图 `102.1335 dB`，debug 逐像素完全一致。
- graph：3 decals 开 `43` / 关 `39`；单个 decal `41`；forward on/off 均为 `37` 且逐像素一致。
- 1080p：DBuffer `1920×1080`，`24,883,200 B`，`gpuFrame=5`；1280×720 为 `11,059,200 B`。
- 截图：`m27_on4.png`、`m27_off4.png`、`m27_single_on.png`、`m27_on_vk.png`、`m27_off_vk.png`、`m27_debug.png`、`m27_debug_vk.png`、`m27_1080_on.png`、`m27_1080_off.png`。

### HDRP/Core 源码索引

- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:1321-1341`：3/4RT DBuffer 格式与 clear color；`:1356-1447` `RenderDBuffer`/depth 绑定；`:1462-1512` `DBufferNormalPatch`（本项目未实现）。
- HDRP `Material/Decal/Decal.cs.hlsl:33-47`：`DecalData` 布局；`Decal.hlsl:52-104`：DBuffer encode/decode 与 3RT/4RT 宏。
- HDRP `Material/Decal/DecalData.hlsl:26-97`：albedo/mask 混合与 remap；`:107-154`：normal map/tangent space；`:158-159`：`MAOSBlend`。
- HDRP `Material/Decal/DecalSystem.cs:927-937`：fade/UV scale-bias 编码；`:1024-1046`：DBuffer 数据；`:1113-1135`：每个 decal 的 DBuffer draw。
- HDRP `Material/Decal/Decal.shader:116-155`：`DBufferProjector` 的 Cull/ZTest/blend/color mask；`DecalNormalBuffer.shader:56-128`：normal patch/stencil（未实现）。
- HDRP `Material/Decal/ShaderVariablesDecal.hlsl`、`DecalProperties.hlsl`：decal 常量/材质属性。

### 简化边界

- 3RT DBuffer；本项目把 DBuffer2 RGB 打包为 `(metallic, AO, roughness)`，而 HDRP 3RT 只存 smoothness、4RT 才分离 M/A；无 4RT per-channel mask、decal layer、VFX/transparent/forward decals、decal normal buffer patch。
- 每个 decal 用全屏 quad + 局部 box clip，而非 HDRP cube mesh + Cull Front/ZTest Greater；无 scissor，成本为 `O(decals × screen)`；max decals 未设硬上限。
- Angle fade 用 depth 导数重建的几何法线，0 表示关闭；无 decal prepass normal buffer，因此不像 HDRP 那样在复杂曲面上完全稳定。
- 只支持 sidecar 声明的 box decal；不解析 Unity DecalProjector 资产；decal 顺序为 sidecar 顺序，不做排序/聚类。
- Decal 只修改 GBuffer；不参与 SSR/SSGI/探针特殊路径，后续统一由 deferred lighting 着色；无 temporal/history。

## M2.8 Reflection Probes

### 实现

- 新增 `config/uniforms/probe.idl`：两个激活 probe 的 `worldToProbe`、box extents/blendDistance、enabled/intensity/maxMip，以及 `probeGlobal`；`ProbeUniforms` 为 208 B / 7 descriptors。
- 新增 `ReflectionProbe`（`src/render/scene/reflection_probe.*`）：局部盒变换、Euler rotation、box 内权重/包含测试、CPU 最近 2 个选择；局部轴约定与 HDRP probe influence/proxy 一致。
- sidecar `.hdrp.json` 新增 `probes` 数组：`texture`、`position`、`rotation`、`boxExtents`（半 extents）、`blendDistance`、`intensity`、`enabled`；纹理路径相对 sidecar，纹理仍追加到 `RenderScene::images`。
- 新增 `ProbeManager`：从 sidecar 最多加载 4 个静态 equirect probe，复用 `EnvironmentProcessor` 生成 GGX mip（纹理按 rotation=0 生成，rotation 在 shader 的 `worldToProbe` 中应用），上传为独立 RGBA32F mip 链。
- CPU 每帧按相机位置选择影响盒内的最近 2 个 probe；超过 4 个时告警并确定性使用 sidecar 前 4 个；无探针/相机在全部盒外时 active=0。
- `shaders/lighting/environment.sc` 增加 box projection：世界位置/反射方向变换到 probe 局部空间，与半 extents 求 ray-AABB 出口交点，用局部交点方向采样 probe equirect；按 box 面距离 / `blendDistance` 计算影响权重。
- probe 纹理 stage 14/15；最多两个 probe 按权重归一化混合，剩余权重回退 stage 10 sky IBL；probe 的 maxMip/intensity 独立上传。天空反射关闭时仍可使用 probe，probe 关闭/无 probe 时逐像素回到 M1 sky IBL。
- `FrameSettings::ReflectionProbe` + `--reflection-probes`/`--no-reflection-probes` 门控；forward 与 deferred 共用 `hbgEvaluateAmbient` 的 probe 路径。
- 修正 forward instanced 路径缺失 environment/probe/cluster uniform 与 probe 纹理绑定，避免同材质多实例金属物体丢失 IBL/probe。
- Debug view `ReflectionProbe` 仍未实现；登记为 [m12-checklist.md](m12-checklist.md) B07。Probe 状态由 `deferred diagnostics` 日志记录。

### 验收

- [x] 2 个探针的 box projection/方向旋转正确；探针边界按 `blendDistance` 平滑淡出，无硬接缝。
- [x] 探针外/无探针时回退 sky IBL；`--no-reflection-probes` 与无 sidecar probes 的场景逐像素完全一致，天空反射仍可用。
- [x] Forward 与 deferred 的探针反射一致：`m28_single_view` on 图 **56.52 dB**，off 图 **48.21 dB**（D3D11，Uffizi sky）。
- [x] 最多 4 个静态探针；5 个时日志 `5 declared, only the first 4 ...` 且只加载前 4 个；shader 固定 2 个 active slot，无 sampler 越界。
- [x] D3D11/Vulkan 一致；sidecar/选择逻辑/rotation/权重/上限有 CPU 单测。
- 验证场景：`m28_single_view`（1 probe）、`m28_blend_view`（2 probes，`blendDistance=2`）、`m28_rotation_view`（rotation Y=90）、`m28_boundary`（有限盒，边界混合到 sky）；相机 `(0,3,6)`，probe 盒包含相机。
- 指标：单 probe on/off **RMSE 0.315103 / 10.03 dB**；D3D/Vulkan on **64.17 dB**、off **61.70 dB**；双 probe blend D3D/VK **64.22 dB**；rotation D3D/VK **64.21 dB**。
- `m28_single_view` deferred/forward on **56.52 dB**；off **48.21 dB**；`--no-reflection-probes` vs 无 probe sidecar **RMSE 0**。
- 1080p deferred 单 probe：graph **40/40**（probe 不增加 pass），无资源泄漏；无 probe 时 graph 仍 **40/40**。
- 截图：`m28_final_single_on.png`/`m28_final_single_off.png`、`m28_final_single_on_vk.png`/`m28_final_single_off_vk.png`、`m28_final_blend.png`/`m28_final_blend_vk.png`、`m28_final_rotation.png`/`m28_final_rotation_vk.png`、`m28_final_boundary.png`、`m28_1080_on.png`/`m28_1080_off.png`、`m28_view_fwd_on2.png`/`m28_view_fwd_off2.png`。
- `hbg_tests` **120 cases / 17453 assertions**；`ctest` 2/2；`hbg_idl_check` 通过；双后端 `--gfx-test` 通过。

### HDRP/Core 源码索引

- HDRP `Lighting/Reflection/HDProbe.cs:735-803`：probe weight/influence/proxy/bounds。
- HDRP `Utilities/ProbeSettings.cs:141-175, 283-298`：weight、influence、proxy、cube resolution。
- HDRP `Lighting/Reflection/VolumeProjection.hlsl:8-115`：`WorldToProxySpace`、`IntersectBoxProxy`、`InfluenceBoxWeight`。
- HDRP `Lighting/Reflection/ReflectionProbeTextureCache.cs:126-135, 243-329, 594-636`：probe atlas/convert/convolve/fetch（M2.8 未实现 atlas）。
- HDRP `Lighting/LightEvaluation.hlsl:779-821`：`EvaluateLight_EnvIntersection` 的 projection/influence 组合。
- HDRP `Lighting/LightLoop/LightLoopDef.hlsl:89-125, 159-203`：sky/env light 数据与 `SampleEnv`。
- HDRP `Material/Lit/Lit.hlsl:1912-2007`：probe reflection 的 `EvaluateBSDF_Env` 层级/权重路径。

### 简化边界

- 静态 equirect probe，texture 在 CPU 侧转成 GGX mip；不做 realtime capture、cubemap atlas、convert/convolve GPU、planar probe、probe volume/APV。
- 最多 4 个 probe，shader 固定只混合最近 2 个；CPU 仅按相机位置/影响盒选择，不做 HDRP 的完整 probe hierarchy、importance、light layer、per-pixel culling、streaming。
- influence/proxy 共用同一 box；无 normal influence、per-face fade、box side fade、distance-based roughness、range compression、decal/SSR/SSGI 专用 probe 路径。
- 不实现 ReflectionProbe debug view；sidecar 只提供烘焙 equirect，不做实时阴影/材质捕获。
## M2.9 验收/性能

### 实现计划

- 扩展 `DebugDisplayMode`：GBuffer albedo/normal/roughness/metallic/AO/emissive、SSAO、SSR、SSGI、Decals/Probe（按实现情况）；deferred 下 Albedo/Roughness 读 GBuffer，不再走 material debug pass。
- 增加 deferred graph 诊断：pass 数、GBuffer MRT、stencil、feature on/off 的 pass 对比。
- D3D11/Vulkan 截图矩阵：deferred base、GBuffer debug、SSAO/SSR/SSGI、decals、probes、render-scale、feature off fallback。
- 性能：记录 forward/deferred 的 CPU/GPU、draw、pass 数；1080p 目标 60fps（GTX 1660/RX 5600 级），不达标时给出瓶颈和后续任务。
- 文档：回填每个任务 `file:line`、测试计数、边界；更新 `implementation-plan.md` 的 M2 状态/帧顺序/任务表；`m2-checklist` 状态改为 `[x]` 仅在有验证后。
- M2.9 完成后再将 sample/default 切到 deferred；保留 `--lit-shader-mode=forward` 回归。

### 实现记录（2026-11-11）

- `DebugDisplayMode` 从 15 个扩到 18 个；新增 `Metallic`、`AmbientOcclusion`、`Emissive`。`--debug-view` 支持 `metallic|metal`、`ao|gbuffer-ao|material-ao`、`emissive|emission`；旧的 `ambient-occlusion` 仍映射 SSAO，避免破坏 M2.4 语义。
- deferred 下 `Albedo/Roughness/Metallic/AO/Emissive` 统一走新增 `DebugGBuffer` fullscreen pass：读取 GBuffer0/1/2/3，`gbufferUV=(u,1-v)`，用 `depthPyramid` 的深度遮掉背景，模式编码复用 `DebugUniforms.clusterParams.y`，不新增 uniform block。
- forward 的 `Albedo/Roughness` 仍走 `DebugAlbedo/DebugRoughness` material pass；deferred 不再重绘 opaque material。`debug_normal.fs.sc` 修正 V 翻转，修复 normal debug 上下颠倒（forward/deferred 共用）。
- `ForwardRenderer::FrameDiagnostics` 暴露 `passCount/gbufferTargets/deferred/stencil`；首帧日志增加 `deferred diagnostics: GBuffer MRT=4, depth=..., stencil=..., color=..., SSAO/SSR/SSGI/Decals/Probes, live/culled`。
- `render-test` 新增 `render performance` 日志：frames、平均 wall frame ms、fps、bgfx CPU submit、GPU timer、draw/compute、passes、deferred、GBuffer MRT、stencil。
- `config/frame_settings.json` 默认 `litShaderMode` 切为 `deferred`；`pipeline.json` 仍为 `both`，`--lit-shader-mode=forward` 保持 M1 回归。
- 测试：`tests/render/test_debug_display.cpp` 更新 18 模式表；`tests/app/test_backend.cpp` 覆盖 metallic/ao/emissive CLI；`test_uniform_layout` 继续校验 DebugUniforms 32B/2 descriptors。

### 验收

- [x] `hbg_tests` **120/120 passed，17465 assertions**；`ctest` **2/2**；`hbg_idl_check` 通过。
- [x] D3D11/Vulkan deferred base 无 validation error、无资源泄漏；graph 顺序与文档一致；`ForwardRenderer` 日志输出 MRT=4、D24S8 stencil=true、live/culled pass。
- [x] Feature off fallback 全部回到 deferred base：`m29_fallback_base` vs `m29_fallback_off` 逐像素一致；forward 带 `--ssao --ssr --ssgi --decals` 与 forward base 逐像素一致。
- [x] Forward 回归保持 M1 基线；`--lit-shader-mode=forward` 仍可运行，`m29_base_forward` D3D/VK **64.2174 dB**。
- [x] GBuffer/stencil 在 render scale 0.5/1.0 下正确：1920×1080 presentation 使用 960×540 GBuffer（`deferred foundation active: 960x540 GBuffer with 4 targets`），render-scale 0.5 graph **38** passes；透明、sky、unlit 仍由原 M2.2/M2.7 回归覆盖。
- [x] 1080p 性能记录完成；D3D11/VK base 与 full-feature 均超过 60fps；未达标项无（RenderDoc 抓帧仍缺环境）。
- [x] `config/frame_settings.json` 不传 `--lit-shader-mode` 时启动为 deferred（`settings: lit=deferred`），截图 `m29_default_deferred.png`；`pipeline.json` 仍 `both`，forward CLI 回归可用。
- [x] 文档已回填：`docs/m2-checklist.md` M2.9、`docs/implementation-plan.md` 当前状态、`AGENTS.md` Debug/性能约定；无 stale “默认 forward” 描述。

### 指标

- Deferred base D3D/VK：**64.2224 dB**；forward base D3D/VK：**64.2174 dB**；同一 D3D 场景 forward/deferred：**56.7363 dB**。
- 全特性（SSAO+SSR+SSGI+Decals）D3D/VK：**69.6591 dB**。
- GBuffer debug D3D/VK：albedo/normal **逐像素一致**；roughness **49.7880 dB**、metallic **49.7982 dB**、AO **39.5633 dB**、emissive **76.1606 dB**（最大差 0.0157）。
- `m29_color_albedo_mat` vs `m29_color_albedo_gbuffer`：GBuffer albedo 与 material albedo 视觉一致；normal debug 已修正上下颠倒，地面在画面底部。
- 1080p deferred D3D/VK：**64.2665 dB**；1080p forward/deferred D3D：**56.7294 dB**。
- 1080p 性能（1920×1080，300 帧，`--no-vsync`，RTX 2060 SUPER）：

| 路径 | 平均 frame ms | FPS | GPU ms | draw | passes |
|---|---:|---:|---:|---:|---:|
| D3D11 deferred base | 6.854 | 145.9 | 1.701 | 83 | 40 |
| D3D11 forward base | 7.118 | 140.5 | 1.546 | 79 | 38 |
| D3D11 deferred full-feature | 8.655 | 115.5 | 6.170 | 106 | 51 |
| Vulkan deferred base | 2.869 | 348.5 | 2.041 | 83 | 40 |
| Vulkan forward base | 3.091 | 323.5 | 1.334 | 79 | 38 |
| Vulkan deferred full-feature | 8.671 | 115.3 | 6.713 | 106 | 51 |

- 性能口径：平均 frame 为 render-test loop wall time；GPU 为 bgfx GPU timer；CPU submit 为 bgfx 最后一帧统计。单次 300 帧结果，D3D11 deferred base 的 run-to-run 约 119–146fps，表中为其中一次；目标硬件为 GTX 1660/RX 5600 级，本机 RTX 2060 SUPER 达到 60fps，未在低一档硬件复测。
- 截图矩阵：`m29_default_deferred.png`、`m29_base_deferred.png`/`_vk.png`、`m29_base_forward.png`/`_vk.png`、`m29_1080_deferred.png`/`_vk.png`、`m29_1080_forward.png`、`m29_all_features.png`/`_vk.png`、`m29_gbuffer_albedo/normal/roughness/metallic/ao/emissive.png` 及 `_vk`、`m29_render_scale_05.png`、`m29_fallback_base/off.png`、`m29_forward_base/features.png`、`m29_normal_forward_fixed.png`、`m29_color_albedo_mat/gbuffer.png`。

### HDRP/Core 源码索引

- HDRP `Debug/MaterialDebug.cs:53-78`：`DebugViewGbuffer` 枚举；`:337-377`：GBuffer/BSDF 调试项构建。
- HDRP `RenderPipeline/HDRenderPipeline.Debug.cs:1561-1621`：`DebugViewMaterialGBuffer` 全屏读取 GBuffer 的路径。
- HDRP `RenderPipeline/HDRenderPipeline.Prepass.cs:79-81`：debug 开启时清 GBuffer；`:721-793`：4 RT GBuffer MRT 绑定。
- HDRP `RenderPipeline/HDRenderPipeline.RenderGraph.cs:146`：debug material 分支在 render graph 中的位置。

### 简化边界

- GBuffer debug 只提供 albedo/normal/roughness/metallic/AO/emissive 六个 M2 字段，不是 HDRP 全 material/BSDF 调试；emissive 输出 clamp 到 [0,1] 后 sRGB，无 HDR 伪彩/曝光控制。
- `ReflectionProbe` debug view 仍未实现（M2.8 可选边界）；Probe 选择/live 数量只在 `deferred diagnostics` 日志中显示。
- normal debug 的 forward 与 deferred 背景不同：forward prepass normal buffer 背景为黑，deferred GBuffer clear 为 +Z purple；两个后端的几何朝向都已修正。
- 性能数字来自 bgfx stats + wall loop，不是 RenderDoc GPU 抓帧；外部大场景（Sponza）和 GTX 1660/RX 5600 级硬件未复测。

## 测试矩阵（规划）

| 场景 | 验证点 |
|---|---|
| Deferred base | GBuffer、deferred lighting、forward-only、transparent、sky |
| GBuffer debug | albedo/normal/roughness/metallic/AO/emissive |
| 阴影 | 方向光级联、point/spot、D3D11/Vulkan |
| SSAO | on/off、半分辨率、边界 clamp |
| SSR | 金属/光滑材质、屏幕外 fallback、关闭跳过 |
| SSGI | 室内/角落、SH fallback、关闭跳过 |
| Decals | 单/多 decal、normal/MAOS、sidecar |
| Probes | 双探针 box projection、边界 blend、无 probe fallback |
| Render scale | 0.5/1.0、GBuffer/lighting/presentation 尺寸 |
| 回归 | forward 路径、M1 86/15842 基线 |

## 已知限制（M2 v1）

- Deferred 只支持 4 RT、full depth prepass、D24S8；不支持 MSAA、partial prepass、D32F stencil。
- 只支持 Lit opaque GBuffer；无 clear coat、SSS、light layers、shadow mask。
- SSAO/SSR/SSGI 为屏幕空间 v1：无 temporal/history；SSGI 为后置加性并默认扣 0.5 ambient baseline；SSR 无透明/Color pyramid；SSAO 不影响 forward-only/transparent。
- Cluster 为 CPU 64×64×32，不是 HDRP compute cluster；Deferred 仍用 FPTL。
- Decals 为 3RT DBuffer、deferred opaque only；无 decal layers/VFX/forward decals。
- Reflection Probes 为最多 4 个静态 equirect、最近 2 个混合；无 realtime capture/planar/probe volume。
- 不做 raytracing、GPU cluster、compute light list、SSGI temporal、SSR accumulation。
- RenderDoc 未安装时继续用双后端截图 + bgfx stats + graph diagnostics；M2 验收需补 RenderDoc（若环境可用）。

## 常用命令（M2 目标 CLI，M2.0 起落地）

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
py -3 tools/idl_gen.py --root . --check

.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --render-test=<scene.gltf> --frames=3 --screenshot=deferred.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --lit-shader-mode=deferred --render-test=<scene.gltf> --frames=3 --screenshot=deferred_vk.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=forward --render-test=<scene.gltf> --frames=3 --screenshot=forward.png

.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --ssao --render-test=<scene.gltf> --frames=3 --screenshot=ssao.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --ssr --render-test=<metal.gltf> --frames=3 --screenshot=ssr.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --ssgi --render-test=<scene.gltf> --frames=3 --screenshot=ssgi.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --decals --render-test=<decal.gltf> --frames=3 --screenshot=decal.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --lit-shader-mode=deferred --reflection-probes --render-test=<probe.gltf> --frames=3 --screenshot=probe.png
```

## M2 出口标准

- Forward 与 Deferred 双路径均可运行；M1 forward 回归不退化。
- GBuffer + Deferred Lighting + Cluster + SSAO/SSR/SSGI + Decals + Reflection Probes 的 v1 边界全部实现并验证。
- D3D11/Vulkan 无 validation error；截图/像素差异有记录；无资源泄漏。
- `hbg_tests`、`hbg_idl_check`、graph diagnostics 全通过；HDRP `file:line` 回填完成。
- 1080p 性能有记录；未达标项明确转 M3/M6，不阻塞 M2 出口。
