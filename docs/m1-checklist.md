# hdrp-bgfx M1 实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 本文件只覆盖 M1.1–M1.10；M0 见 [m0-checklist.md](m0-checklist.md)。
> 总体方案与后续 M2+ 路线见 [implementation-plan.md](implementation-plan.md)。
> 源码优先：实现前先读 HDRP/Core/bgfx 源码；参考根：HDRP `E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`；Core `E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`；bgfx `build\vs2022\_deps\bgfx-src`。

## 状态

| 里程碑 | 状态 | 摘要 |
|---|---|---|
| M1.1 RenderGraph | `[x]` | Handle/PassBuilder、校验、死 pass flood-fill、transient/FBO 池、ViewId 分配 |
| M1.2 FrameSettings | `[x]` | 128 位 BitSet、HDRP bit 映射、默认值/OverrideMask、Pipeline 门控、JSON |
| M1.3 ViewState | `[x]` | 矩阵/抖动/上帧矩阵、History 双缓冲、RT Scale、全局 CB |
| M1.4 DrawList | `[x]` | HDRP queue/pass tag/frustum cull/sort key、opaque+shadow instancing、submesh index range |
| M1.5 Prepass | `[x]` | 深度/法线（888 oct+roughness）、物体/相机运动矢量、法线调试、深度金字塔 min/max mip |
| M1.6 光照 v1 | `[x]` | CPU 16×16/64×64 tile LightList、R32_UINT 数据纹理、方向光分离、Forward LightLoop |
| M1.7 阴影 v1 | `[x]` | 4 级联方向光、point 6 面/spot 请求、双 atlas、ShadowContext、PCF |
| M1.8 Sky/IBL | `[x]` | HDR 环境贴图、天空 pass、SH9 环境光、GGX mip、天空反射 |
| M1.9 最终输出 | `[x]` | 固定/自动曝光、ACES/Neutral、sRGB、Debug 覆盖 |
| M1.10 Debug | `[x]` | 深度/法线/速度/Cluster/阴影图集/Albedo/Roughness |

## 当前验证

`ctest` 86 个用例、15842 assertions + `hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test`、DamagedHelmet、重复 helmet instancing、shadow scene/重复 cube instancing、透明场景、prepass normal/motion/depth-pyramid、point/spot LightList、point/spot/级联阴影、小型 point 场景、wireframe、debug light-data 截图通过；双 helmet `opaque=2 / 1 batch / draw=4`，双 cube 阴影 `opaque=3 / 2 batches / draw=8`；`--camera-orbit --debug-view=motion-vectors` 验证相机/物体运动矢量；`--render-scale=0.5` 双后端输出 1280×720 截图；M1.8 `Sky/IBL` 双后端 graph `28/28`，金属反射、SH9 ambient、SkyReflection 开关和无 sky fallback 验证通过；M1.10 Debug 双后端覆盖 depth/depth-pyramid/normal/motion/light-cluster/方向与 punctual shadow atlas/albedo/roughness，shadow atlas graph `38/38`、Material debug `27/27`，像素一致性验证通过。

## 关键架构事实

- C++20，命名空间 `hbg`；类型 `PascalCase`，函数 `camelCase`，成员 `m_`，常量 `k_`。
- 右手系 Y-up、CCW 正面；资源句柄化；`gfx/` 外禁止裸 `bgfx::*Handle`；`src/app/debug_ui.cpp` 是自写 bgfx backend 的既有例外。
- bgfx 限制：无 StorageBuffer 读取、无 bindless/AsyncCompute/DXR/Geometry Shader；LightData/ShadowContext 使用 Data Texture + `texelFetch`；固定 16 sampler；int/bool uniform 用 float。
- 本机 caps：`homogeneousDepth=false`、`originBottomLeft=false`、`maxViews=4096`、`maxTextureSamplers=16`；`RG11B10F` 可作 RT。
- 矩阵：列向量下 `viewProj = projection * view`；阴影 `worldToShadow = biasCrop * (lightProj * lightView)`。ShadowContext 的矩阵按列写入 texel 0..3；bgfx `mat4` 构造布局要求 shader 使用 `transpose` 后再 `mul(M, v)`。
- **ViewState**：jittered `viewProj` 供 forward；`nonJitteredViewProj`/`prevViewProj` 为非 jittered；默认 jitter 关闭。
- **Global CB**：864 bytes / 19 descriptors；`screenSize` 为 render 尺寸，`rtScale.xy/zw` 为当前/上一帧 render/target。
- **RT scale**：`ViewResolution` 区分 render/target；Forward/FBO 用 render 尺寸，presentation 用 target 尺寸；`--render-scale` 范围 0.1..1.0。
- **PassUniforms**：240 bytes / 9 descriptors：`shadowMatrix`、`shadowViewProj`、`shadowParams(filterMode,depthBias,enabled,normalBias)`、`shadowCascadeSplits`、`lightDirection`、`lightColor`、`ambientColor`、`lightCounts(total,directional,punctual,shadowed)`、`lightListParams`。
- **LightData**：`RGBA32F`，4×32；每灯 4 texels：position/range、color/intensity、direction/cosOuter、shadowIndex/cosInner/layerMask/flags；flags 低 2 位 type，bit2 shadow。方向光 shadowIndex=0（级联基址）；point/spot 为 ShadowContext 请求基址，point 再按 dominant face 加 0..5。
- **LightList**：CPU 16×16 FPTL tile + 64×64 big-tile；`R32_UINT` 布局 `width=33*tileCountX`、`height=tileCountY`，每 tile 首 dword 为 count，后接最多 32 个绝对 GpuLight 索引；方向光不进入 tile list。
- **ShadowManager**：方向光 atlas 2048×2048（4 个 1024 级联）+ punctual atlas 2048×2048；point 6×256 faces、spot 1×512；方向光 split 为 shadow distance 的 10%/25%/50%/100%，shadow distance 取 scene radius×8，clamp 200 与 camera.far。`ShadowContext` 为 `RGBA32F` 8×32（8 texels/request，最多 32 requests）。
- **DrawList**：HDRP queue 分段 + `ShaderPassMask` 过滤 + non-jittered frustum cull；opaque 按 queue/state key，transparent 按 queue/priority/distance back-to-front；opaque/shadow 使用 64B instance transform 合批，transparent 不实例化。
- **ForwardRenderer**：法线翻转 `viewSign = gl_FrontFacing ? -1.0 : 1.0`；透明物体不写深度；forward/shadow index buffer 使用 submesh offset/count。
- 每次 `Encoder::submit` 默认 `BGFX_DISCARD_ALL`；同一 draw 前必须重新上传所需 global/pass/material uniforms 和 sampler。

## M1.1 RenderGraph

### 实现

- `src/render/graph/render_graph.h/.cpp`：logical texture/buffer handle、Builder、Context、`beginFrame/compile/execute/shutdown`。
- Builder：`readTexture/writeTexture`、`setColorAttachment/setDepthAttachment`、`readBuffer/writeBuffer`、`writeBackbuffer`、`allowCulling`、`setRenderFunc`。
- `compile()`：handle 代际校验、attachment 连续性、transient read-before-write、缺 render func、backbuffer/attachment 冲突；从 side effect/backbuffer/`allowCulling=false` 向上游 flood-fill 裁剪；计算生命周期；分配 ViewId。
- `execute()`：解析 imported 资源；按 descriptor + 生命周期从 transient texture/buffer 池取用；按附件组合复用 FBO；仅调用 live pass。
- ViewId：graph pass 从 `kRenderGraphViewBase=8` 递增；backbuffer 固定 `4094`；DebugUI 固定 `4095`。
- `ForwardRenderer` pass 顺序：`Shadow(s) → Scene Clear → Forward Depth Prepass → Depth Pyramid → Objects Motion Vectors → Camera Motion Vectors → Forward Opaque → Transparent → Wireframe → Presentation`；Material debug 时替换 sky/forward/wireframe 为 opaque debug pass；UI 在 graph 之后。
- `ResourceManager::destroyTexture` 会先销毁引用该 texture 的 cached FBO，修复 stale framebuffer key。

### HDRP/Core 源码索引

- `RenderGraph.cs:1448` BeginRecording；`:1490` EndRecordingAndExecute；`:1540` Execute；`:1260` AddRasterRenderPass。
- `IRenderGraphBuilder.cs:115` AllowPassCulling；`:163` SetRenderAttachment；`:412` SetRenderFunc。
- `Compiler/NativePassCompiler.cs:475` CullUnusedRenderGraphPasses；`:490` side-effect 上游 flood-fill；`:539` 未使用输出裁剪。
- `RenderGraphResourcePool.cs:54` ReleaseResource；`:65` TryGetResource；`:197` PurgeUnusedResources。
- HDRP `HDRenderPipeline.RenderGraph.cs:203` RenderShadows；`:223` RenderForwardOpaque；`:270` RenderTransparency；`:367` RenderPostProcess。

### 简化边界

- 不做 native render pass/subpass 合并、async compute；每个 live raster pass 一个 bgfx ViewId。
- 单一 resource version；同一 texture 的多个 writer 保守地全部保留。
- `addComputePass` API 已保留，ForwardRenderer 尚未使用 compute。
- transient 池按 descriptor shape + 生命周期复用，跨帧保留最近 2 帧；resize 清空池和 FBO。

### 验证

- `ctest` 通过；D3D11/Vulkan DamagedHelmet、shadow scene、wireframe、debug light-data 截图均 exit 0。
- shadow scene 的资源计数无泄漏；`--debug-light-data` 时 shadow pass 可被裁剪。

## M1.2 FrameSettings

### 实现

- `src/render/pipeline/frame_settings.h/.cpp`：`FrameSettingsField` 与 HDRP 同 bit 索引（`LitShaderMode=0` … `AdaptiveProbeVolume=127`）；Camera/CustomOrBakedReflection/RealtimeReflection 三套默认值；`FrameSettingsOverrideMask` 与 `overrideWith`；`sanitize` 按 `PipelineSettings` 能力门控。
- `src/render/pipeline/pipeline_settings.h/.cpp`：静态能力、LitShaderMode、颜色/深度格式、MSAA、阴影/SSR/SSAO/SSGI/Decal/MotionVector/Water/Async 等 support 标志。
- JSON：`config/pipeline.json`、`config/frame_settings.json`；`--pipeline-settings=<path>`、`--frame-settings=<path>`；未知键/非法枚举/类型错误使加载失败。
- `ForwardRenderer`：`setSettings` 保存并 sanitize；`ShadowMaps` 门控 shadow pass，`OpaqueObjects`/`TransparentObjects` 门控 forward/wireframe；PipelineSettings 决定 HDR/Depth 格式并在 caps 不支持时 fallback。
- 默认能力为 ForwardOnly、RG11B10F、D24S8、MSAA None；ShadowMaps/MotionVectors 可用；SSR/SSAO/SSGI/Decals/Volumetrics 默认不支持。

### HDRP/Core 源码索引

- HDRP `FrameSettings.cs:107` `FrameSettingsField`；`:429` 128-bit 限制；`:455` `bitDatas`；`:508` `IsEnabled`；`:658` `Sanitize`；`:820` `AggregateFrameSettings`。
- HDRP `FrameSettingsDefaults.cs:7/92/149` 三套默认位数组；`:222` `Get`。
- HDRP `RenderPipelineSettings.cs:31` 结构；`:259/263/265` SSR/SSAO/SSGI；`:337/339` 颜色/深度格式；`:350` LitShaderMode；`:387/390` MSAA；`:399` MotionVectors。
- Core `BitArray.cs:531` `BitArray128`；`:554` 索引器；`:612/619` `|`/`&`。

### 简化边界

- 只有全局 Camera FrameSettings；per-camera override、FrameSettingsHistory、DebugMenu 四列视图顺延。
- `sanitize` 不依赖真实 Camera；Renderer 按普通 Camera 调用。

### 验证

- `--frame-settings` 关闭 `ShadowMaps/OpaqueObjects/TransparentObjects`：DamagedHelmet 的 shadow/opaque/transparent pass 被裁剪。
- `--pipeline-settings` 将 `supportShadows=false`：shadow scene pass 数下降。
- 不存在 settings 文件时 exit 1。

## M1.3 ViewState

### 实现

- `src/render/camera/view_state.h/.cpp`：维护 jittered/non-jittered view/proj/VP、逆矩阵、上一帧 view/proj/VP、相机位置、6 个 frustum planes；投影按 `bgfx::getCaps()->homogeneousDepth` 选择深度范围。
- `src/render/camera/history_buffer.h`：`HistoryBuffer<T>` 双槽；首帧 previous=current，第二帧起 `hasHistory=true`。
- `src/render/camera/resolution_group.h/.cpp`：`ViewResolution` 分离 render/target、screenSize、RT scale、dynamic-resolution scale；render scale 限制 0.1..1.0。
- TAA jitter：Halton(2/3) - 0.5；taaFrameIndex 0..1023；jitter 写入 `taaJitter = {x,y,x/w,y/h}`。
- 全局 CB 增加 invView/invProj/nonJittered/prev/taa/rtScale/dynamicResolution；`GlobalUniformBinder` 19 descriptors。
- `ForwardRenderer`：每帧由 ViewState 更新全局 CB；HDR/depth 用 render 尺寸，presentation 用 target 尺寸；`--render-scale` 与 DebugUI 滑杆已接入。

### HDRP/Core 源码索引

- HDRP `HDCamera.cs:24`、`:32` `ViewConstants`；`:1137` RequiresCameraJitter；`:1436-1451` update flow；`:2107` UpdateViewConstants；`:2338` GetJitteredProjectionMatrix。
- HDRP `HDCamera.cs:1594` scales/screen sizes；`:1625` UpdateShaderVariablesGlobalCB；`ShaderVariablesGlobal.cs:40`。
- Core `HistoryBuffer`/`RTHandle` 相关 API 为双槽与分辨率分离参考。

### 简化边界

- 只有 Camera history；TAA/motion blur 尚未实现，jitter 默认关闭。
- 动态分辨率只暴露 scale API，无真实动态分辨率调度。

### 验证

- `test_view_state` 覆盖 jitter/history/RT scale；`--render-scale=0.5` 双后端截图仍为 1280×720。
- `--camera-orbit` 验证相机运动与上一帧矩阵。

## M1.4 DrawList

### 实现

- `src/render/material/render_queue.h/.cpp`：照 HDRP 定义 Background/Opaque/AlphaTest/AfterPostprocess/PreRefraction/Transparent/LowTransparent/LineRendering/Overlay 优先级与 RenderQueueRange。
- `src/render/material/shader_pass.h/.cpp`：Forward/DepthOnly/DepthForwardOnly/ForwardOnly/GBuffer/GBufferWithPrepass/SRPDefaultUnlit/ShadowCaster/MotionVectors/TransparentDepthPrepass/TransparentBackface/TransparentDepthPostpass/DistortionVectors；按 Lit/Unlit 与 alphaMode 生成默认 pass mask。
- `src/render/culling/sort_key.h/.cpp`：SortingLayer/RenderQueue/BackToFront/QuantizedFrontToBack/OptimizeStateChanges/CanvasOrder/RendererPriority；opaque 使用 `CommonOpaque & ~QuantizedFrontToBack`，transparent 使用 `CommonTransparent | RendererPriority`。
- `src/render/culling/frustum.h/.cpp`：ViewState 六个平面做 AABB frustum 测试；frustum planes 使用 non-jittered VP。
- `DrawList`：DrawItem 记录 object/mesh/submesh/material、submesh index range、world bounds、distance、queue type、pass mask、sort key；按 pass tag + queue range 过滤，按 HDRP criteria 排序，再按 material/mesh/submesh 连续 state key 生成 DrawBatch。
- ForwardRenderer：opaque = `kAllForwardOpaquePassMask`（Forward|ForwardOnly|SRPDefaultUnlit）+ AllOpaque；transparent = `kAllTransparentPassMask` + AllTransparentWithLowRes；shadow = ShadowCaster + AllOpaque 且不做相机 frustum cull；wireframe 使用全部可见队列。
- opaque/shadow 使用 `gfx::InstanceData`（64B/instance）合批；transparent 保持逐物体深度排序；实例容量不足或程序不可用时回退普通逐物体路径。

### HDRP/Core 源码索引

- HDRP `HDRenderQueue.cs:16-51` Priority；`:93-113` RenderQueueRange；`:124-145` GetTypeByRenderQueueValue；`:147-174` ChangeType。
- HDRP `HDStringConstants.cs:4-120` HDShaderPassNames。
- HDRP `HDUtils.cs:23` k_OpaqueSortingCriteria；`HDRenderPipeline.cs:177-199` pass 列表；`:3410-3500` RendererListDesc/sorting；`:3556-3560` opaque/transparent draw。
- HDRP `HDRenderPipeline.Prepass.cs:623/695/843` depth/motion/GBuffer；`HDRenderPipeline.RenderGraph.cs:928-965` transparent queue 选择。
- bgfx `examples/05-instancing/instancing.cpp:224-264` instance data 上传；`vs_instancing.sc`/`varying.def.sc` 的 `i_data0..3` 约定。

### 简化边界

- M1 为 forward-only；GBuffer pass 只完成 tag/队列过滤，录制顺延到 [M2.1](m2-checklist.md#m21-gbuffer)。
- 无 indirect/multi-draw；shadow list 尚未用 light frustum 精细裁剪；alphaMask caster 尚未实现。
- DrawList 每帧重建，无持久 RendererList cache。

### 验证

- `test_draw_list` 覆盖 queue 数值/范围、pass mask、过滤、排序、batch 分组、frustum cull、距离量化。
- 重复 helmet：`opaque=2 / 1 batch / draw=4`；重复 cube 阴影：`opaque=3 / 2 batches / shadow=3 / draw=8`。
- 透明测试场景：`opaque=0 / transparent=2`，截图通过。

## M1.5 Prepass

### 实现

- `src/render/passes/prepass_utils.h/.cpp`：`depthPyramidMipCount` 与逐 mip 尺寸；`config/uniforms/prepass.idl` 新增 `Object.prevModel`、`Prepass.depthPyramidParams`。
- `src/gfx/framebuffer_cache.*`：`FrameBufferKey` 增加 per-attachment mip，`bgfx::Attachment` 支持离屏 mip；RenderGraph 的 attachment 支持 mip；FBO 池 key 区分 mip。
- `src/gfx/encoder.*`：新增 `setTextureMip`，支持只绑定某个 mip。
- Pass 顺序：`Scene Clear → Forward Depth Prepass → Depth Pyramid → Objects Motion Vectors → Camera Motion Vectors → Forward Opaque → Transparent → Wireframe → Presentation`。所有深度消费者显式声明 graph read；Camera Motion 采样 depth pyramid，避免 D3D 深度纹理同时作为 DSV/SRV。
- Depth prepass：`kDepthPrepassPassMask`（DepthOnly/DepthForwardOnly）+ AllOpaque，输出 `RGBA8` Normal Buffer 与深度；forward opaque 在 prepass 可用时 depth Equal、不写深度。
- Normal：`material/normal_buffer.sc` 按 HDRP `PackNormalOctQuadEncode + PackFloat2To888`；A 存 perceptual roughness。
- Motion Vectors：`motion_vectors.vs/fs.sc` 使用当前/上一帧 world matrix、`nonJitteredViewProj`、`prevViewProj`，输出 `RG16F`，`*0.5` 后 clamp；Camera MV 采样 depth pyramid 重建背景运动。M1 时远平面写零；M3.4 TAA 起远平面改为写背景 camera motion（见 M3.4）。
- Depth Pyramid：`RG32F` min/max + scratch 栅格路径，避免同纹理 mip 同时读写。

### HDRP/Core 源码索引

- HDRP `HDRenderPipeline.Prepass.cs:84-121/275-420/485-645` prepass 顺序/目标；`:146-161` Normal；`:185-201/648-707` Motion；`:1651-1670` Camera MV。
- HDRP `Material/NormalBuffer.hlsl:20/44`；`Material/BuiltinUtilities.hlsl:9/37`；`Material/Builtin/BuiltinData.cs:97`；Core `Packing.hlsl:57/71/527/538`。
- HDRP `RenderPipeline/RenderPass/DepthPyramid.compute`；`MipGenerator.cs:62-118`；`HDCamera.cs:1391`；bgfx `Attachment` mip + `examples/40-svt/vt.cpp:494`。

### 简化边界

- 透明 depth prepass/MV、distortion、TAA/motion blur、moving object prevWorld authoring 未接。
- Uniform 直通深度，不回读 depth clamp；深度金字塔为 RG32F min/max + scratch，不是 HDRP R32F min-only compute/atlas。

### 验证

- `ctest` 66 个用例、4440 assertions、`hbg_idl_check` 通过；新增 `test_prepass`、RenderGraph mip attachment、FrameBufferKey mip 测试。
- D3D11/Vulkan normal/motion/depth-pyramid debug、`--camera-orbit` 通过；graph `27/27`（transient=15、FBO=24）。
- 重复 helmet `opaque=2 / prepass=2 / motion=2`；shadow scene `28/28`（M1.6 时）；透明 `opaque=0 / transparent=2`。

## M1.6 光照 v1

### 实现

- `src/render/lighting/light_list_builder.h/.cpp`：按 HDRP FPTL 16×16 tile 与 64×64 big-tile 两阶段 CPU 构建；punctual light 用世界包围球投影屏幕 AABB，跨近裁剪面保守全屏，完全在相机后方剔除。
- `GpuLightListData`：每 tile 33 dword（count + 最多 32 个绝对 GpuLight 索引），布局 `width=33*tileCountX`、`height=tileCountY`，格式 `R32_UINT`；big tile 只作为构建中间层。
- `config/uniforms/pass.idl` 新增 `lightListParams`；shader 以 `USAMPLER2D(s_lightList,7)` + `texelFetch` 读取。
- `shaders/lighting/light_loop.sc`：方向光单独循环，punctual 从当前像素 tile 列表遍历；list 不可用时回退全 punctual。
- `ForwardRenderer` 在 resize/RT scale 变化时重建 LightList 纹理，每帧上传，forward opaque/transparent 显式 `readTexture(lightList)`，stage 7 绑定。
- `src/gfx/texture.*` 新增 `R32U`；`tests/render/test_light_list.cpp` 覆盖 tile 尺寸、中心/屏幕外/相机后方、相机内包围球、方向光排除、32 灯上限。

### HDRP/Core 源码索引

- HDRP `HDRenderPipeline.LightLoop.cs:665-697` BuildGPULightList；`:179-196` ClearLightLists；`:199-219` screen AABB；`:222-238` BigTile；`:240-283` PerTile；`:284-313` VoxelLightList。
- HDRP `Lighting/LightLoop/LightLoop.cs:106-108` tile 尺寸（16/32/64）；`:558-565` cluster 常量；`:635-653` tile 数量；`:1921-1954` light/tile CB；`:1963-1966` LightData 上传；`:2030-2043` dispatch。
- HDRP `LightLoopDef.hlsl:245-283` FPTL `GetCountAndStartTile`/`FetchIndex`；`:294-330` big-tile/cluster 读取；`LightLoop.hlsl:187` 入口，`:252-312` punctual 列表。
- HDRP `lightlistbuild-bigtile.compute`、`lightlistbuild.compute`、`LightCullUtils.hlsl`；`HDGpuLightsBuilder.Jobs.cs:422-462` screen bounds，`:639-651` sort key。

### 简化边界

- LightList 的 CPU 屏幕空间 AABB v1 已由 M6/M7 的 GPU light-list/cluster 关闭；剩余 256 lights、async light-list 等见 [m12-checklist.md](m12-checklist.md) A/B 类。
- 每 tile 最多 32 灯；spot 用 range 包围球保守裁剪，未做 cone 精确测试。
- `LightBuilder` 仍只选 1 个方向光 + 31 punctual；M1.7 已为 point/spot 回填 ShadowContext 请求索引。

### 验证

- `ctest` 71 个用例、4710 assertions、`hbg_idl_check` 通过；`test_light_list` 覆盖 tile/剔除/上限。
- D3D11/Vulkan point/spot 场景显示 tile 内灯光，`light list: 80x45 tiles / 20x12 big tiles / max=1`；小型 point 场景局部照亮。
- DamagedHelmet graph 27/27；M1.7 后 shadow scene 37/37；transparent 5/5；`--render-scale=0.5` 正常。

## M1.7 阴影 v1

### 实现

- `src/render/shadows/shadow_manager.h/.cpp`：`ShadowRequest` 记录 type/atlas/viewport/view-projection/world-to-shadow/face/light index/bias；方向光 4 级联，split 比例为 shadow distance 的 10%/25%/50%/100%，用相机 slice 的 8 个世界角点拟合 light-space 正交投影。
- 方向光 atlas 2048×2048，4 个 1024×1024 象限；punctual atlas 2048×2048，point 每面 256、spot 512；按行 shelf packer，失败时回滚 cursor 和已加入请求。
- `ShadowContext`：`RGBA32F` 8×32；每个 request 8 texels：world-to-shadow 矩阵列 0..3、`atlasScale.xy/atlasOffset.xy`、`depthBias/normalBias/invWidth/invHeight`、`type/faceIndex/0/enabled`、保留。最多 32 requests；point 的 6 个 face 连续分配，shader 用 dominant axis 选择 face。
- `LightBuilder` 暴露 selected lights 并支持 `setShadowIndex`；方向光写 0，point/spot 写 request base；没有 ShadowContext 请求时 shadowIndex 保持 -1。glTF 点光/聚光默认参与阴影。
- `shaders/lighting/shadows.sc`：stage 6 方向光 atlas、stage 8 context、stage 9 punctual atlas；从 context 重建矩阵并 `transpose` 后 `mul(M, position)`；方向光按 view depth 选 cascade，point 选 face；PCF 提供 1-tap/3×3/5×5 分支，`shadowParams.x` 为 filter mode（默认 3×3）。
- `ForwardRenderer` 为每个 ShadowRequest 录制一个 graph shadow pass，按 request 选择 atlas、设置 viewport/view/projection/depth clear；forward pass 读取两个 atlas 与 context，stage 6/8/9 绑定；FrameSettings 关闭 ShadowMaps 时不创建 shadow pass。
- `config/uniforms/pass.idl` 新增 `shadowCascadeSplits`；`PassUniforms` 240 bytes/9 descriptors；`tests/render/test_shadow.cpp` 覆盖 4 级联、point 6 面、spot、请求布局/索引和禁用阴影。`tests/asset/test_gltf.cpp` 锁定 point light castShadows。

### HDRP/Core 源码索引

- HDRP `Lighting/Shadow/HDShadowManager.cs:73-97` `HDShadowData`；`:171-331` `HDShadowRequest`/`HDShadowCullingSplit`；`:899-927` atlas size/directional resolution；`:929-969` `ReserveShadowResolutions`；`:1014-1062` `CreateShadowData` atlas scale/offset；`:1072-1108` `LayoutShadowMaps`；`:1110-1196` `PrepareGPUShadowDatas`；`:1414-1452` `RenderShadows`。
- HDRP `Lighting/Shadow/HDShadowAtlas.cs:102-199` atlas texture/props；`:207-220` add request；`:758` `RenderShadows`；`HDDynamicShadowAtlas.cs:39-126` `ReserveResolution`/`AtlasLayout` shelf packing，`:129-194` resize/scale。
- HDRP `HDShadowUtils.cs:45-71` point/spot extraction；`:121-141` directional cascade extraction；`:161-...` cubemap faces；`:298-...` spot matrix；`:374-...` point matrix。
- HDRP `HDShadowContext.hlsl`；`HDShadowSampling.hlsl:14-111` PCF 3×3/5×5/7×7；`HDShadowAlgorithms.hlsl:36-53` directional/punctual quality 宏，`:77-144` world-to-atlas texcoord；`HDShadowManager.cs.hlsl:20-37` `HDShadowData`。
- HDRP `HDShadowManager.cs:904-906` cascade atlas size；`:1166-1195` cascade data/upload；`HDDynamicShadowAtlas.cs:86-109` 2D shelf viewport。实现按 bgfx 限制改为 2D Data Texture + 每 request 一个 graph pass；未复制 HDRP cached/mixed atlas。

### 简化边界

- 固定 4 级联、固定 2048 atlas/1024 级联；point 6×256、spot 512；无动态 atlas 缩放、resolution scaling、cached/mixed shadow、area light shadow。
- 无 volume 的 maxShadowDistance/cascade border/fade、PCSS/EVSM/VSM/IMS、screen-space/contact shadow、shadow mask、light layers；PCF 默认 3×3，5×5 仅 shader 分支。
- shadow draw list 按相机共享，未做 per-request light frustum cull；alphaMask caster、slope-scaled bias 未实现；bias 使用固定值（方向光 `.01/.2`、point `.01/.2`、spot `.003/.05`）抑制 acne。
- point face 按 dominant axis 选择，face 边界无混合；cascade 边界无 fade；无 TAA/PCSS 软阴影；atlas 固定布局，不做异步 compute。

### 验证

- `ctest` 86 个用例、15842 assertions、`hbg_idl_check` 通过；`test_shadow` 覆盖 4 级联、point 6 面、spot、shadowIndex 回填、context 请求布局与禁用阴影。
- D3D11/Vulkan `shadow.gltf`：`requests=10 directional=4 punctual=6`，graph `37/37`；`point.gltf`：`33/33`；`spot.gltf`：`28/28`；无 shader/uniform 错误和资源泄漏。
- FrameSettings `ShadowMaps=false`：`requests=0`，graph 从 `37/37` 降到 `27/27`，不执行 shadow pass；`--render-scale=0.5` shadow scene `35/35`、无泄漏。
- 方向光与 point/spot 的 bias 调整在 D3D11/Vulkan 上验证；点光阴影 ground 仍有轻微 face/bias 边界，作为 M1.7 v1 保留。

## M1.8 Sky/IBL

### 实现

- `src/render/sky/environment_processor.h/.cpp`：支持 2:1 equirect HDR 和 6:1 cubemap strip；IBL 基图 1024×512、7 mips，sky 贴图 2048×1024；GGX importance sampling 32 samples/mip，mip roughness 使用 HDRP `MipmapLevelToPerceptualRoughness` 映射；SH9 从 IBL base 积分并预乘 clamped-cosine、1/π 和 SH basis，输出 diffuse irradiance/π。
- SH9 使用 Unity 的 7×vec4 packing（`shAr/shAg/shAb/shBr/shBg/shBb/shCr`），shader 用 `SHEvalLinearL0L1 + SHEvalLinearL2` 等价公式；方向、旋转和 intensity 在烘焙输入侧统一处理。
- `src/render/sky/sky_manager.h/.cpp`：加载 HDR/EXR，上传独立 sky（raw）和 IBL（7 mips）两张 `RGBA32F` 2D 纹理；U wrap/V clamp；`--sky`、`--sky-intensity`、`--sky-rotation`。
- `shaders/lighting/environment.sc`：equirect UV、SH9 采样、roughness→LOD、UE4 EnvBRDF 近似；`shaders/passes/sky.fs.sc` 从 `invViewProj` 重建方向，sky 用 LOD 0；`shaders/material/lit.sc` 用 SH9 替代半球 ambient，并加天空镜面反射。
- `ForwardRenderer` 在 prepass/depth pyramid 后、opaque/transparent 前插入 `Sky` fullscreen pass；sky 使用独立 raw 纹理，forward 使用 IBL 纹理；stage 10；`FrameSettingsField::SkyReflection` 控制镜面 IBL，天空仍可见。无 sky 时使用 1×1 黑 fallback，保留旧半球 ambient。
- `config/uniforms/environment.idl` 生成 `EnvironmentUniforms`：`skyParams(intensity,maxMip,enabled,reflection)` + 7 SH vec4；`tests/render/test_environment.cpp` 覆盖 SH 常数/梯度、mip 尺寸、方向 round-trip；`tests/app/test_backend.cpp` 覆盖 CLI；`tests/gfx/test_uniform_layout.cpp` 锁定 128 bytes/8 descriptors。

### HDRP/Core 源码索引

- HDRP `Sky/HDRISky/HDRISkyRenderer.cs:90-161` `PreRenderSky`/`RenderSky`；`HDRISky.shader` `GetColorWithRotation`、`RenderSky`；`Sky/SkyUtils.hlsl:14-39` `GetSkyViewDirWS`/`GetLatLongCoords`/`RotationUp`。
- HDRP `Sky/SkyManager.cs` `BuiltinSkyParameters`/`SkyRenderingContext`/`RenderSky`；`Sky/AmbientProbeConvolution.compute:51-215` `PackSHFromScratchBuffer`、`ConvolveZonalFromScratchBuffer`、SH basis/clamped-cosine packing。
- HDRP `Material/GGXConvolution/IBLFilterGGX.cs:129-194` `FilterCubemapCommon`；`GGXConvolve.shader` `MipmapLevelToPerceptualRoughness`/`IntegrateLD`；Core `ShaderLibrary/SphericalHarmonics.hlsl`、`ImageBasedLighting.hlsl`。
- HDRP `Lighting/LightLoop/LightLoopDef.hlsl:159-203` `SampleEnv`/sky reflection；`ShaderLibrary/ShaderVariablesFunctions.hlsl:113` `SampleSkyTexture`。实现因 bgfx 无 StorageBuffer/compute 依赖改为 CPU GGX + 2D equirect mip，不复制 HDRP cubemap/BSDF LUT。

### 简化边界

- IBL 为 1024×512 equirect 7 mips，sky 为 2048×1024 raw；不是 HDRP 的 cubemap + BSDF cubemap array；无真实 cubemap 过滤立方体、无反射探针/planar reflection/SSR/SSGI。
- GGX prefilter 为 CPU 32 samples/mip；sky texture 不做逐帧更新；SH9 为静态烘焙，不支持动态天空/时间变化、clouds、volumetric fog、backplate、distortion/flowmap。
- SkyReflection 只控制 IBL specular，ambient diffuse SH 仍启用；M1.9 已补 exposure/EV100；无 specular occlusion/DFG LUT、无 mip bias/rough reflection 配置；使用 UE4 analytic EnvBRDF 近似。
- 支持 equirect 和 6:1 cubemap strip，不解析 Unity cubemap 的 face 元数据/压缩纹理；外部 HDR 源缺失时沿用旧 fallback。

### 验证

- `ctest` 86 个用例、15842 assertions、`hbg_idl_check` 通过；`test_environment` 覆盖 equirect/6:1 cubemap strip、SH 常数/梯度、mip 尺寸与方向 round-trip。
- D3D11/Vulkan `BoxTextured + Uffizi EXR`：graph `28/28`，sky/IBL 纹理上传无错误；金属材质显示天空反射；skybox 与反射在双后端一致。
- `DefaultHDRISky.exr`（6:1）和 `blue_lagoon_night_1k.hdr`（2:1）均可加载；无 sky 时 graph `27/27` 与原路径一致；`SkyReflection=false` 时天空仍显示、镜面反射关闭。
- `--sky-intensity`/`--sky-rotation` 已接入 CLI；`--render-scale=0.5` 下 sky pass 正常。RenderDoc 抓帧仍待补。

## M1.9 最终输出

### 实现

- `src/render/postprocess/post_process_settings.h/.cpp`：固定 EV100 / 自动曝光设置、HDRP `PhysicalCamera.hlsl` 的 EV100↔exposure 换算、`ColorUtils` 的 lens attenuation/light-meter 常量、参数 sanitize；自动曝光为最终 pass 内 4×4 center-weighted 瞬时测光。
- `shaders/post/tonemap.fs.sc`：固定/自动曝光；HDRP `Color.hlsl` 的 ACES approximate（Glow/Red modifier/AP1/RRT+ODT）与 Neutral；None；线性→sRGB 输出。ACES 矩阵使用显式行向量 `dot`，避免 D3D 全局 `const` 未初始化。
- `config/uniforms/postprocess.idl` 生成 `PostProcessUniforms`：exposureParams / autoExposureParams / tonemapParams；CLI 与 DebugUI 支持 `--exposure-mode=fixed|auto`、`--exposure-ev100`、`--exposure-compensation`、`--exposure-limit-min/max`、`--tonemap=aces|neutral|none`、`--debug-exposure`。
- `FrameSettingsField::Postprocess`、`ExposureControl`、`Tonemapping` 门控；固定默认 EV100=-1.263（等价旧 2× exposure），自动默认 limits -1..14 EV。

### HDRP/Core 源码索引

- HDRP `HDRenderPipeline.PostProcess.cs:1455-1585` `DoFixedExposure`/exposure texture；`PostProcessing/Shaders/Exposure.compute:15-153` fixed/average/reduction/adaptation；`ExposureCommon.hlsl:26-121` EV100/luminance/adaptation。
- Core `ShaderLibrary/PhysicalCamera.hlsl:12-74` `ComputeEV100`/`ConvertEV100ToExposure`；`ColorUtils.cs:39-55/224-288` lens/light-meter constants、EV100 conversion；Core `Color.hlsl:608-786` Neutral/AcesTonemap approximate；HDRP `FinalPass.shader:170-205` sRGB/dither output。

### 简化边界

- 自动曝光不是 HDRP GPU histogram/1×1 exposure texture/Progressive adaptation；为最终 tonemap 内 4×4 采样，瞬时响应，无 exposure history。
- ACES 使用 approximate 路径（非 full ACES），无 Bloom/ColorGrading/Custom LUT/Vignette/FilmGrain/Dithering；无 HDR display/HDROutput；sRGB 为手动线性→sRGB，不切换 swapchain sRGB。
- `--debug-exposure` 只是 HDRP 风格 EV 覆盖；M1.10 已补独立全屏 debug view。

### 验证

- `hbg_tests` 86 个用例、15842 assertions；`hbg_idl_check` 通过；`test_post_process` 覆盖曝光换算、固定/自动补偿、limits、sanitize、CLI。
- D3D11/Vulkan：fixed ACES/Neutral、auto exposure（默认按场景亮度可能偏暗，`--exposure-compensation=2` 提高可见性）、`--debug-exposure`、`--render-scale=0.5`；graph 保持 `28/28`（自动测光在最终 pass 内，不加额外 graph pass）。
- 自动曝光对 sky intensity 1×/2× 输出均值基本稳定（55–75/255），确认 4×4 测光会收敛而不是固定曝光。
- 最终输出在每个后端都执行线性→sRGB；无 NaN/全黑 ACES 回归。

## M1.10 Debug

### 实现

- `src/render/debug/debug_display.h/.cpp`：11 个模式 `None / LightData / Normal / MotionVectors / Depth / DepthPyramid / LightCluster / ShadowAtlasDirectional / ShadowAtlasPunctual / Albedo / Roughness`，名称表与 CLI/UI 共用。
- 深度：`shaders/passes/debug_depth.fs.sc` 从硬件深度和 `u_global_invViewProj` 重建 view-space 线性深度；`config/uniforms/debug.idl` 新增 `DebugUniforms.depthParams`（near/far），DebugUI/CLI 共用。`debug_depth_pyramid.fs.sc` 从 RG32F pyramid mip0 做同样的线性化；`debug_normal.fs.sc` 零点背景输出黑色。
- 速度：`debug_motion_vectors.fs.sc` 使用 HDRP `DebugFullScreen.shader` 的方向 hue wheel + speed intensity；`camera_motion_vectors.fs.sc` 改为采样 depth pyramid，避免 D3D 同时把深度纹理作为 DSV/SRV；M1 时远平面写零，M3.4 TAA 起改为写背景 camera motion。
- Cluster：`debug_light_cluster.fs.sc` 按 16×16 CPU LightList tile 绘制占用热图、灯光颜色和 tile 网格；读 stage 7 `s_lightList` + stage 5 `s_lightData`。
- 阴影图集：`debug_shadow_atlas.fs.sc` 对方向光/点光 atlas 做保持 1:1 的居中显示、深度对比度重映射；debug 模式先清整个 atlas，避免未初始化区域；stage 1 使用独立 raw-depth sampler。
- Albedo/Roughness：`debug_material.fs.sc` 复用 `hbgMakeSurfaceData`，支持 alpha clip/mask map；`ForwardRenderer` 在 depth prepass 后以 opaque 几何重绘 debug 材质，跳过 sky/forward/wireframe，背景黑色；`debug_color.fs.sc` 直接呈现。仅覆盖 opaque，透明物体不参与。
- `ForwardRenderer::addPresentationPass` 扩展 depth/lightList/两个 shadow atlas 依赖；`debug_ui.cpp` 自动列出全部模式；`backend.cpp` 新增 CLI 别名 `depth`、`light-cluster|cluster`、`directional-shadow-atlas`、`punctual-shadow-atlas`、`albedo`、`roughness`。

### HDRP/Core 源码索引

- HDRP `Runtime/Debug/DebugDisplay.cs:55-139` `FullScreenDebugMode`（DepthPyramid/LightCluster/MotionVectors 等）；`HDRenderPipeline.Debug.cs:540-660` FullScreen Debug/Resolve pass。
- `Runtime/Debug/DebugFullScreen.shader:230-475` depth pyramid 线性化、motion vector hue/arrow、LightCluster 输出；`:312-380` 方向 hue wheel。
- `Runtime/Debug/DebugDisplayMaterial.hlsl:18-62` `_DebugViewMaterialArray` 与 `GetBSDFDataDebug`；`Runtime/Material/Lit/Lit.cs.hlsl:142-202` `diffuseColor`/`perceptualRoughness` debug 输出。
- `Runtime/Lighting/Shadow/DebugDisplayHDShadowMap.shader` 阴影 atlas 深度显示；`HDRenderPipeline.Debug.cs:1040-1065` 方向光/punctual atlas 选择；`HDRaytracingLightCluster.cs:232-292` cluster debug（HDRP 为 ray tracing cluster，本项目是 CPU tile 近似）。

### 简化边界

- LightCluster 已由 M2.3/M6 升级为 64×64×32 CPU cluster + GPU cluster；剩余 debug 限制（无逐 voxel/灯数文本/depth 剔除视图）见 [m12-checklist.md](m12-checklist.md) B07。
- 阴影图集为 debug 模式下整图清空后的 raw depth 对比度显示；无单阴影 overlay、atlas 缩放渐变、cached/area atlas。
- Material debug 只显示 opaque；依赖 depth prepass；透明/alphaMask 外部场景仍受资产边界限制；一次只显示 Albedo 或 Roughness，不含 Metal/AO/Specular/Alpha 等 HDRP 全材料属性。
- Motion Vectors 为 hue wheel + speed intensity，不包含 HDRP 的箭头网格和独立 Motion Vectors Intensity 模式；DepthPyramid 固定 mip0，无 UI mip 选择。
- 当前没有全屏 debug color picker/overlay、GPU cluster 可视化、阴影级联边界标记或 DebugUI 采样点读取。

### 验证

- `hbg_tests` 86 个用例、15842 assertions；`hbg_idl_check` 通过；`test_debug_display` 覆盖 11 模式表，`test_backend` 覆盖新 CLI，`test_uniform_layout` 覆盖 `DebugUniforms` 16B/1 descriptor。
- D3D11/Vulkan：Depth、DepthPyramid、Normal、Albedo、Roughness、LightCluster、Directional/Punctual Shadow Atlas 均出图；DamagedHelmet 的 Albedo 保留贴图，Roughness 为灰度，正常背景为黑。
- D3D11/Vulkan 像素统计：Depth/DepthPyramid/Shadow directional/punctual/Cluster 完全一致；Normal/Roughness maxdiff 0–1，Albedo maxdiff 4（单像素舍入）；Motion 修复 camera depth 采样后 maxdiff 1、mean diff 0；shadow atlas graph `38/38`，point cluster `33/33`，Material debug `27/27`，DepthPyramid/Normal `27/27`。
- `--render-scale=0.5` Albedo graph `25/25`；无 shadow 场景请求 atlas 模式自动回退到 `None`，graph 保持 `27/27`；正常 shadow scene 仍 `37/37`。
- 无 shader/uniform 错误，无资源泄漏。截图：`m110_depth_*`、`m110_pyramid_*`、`m110_normal_*`、`m110_motion_*`、`m110_cluster_*`、`m110_shadow_dir_*`、`m110_shadow_punctual_*`、`m110_albedo_*`、`m110_roughness_*`。

## 已知限制

- 最终输出 v1：固定 EV100 默认 -1.263（等价旧 2× exposure），自动曝光为最终 pass 内 4×4 center-weighted 瞬时测光；ACES approximate/Neutral/None、无 Bloom/ColorGrading/Custom LUT、无 HDR output/Progressive adaptation。Sky/IBL v1（equirect/6:1 + SH9 + GGX mip，无 SSAO/SSR/反射探针）；透明/alphaMask 外部场景未完成。
- Debug v1：LightCluster 已升级为 CPU 64×64×32 + GPU cluster（M2.3/M6）；阴影图集为 raw depth 对比度视图；Material debug 仅 opaque/depth prepass、一次一个属性；Motion 无箭头网格/Intensity 子模式；DepthPyramid 固定 mip0，无全屏 debug overlay/color picker。剩余项见 [m12-checklist.md](m12-checklist.md) B07。
- LightList 已由 M6/M7 升级为 GPU screen AABB/big-tile/FPTL/cluster；M8/M9 已补 64/128 capacity、16-bit packing、layered offset、category/feature flags、light layers。剩余 256 lights/async 见 [m12-checklist.md](m12-checklist.md) A/B 类。
- M1.7 阴影为固定 atlas/4 级联 v1：无 cached/mixed shadow、per-request shadow caster cull、alphaMask caster、area light shadow、volume shadow distance/border/fade、shadow mask、screen-space/contact shadow；PCSS/EVSM/VSM 与动态 atlas resize 未接，PCF 默认 3×3，point face/cascade 边界无混合。
- Transparent depth prepass/MotionVectors、distortion、TAA/motion blur、moving object prevWorld authoring 未完成；当前 glTF 物体为静态。
- Depth pyramid 采用 RG32F min/max + scratch 栅格路径，不是 HDRP R32F min-only compute/atlas。
- glTF sampler wrap/filter、压缩纹理、mask 重打包、skin/morph/animation/Draco/KHR_texture_basisu 未完成。
- Sponza 贴图缺失，无法完整验证纹理链路。
- Vulkan validation layer 旧版 1.3.216；`--bgfx-debug` 默认关闭。
- Release 全量 shaderc/tint 构建可能超时；可用 `HBG_BUILD_SHADER_TOOLS=OFF` + 外部 Debug shaderc 验证，最终 cache 恢复 ON。

## M1 任务

| 任务 | 内容 |
|---|---|
| M1.1 RenderGraph | `[x]` Handle/PassBuilder/compile/execute、死 pass、transient 池、ViewId、校验 |
| M1.2 FrameSettings | `[x]` 128 位 FrameSettings + PipelineSettings 能力门控 + JSON |
| M1.3 ViewState | `[x]` 矩阵/抖动/上帧矩阵、History 双缓冲、RT Scale、全局 CB |
| M1.4 DrawList | `[x]` PassTag/队列过滤、HDRP 队列分段、frustum cull、排序/合批 |
| M1.5 Prepass | `[x]` 深度/法线/运动矢量/深度金字塔 |
| M1.6 光照 v1 | `[x]` CPU 裁剪排序、LightData、BigTile+PerTile LightList、Forward LightLoop |
| M1.7 阴影 v1 | `[x]` 4 级联、点光 6 面/聚光请求、双 atlas/shelf pack、ShadowContext、PCF |
| M1.8 Sky/IBL | `[x]` HDRI 天空、SH9、GGX mip、天空反射 |
| M1.9 最终输出 | `[x]` 固定/自动曝光、ACES/Neutral、sRGB、Debug 覆盖 |
| M1.10 Debug | `[x]` 深度/法线/速度/Cluster/阴影图集/Albedo/Roughness 视图 |

**M1 出口标准**：glTF + HDRI + 级联阴影 + 聚类灯光 + 运动矢量 + Tonemap + RenderGraph + FrameSettings；1080p 60fps（GTX 1660/RX 5600 级）。

## 常用命令

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
py -3 tools/idl_gen.py --check
.\build\vs2022\Debug\hbg.exe --backend=d3d11
.\build\vs2022\Debug\hbg.exe --backend=vulkan
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --frames=3 --screenshot=out.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=<scene.gltf> --frames=3 --screenshot=out.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --frames=3 --render-scale=0.5 --screenshot=scale.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=depth --render-test=<scene.gltf> --frames=3 --screenshot=depth.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=normal --render-test=<scene.gltf> --frames=3 --screenshot=normal.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=albedo --render-test=<scene.gltf> --frames=3 --screenshot=albedo.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=light-cluster --render-test=<scene.gltf> --frames=3 --screenshot=cluster.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-light-data --render-test=<scene.gltf> --frames=3 --screenshot=light.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --sky=<hdri.hdr|exr> --sky-intensity=1 --sky-rotation=0 --frames=3 --screenshot=sky.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --tonemap=neutral --exposure-mode=fixed --exposure-ev100=-1.263 --frames=3 --screenshot=neutral.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --sky=<hdri.hdr|exr> --exposure-mode=auto --exposure-compensation=2 --frames=3 --screenshot=auto.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --debug-exposure=1 --frames=3 --screenshot=debug-exposure.png
```
