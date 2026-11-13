# M6 GPU 光照列表实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M6.0、M6.1、M6.2、M6.3、M6.4 已完成（2026-11-12/13）**。
> 原则：先读 HDRP/bgfx 源码再实现；本文只做索引和设计，HDRP 行为以源码为准。
> 参考：
> - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
> - HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
> - bgfx：`build\vs2022\_deps\bgfx-src`
> - 当前实现：`src\render\lighting\light_list_builder.*`、`shaders\common\light_data.sc`、`shaders\passes\light_screen_aabb.cs.sc`、`src\render\passes\forward_renderer.*`

---

## M6 范围与路线

M6 的主题是**把 CPU `LightListBuilder` 的每帧光源剔除迁移到 GPU**。M6 不增加新的视觉特性，不改变阴影过滤、contact shadows、雾或后处理结果；目标是保持现有光照结果不变，同时减少 CPU 端逐 tile/逐 cluster 的剔除工作，为后续 GPU-driven 几何/阴影阶段提供基础。

M6 v1 采用 **texture-backed GPU light list**：compute 写入现有 `R32_UINT` light-list 纹理，现有着色器读取路径（`shaders/common/light_data.sc`）保持不变。这样先验证 GPU 剔除的正确性和收益，**不在 M6 引入 storage buffer / indirect draw**；完整 buffer/indirect 版本留到 M7。

| 子任务 | 目标 | 依赖 |
|---|---|---|
| **M6.0 GPU light-list 基础设施 + screen AABB** | 新增 GPU 开关/回退、compute pass、screen-space 光源包围盒纹理；先不替换 CPU list | M5.4 |
| **M6.1 GPU big-tile + FPTL** | 64×64 big-tile 粗剔除 + 16×16 FPTL 精剔除，写入现有 FPTL 纹理；deferred/forward opaque 使用 GPU 结果 | M6.0 |
| **M6.2 GPU cluster** | 64×64×32 log-depth cluster list；替换 CPU cluster；M6.1 完成后关闭 CPU builder 的正式路径 | M6.1 |
| **M6.3 集成、回退、调试、性能接线** | 所有消费者使用 GPU list；`--no-gpu-light-list` 回退；debug/诊断/性能日志 | M6.2 |
| **M6.4 M6 验收** | D3D11/Vulkan 矩阵、CPU/GPU 对照、1080p 性能、文档、边界 | M6.0–M6.3 |

### M6 明确不做

- **不做 GPU shadow request/caster culling、indirect shadow draw**：这些需要 GPU draw-list、indirect arguments 和 shadow pass 重排，放到 M7；M6 不改变 `ShadowManager` 的 CPU request/culling/draw 路径。
  - HDRP 的 `BuildGPULightList` 只做 `ClearLightLists -> GenerateLightsScreenSpaceAABBs -> BigTilePrepass -> BuildPerTileLightList -> VoxelLightListGeneration -> BuildDispatchIndirectArguments`（`HDRenderPipeline.LightLoop.cs:675-697`），并不替代 shadow request/caster 管理；shadow request 更新在 `HDGpuLightsBuilder.LightLoop.cs:478-581` 的 `UpdateShadowRequestsAndCalculateShadowIndices`/shadow job 路径。把两者合并会同时改动 shadow atlas packing、per-request culling、shadow draw list 和 M5 已验证路径，超出“迁移 light list”的范围。
- 不做 GPU-driven opaque/transparent draw、Hi-Z occlusion culling、meshlet、mesh shader、bindless/descriptor indexing、DXR/RT。
- 不做 async compute；M6 的 GPU light-list pass 与主渲染同步执行。HDRP 的 `BuildLightListRunsAsync()` 不作为 M6 目标。
- 不做 light capacity 扩展：保持 `kMaxGpuLights = 32`、每 tile/cluster 最多 32 个 punctual lights；64/128 灯容量留到 M7。
- 不做 HDRP 的 light category/layer、feature flags、FPTL/cluster 多分类、decal/env light list。
- 不做 adaptive probe volume、lightmap/shadow mask、面积光/云/体积 VBuffer 新路径。
- 不改变现有 CPU `LightListBuilder` 的 tile/cluster 语义：FPTL 16×16、cluster 64×64、32 个 log-depth slice、固定 33 texel stride。
- 不引入每帧 GPU→CPU readback；GPU list 只通过纹理、截图和 debug view 验证。
- 不改 M3 后处理、M4 fog、M5 shadow/contact 的算法与帧顺序；只把 light-list 的生产者从 CPU 换成 GPU。

### 为什么这些取舍成立

- **shadow culling 不应塞进 light-list pass**：HDRP 的 `BuildGPULightList` 只生产光源列表和 dispatch 参数，shadow request 的更新是独立的 `HDGpuLightsBuilder.LightLoop.cs:478-581` job/CPU 数据路径；项目还有自己的 atlas packing、`DrawList::buildCulled` 和每 request 一个 shadow pass。把 GPU shadow 塞进 M6 会改变 M5 已验证的阴影帧顺序和 caster 提交，不是“同一份 light list”的自然延伸。
- **不用 storage buffer/indirect 是兼容性选择**：HDRP 可以用 `GraphicsBuffer.Target.Structured`、`g_LayeredOffset` 和 indirect dispatch（`LightLoop.cs:366-413`、`HDRenderPipeline.LightLoop.cs:316-388/621-657`），因为它的消费者全部按 buffer 编写；本项目现有 `light_data.sc`、forward/deferred/fog/contact 都按 `R32_UINT` texture + `texelFetch` 读取。M6 v1 先保持读取接口不变，把 buffer/layered/indirect 作为 M7 的消费者重写，避免同时改生产者和所有消费者导致无法定位回归。
- **async 需要另一条队列**：HDRP 在 `HDRenderPipeline.LightLoop.cs:677` 用 `BuildLightListRunsAsync()` 选择异步 compute；本项目 `RenderGraph`/`Encoder` 是单队列同步提交，没有 async queue、跨队列依赖和双重缓冲。M6 同步执行即可验证正确性，避免把“异步”误当成免费收益。
- **32 灯是现有数据契约束缚**：HDRP 的 FPTL 上限是 63、可见灯上限 4096、big-tile 上限 512（`ShaderConfig.cs.hlsl:18/25-26`）；本项目 `GpuLightData` 固定 32、`kLightListStride=33`，`light_data.sc` 和多个 loop 都依赖它。扩到 64/128 会改变纹理布局、shader 循环和 CPU/GPU reference，不属于 M6 的“迁移生产者”。
- **category/layer/feature variant 会改变光照语义**：HDRP 先按 category 分桶并写 feature flags（`lightlistbuild.compute:413-430`），项目 `GpuLightData` 没有 category/layer/feature flags。M6 只需要 FPTL/cluster 的原有 punctual 列表；加入分类会把材质 lighting variant 和 per-light culling mask 一起拉进来，风险远大于 light-list 迁移。
- **不做 GPU readback 是工具限制**：vendored bgfx 没有可用的 texture readback；因此 M6.0 用 CPU reference 单测验证 AABB 数学，用 pass 顺序/诊断验证 GPU 执行，用后续 M6.1 的双路径截图验证列表结果，不伪造 CPU/GPU 数值对照。RenderDoc 安装后可以再补 F11 抓帧。

---

## 当前基线与缺口

M5.4 结束时的光照列表：

- `LightListBuilder::build()` 在 CPU 上同时构建 FPTL 和 cluster（`src/render/lighting/light_list_builder.cpp:150-307`）：
  - FPTL 16×16，big-tile 64×64，固定 `kLightListStride=33`；
  - cluster 64×64×32，固定 33 texel row，最多 32 个 light；
  - 每个 light 用屏幕空间 AABB + view-depth 范围做保守裁剪。
- `ForwardRenderer::updateLightList()` 在每帧 graph 录制前调用 CPU builder，并把结果上传到两张 `R32_UINT` 纹理（`src/render/passes/forward_renderer.cpp:1199-1264`）：
  - `m_lightListTexture`：FPTL；
  - `m_clusterLightListTexture`：cluster；
  - deferred/forward opaque 读 FPTL，transparent/forward-only/fog 读 cluster（`forward_renderer.cpp:1701-1704`、`:3301-3303`）。
- 着色器读取接口固定为：
  - `s_lightList` stage 7，`HBG_LIGHT_LIST_STRIDE=33`、`HBG_LIGHT_CLUSTER_STRIDE=33`；
  - `hbgLightListCount/Index`、`hbgClusterLightListCount/Index`（`shaders/common/light_data.sc:54-105`）；
  - `u_pass_lightListParams`（tile size/count/enabled）与 `u_cluster_params/depthParams`（`config/uniforms/pass.idl`、`config/uniforms/cluster.idl`）。
- `RenderGraph` 已支持 compute pass 和 texture read/write（`src/render/graph/render_graph.h:76-88`、`:120-121`）；contact shadows/exposure 已使用 compute + `TextureDesc::computeWrite` + `Encoder::setImage`。M6 沿用这条通路，不新增 storage buffer 依赖。
- 当前没有 GPU light culling、screen-space AABB texture、big-tile GPU pass 或 GPU list 诊断；`--no-light-cluster` 只回退到 FPTL，`--no-shadows` 不影响 light list。

---

## HDRP 关键结论

1. **HDRP 的 light list 是 GPU compute 生成的**：先由 `GenerateLightsScreenSpaceAABBs` 生成光源屏幕包围盒，再跑 big-tile、FPTL、cluster，最后给 deferred/forward 使用（`HDRenderPipeline.LightLoop.cs:198-312`）。
2. **big-tile 是粗剔除层**：64×64 tile 先产生 coarse light list，FPTL/cluster 再在 coarse list 上做精剔除，避免每个小 tile 遍历全部光源（`lightlistbuild-bigtile.compute:119-208`、`lightlistbuild.compute:155-422`）。
3. **cluster 是 64×64 屏幕 tile × log-depth slice**：HDRP 用 `g_vLayeredLightList`/`g_LayeredOffset`/`g_LayeredSingleIdxBuffer` 保存每 cluster 的 offset/count/index（`lightlistbuild-clustered.compute:239-500`、`:514-690`）；项目 v1 沿用现有的固定 33 texel row，避免一次性改成 layered offset。
4. **HDRP 使用 `StructuredBuffer`/`RWStructuredBuffer` 和 `GraphicsBuffer.Target.Structured`**（`LightLoop.cs:366-413`、`HDRenderPipeline.LightLoop.cs:621-657`）；M6 为了兼容现有纹理消费者，**故意使用 R32_UINT texture 作为 v1 输出**，不是 HDRP 的最终内存布局。buffer/indirect 版本放 M7。
5. **HDRP 使用 fixed/sorted list 并 clamp 数量**：`SORTLIST`、`MAX_NR_COARSE_ENTRIES`、`MAX_NR_BIG_TILE_LIGHTS` 等保证结果稳定；M6 的 GPU list 必须保持**升序 light index**，避免 D3D/VK 因线程写入顺序不同产生像素差异。
6. **HDRP 的 dispatch/indirect 参数由 GPU 构建**（`HDRenderPipeline.LightLoop.cs:316-388`）；M6 v1 不采用 indirect dispatch，固定 `dispatch(ceil(tiles/…))`，保证先验证剔除结果。

---

## HDRP / bgfx 参考索引

| 参考 | 用途 |
|---|---|
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:198-216` | `GenerateLightsScreenSpaceAABBs` 屏幕包围盒 compute |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:222-236` | 64×64 big-tile coarse list |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:240-280` | FPTL 16×16 light list |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:284-312` | cluster light list |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:316-388` | dispatch indirect 参数构建 |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:621-657` | light list/cluster/big-tile buffer 分配 |
| `RenderPipeline/HDRenderPipeline.LightLoop.cs:665-697` | “Build Light List” compute pass 顺序 |
| `Lighting/LightLoop/LightLoop.cs:366-413` | GPU light/cull/bounds buffers |
| `Lighting/LightLoop/lightlistbuild-bigtile.compute:119-208` | big-tile 粗剔除、LDS、排序 |
| `Lighting/LightLoop/lightlistbuild.compute:155-422` | FPTL 精剔除、coarse list、输出布局 |
| `Lighting/LightLoop/lightlistbuild-clustered.compute:239-500` | cluster tile/depth、coarse list、LDS |
| `Lighting/LightLoop/lightlistbuild-clustered.compute:514-690` | cluster 精确相交、offset/count/index 输出 |
| `Lighting/LightLoop/scrbound.compute:514-904` | screen-space AABB 数学与 min/max 输出约定（M6 简化为 8-corner CPU-equivalent） |
| `Lighting/LightLoop/LightCullUtils.hlsl:1-33` | screen bounds/light cull index 约定 |
| `Lighting/Light/HDGpuLightsBuilder.LightLoop.cs:478-581` | shadow request/index 更新，与 light-list 分离 |
| `Lighting/LightLoop/LightLoopDef.hlsl:274-320` | cluster index 与 layered list 采样 |
| `Lighting/LightLoop/ClusteredUtils.hlsl:16-45` | log-depth slice 映射 |
| `Lighting/LightLoop/ClusteredUtils.hlsl:106-111` | layered offset buffer 索引 |
| `build\vs2022\_deps\bgfx-src\examples\24-nbody` | bgfx compute + `BUFFER_RO/WO/RW` + `setBuffer` |
| `build\vs2022\_deps\bgfx-src\examples\37-gpudrivenrendering` | bgfx GPU culling/LDS/间接绘制参考 |
| `build\vs2022\_deps\bgfx-src\examples\48-drawindirect` | bgfx indirect buffer 参考（M7 使用） |
| `include/bgfx/bgfx.h:2836-2839` | `BGFX_BUFFER_COMPUTE_*` 语义 |
| `include/bgfx/bgfx.h:5131-5204` | `bgfx::setBuffer` 绑定 |

---

## M6 数据契约（v1）

### 输入

复用现有数据，不新增 light buffer：

- `s_lightData`：RGBA32F，6 texels/light，stage 5；布局见 `shaders/common/light_data.sc:5-23`。
- `u_pass_lightCounts`：total/directional/punctual/shadowed。
- `u_global_viewProj`、`u_global_view`、`u_global_screenSize`。
- `u_pass_lightListParams`：FPTL tile size/count/enabled。
- `u_pass_lightScreenSize`：render-resolution 屏幕尺寸/倒数；M6.1 起 AABB/big-tile/FPTL 使用它，不依赖 compute 中的 `u_global_screenSize`。
- `lightViewRows`：6×1 RGBA32F，`viewProj` row0..3 + `view` row2；M6.1 起 AABB 从该纹理取矩阵行。
- `u_cluster_params`、`u_cluster_depthParams`：cluster tile/slice/near/far/log base/enabled。

### 新增纹理

| 纹理 | 格式 | 尺寸 | 用途 |
|---|---|---|---|
| `lightScreenBounds` | RGBA32F | `2*kMaxGpuLights` × 1 | 每 punctual light 的屏幕像素 AABB + view-depth 范围 |
| `lightBigTileList` | R32_UINT | `bigTilesX*33` × `bigTilesY` | 64×64 big-tile coarse list；layout 同 FPTL：count + 32 indices |
| `lightList` | R32_UINT | 现有尺寸 | M6.1 由 GPU 写 FPTL；消费者不变 |
| `clusterLightList` | R32_UINT | 现有尺寸 | M6.2 由 GPU 写 cluster；消费者不变 |

`lightScreenBounds` 的 texel 约定：

```text
texel 2*i + 0: minX, minY, minViewDepth, maxViewDepth
texel 2*i + 1: maxX, maxY, visible, 0
```

- `i` 是绝对 light index；方向光不写入，只处理 `directionalCount..lightCount`。
- 坐标为 render-resolution 像素坐标，和 CPU `projectLightBounds` 一致。
- `visible=0` 表示完全在相机后方/屏幕外；`visible=1` 表示可使用。
- 完全跨过相机平面的 light 按 CPU 行为退化为全屏 AABB；不能因 `w<=0` 直接丢弃整屏可见光。

所有 list 输出保持现有 `count + 32 indices`、`kLightListStride=33`，未使用槽写 0；这样 `shaders/common/light_data.sc` 不需要改接口。

### GPU 开关

- 新增 `PipelineSettings::supportGpuLightList`（或等价的 pipeline capability）；M6.0–M6.3 开发期默认 **false**，M6.4 验收通过后已切 **true**。
- CLI：`--gpu-light-list` / `--no-gpu-light-list`；DebugUI 提供同名 toggle；`--no-light-cluster` 仍关闭 cluster，只保留 FPTL。
- GPU 路径要求 compute 可用；不可用时打印一次警告并回退 CPU `LightListBuilder`，不能黑屏或输出空 light list。
- `--no-gpu-light-list` 必须保持与 M5.4 完全相同的 CPU 路径，作为对照基线。

### 帧顺序

```text
Shadow
Prepass / GBuffer
GPU Screen AABB
GPU Big Tile
GPU FPTL
GPU Cluster（启用时）
Contact Shadows / SSAO / Deferred Lighting / Fog / Transparent
```

- GPU light-list pass 必须在 contact shadows 和所有 light loop 之前；不能放在 deferred lighting 之后。
- M6 v1 只依赖 light data/uniform，不读 depth；因此可以放在 prepass 后、GBuffer 前。若 M6.2 需要 cluster depth 优化，再按 HDRP 方式后移到 depth 可用位置，但不得改变 M6 出口语义。
- GPU 路径开启时跳过 `LightListBuilder::build()`；CPU 路径仍保留用于 fallback 和对照。

---

## M6.0 GPU light-list 基础设施 + screen AABB

状态：`[x]`（2026-11-12）

目标：建立 GPU light-list 的开关、compute pass、纹理生命周期和 screen-space 光源包围盒；此时光照结果仍走 CPU list，GPU 结果只用于验证/后续阶段。

### 实现

- 新增 `shaders/passes/light_screen_aabb.cs.sc`：
  - 64 threads，thread 0..31 对应绝对 light slot；方向光/无效/越界 slot 写零，punctual slot 从 `s_lightData` `texelFetch` 取 position/range/type。
  - 用与 CPU `projectLightBounds` 相同的 8-corner sphere AABB 投影；
  - 处理 `clip.w<=0`、全 behind、屏幕外、完全覆盖屏幕；
  - 输出 `minX/minY/minViewDepth/maxViewDepth` 和 `maxX/maxY/visible`。
  - view depth 用 `u_global_view`，near/far 取 `u_cluster_depthParams.x/y`（与 CPU cluster 一致），范围 `max(centerDepth-range, near)` 到 `min(centerDepth+range, far)`。
- 新增 `m_lightScreenBoundsTexture`：
  - `TextureDesc::computeWrite=true`，RGBA32F，`2*kMaxGpuLights × 1`，point/clamp；尺寸固定，不需要随 resize 重建；init 创建，shutdown 释放。
- `ForwardRenderer` 新增 `m_gpuLightListEnabled`、`setGpuLightListEnabled()`、`gpuLightListActive()`；M6.0 不替换 CPU list。
- 新增 `addGpuLightScreenBoundsPass(graph, resources)`：
  - `graph.addComputePass("GPU Light Screen AABB")`；
  - read `lightData`，write `lightScreenBounds`；
  - 绑定 stage 5 lightData、stage 1 输出 image（按 M3 的“sampler 与输出 image 不同 stage”约定）；
  - upload global/pass/cluster uniforms，dispatch `1,1,1`（一个 64-thread group 覆盖 32 个绝对 slot；与 CPU reference 的固定 32 上限一致）。
- `shader_key.h` 增加 `LightScreenAABB`；`shader_library`/`tools/shader_build.py`/manifest 同步 compute program；manifest 缺该 entry 时只禁用 GPU path，不破坏 CPU path。
- 诊断：`FrameDiagnostics` 增加 `gpuLightListRequested/gpuLightListActive/gpuLightListPasses`；首帧日志打印 `gpu light list: requested=... active=... passes=...`，`render performance` 增加 `gpu_light_list`/`light_list_passes`。
- 测试：CLI 正负参数、pipeline capability、`LightListBuilder::projectLightScreenBounds` CPU reference、compute program 可加载、`--no-gpu-light-list` 不改变现有画面。

### 实际实现与验证

- HDRP 参考：`scrbound.compute:514-904` 的屏幕 AABB 数学与输出约定、`LightCullUtils.hlsl:23-33` 的 min/max bounds index；M6.0 按项目 CPU `projectLightBounds` 的 8-corner 语义实现，而不是直接移植 HDRP 的 4-thread 多边形裁剪（后者需要 `SFiniteLightBound`，当前项目没有）。
- 新增 `src/render/lighting/gpu_light_data.h` 的 `LightScreenBounds`；`LightListBuilder::projectLightScreenBounds()` 作为 CPU reference，覆盖 behind/offscreen/全屏、point/spot 共用 sphere AABB。
- `src/render/passes/forward_renderer.cpp` 的 pass 顺序为 `... -> Camera Motion Vectors -> GPU Light Screen AABB -> GBuffer Clear -> GBuffer -> Deferred Lighting -> ...`；pass 使用 `allowCulling(false)`，防止无消费者时被 RenderGraph 裁掉。
- 验证（1280×720，`m28_blend_view.gltf`，`--no-shadows`，D3D11/Vulkan）：GPU off D3D/VK **0 diff**、GPU on D3D/VK **0 diff**；GPU 开启 pass 数 **29→30**、`compute=1`、`gpu_light_list=true light_list_passes=1`；关闭时恢复 `gpu_light_list=false light_list_passes=0`。D3D11 与 Vulkan 之间最大通道差为 **1–2**，与 GPU off 基线相同，不是 light-list 引入的画面变化。
- 门禁：`hbg_tests` **201 cases / 18,607 assertions**、`ctest` 2/2、`hbg_idl_check`、D3D11/Vulkan `--gfx-test`、render-test 无泄漏通过；截图/日志在 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m60`，未进仓库。

### 出口

- D3D11/Vulkan 都能创建/写入 `lightScreenBounds`，无 validation error、无泄漏。
- `--no-gpu-light-list` 与 M5.4 基线 0 diff；GPU 开关不影响 CPU listing。
- screen AABB 的 CPU reference 单测覆盖 behind/offscreen/覆盖全屏/point/spot。
- 首帧日志和 `render performance` 能区分 GPU 开关与 pass 数。

---

## M6.1 GPU big-tile + FPTL

状态：`[x]`（2026-11-13）

目标：在 GPU 上生成 64×64 big-tile coarse list 和 16×16 FPTL，并让 deferred/forward opaque 使用 GPU FPTL；cluster 暂时仍由 CPU 生成，作为过渡。

### 实现

- 新增 `shaders/passes/light_big_tile.cs.sc`：
  - 一个 workgroup 处理一个 64×64 tile，64 threads；
  - 从 `lightScreenBounds` 读取每个 punctual light 的 AABB；
  - 用 `shared uint mask`/`atomicOr` 收集 32 个 light，按 light index 升序写 `count + 32 indices`；
  - 输出 `lightBigTileList`（R32_UINT，`bigTilesX*33 × bigTilesY`）。
- 新增 `shaders/passes/light_fptl.cs.sc`：
  - 一个 workgroup 处理一个 16×16 tile，64 threads；
  - 先读 big-tile coarse list，再对每个候选 light 检查是否与当前 tile 屏幕矩形相交；
  - 同样用升序 mask 输出到现有 `m_lightListTexture`；
  - `count` 超过 32 时置 overflow 统计，但 list 保持最多 32 个。
- `ForwardRenderer` 新增：
  - `m_lightBigTileTexture`，`computeWrite=true`，尺寸由 `LightListBuilder` 的 big-tile helper 计算；
  - `addGpuLightListPasses()`：AABB → big-tile → FPTL，按顺序加入同一个 graph；
  - GPU FPTL 开启时不再上传 CPU FPTL 纹理，但 CPU builder 仍可为 cluster 过渡生成数据；
  - `m_passUniforms.lightListParams` 使用 GPU 路径的 tile counts；`u_pass_lightCounts` 不变。
- shader 读取约定：
  - `s_lightData` stage 5；
  - `lightScreenBounds` stage 1（big-tile 输入）；
  - big-tile list stage 2（FPTL 输入）；
  - FPTL 输出 image stage 3；
  - 不在同一 dispatch 中复用 sampler/image stage。
- 测试：
  - `--gpu-light-list` vs `--no-gpu-light-list` 在 deferred/forward opaque 场景逐像素 **0 diff**；若无法 0 diff，必须定位到具体 tile/pixel 并记录，不能用大阈值掩盖。
  - D3D11/Vulkan GPU FPTL 图一致（0 diff 或 ≥60 dB，按实际浮点情况记录）。
  - 1/8/32 lights；相机后方/屏幕外/大 range 覆盖全屏；`--no-light-cluster` 回退 FPTL。
  - 单元测试覆盖 big-tile/FPTL 纹理尺寸和 CPU reference 的 list 顺序。

### 实际实现与验证

- HDRP 参考：`scrbound.compute` 生成屏幕 AABB；`lightlistbuild-bigtile.compute` 的 workgroup 对应一个 64×64 big tile；`lightlistbuild.compute` 的 workgroup 对应一个 FPTL tile，dispatch 为 `numTilesFPTLX × numTilesFPTLY`（`HDRenderPipeline.LightLoop.cs:222-280`）。
- big-tile 与 FPTL 都用 `gl_WorkGroupID.xy` 直接作为 tile 坐标，dispatch 分别为 `bigTileCountX × bigTileCountY` 和 `tileCountX × tileCountY`。不能用 `gl_GlobalInvocationID.xy / tileSize` 再按 tile 数 dispatch：16×16 tile 的 64-thread group 会被错误展开成多行/越界写入。
- FPTL 的 coarse tile 是 `tile >> 2`（64/16），不能继续用全局线程坐标除以 64；否则只有第一行/越界列能读到 big-tile list。
- AABB 的 view-proj 行、view row2 通过 6×1 RGBA32F `lightViewRows` 上传（CPU 每帧填 `viewProj` row0..3 和 `view` row2），AABB/FPTL 的屏幕尺寸用新增 `u_pass_lightScreenSize`；不依赖 compute 中的矩阵/全局屏幕 uniform 布局。
- 纹理绑定保持：`s_lightData` stage 5、`lightScreenBounds` stage 1、`lightBigTileList` stage 2、FPTL 输出 image stage 3；AABB 输出 image stage 1；`m_lightBigTileTexture` 与 `m_lightListTexture` 均 `computeWrite=true`。
- AABB 输出严格对齐 CPU `LightScreenBounds` 契约：min texel `(minX,minY,minDepth,maxDepth)`，max texel `(maxX,maxY,visible=1,0)`；big-tile/FPTL 用 `max.z > 0.5` 判断 visible，不能把 maxDepth 写到 visible 槽，否则近处灯光（maxDepth < 0.5）会被错误剔除。
- 验证（D3D11，2 帧，CPU vs GPU 逐像素）：
  - 640×360、`--render-scale=0.5`、`m61_one/eight/thirtytwo/full`：全部 **0 diff**。
  - 1280×720、`m61_thirtytwo/m61_full`：**0 diff**。
  - 1920×1080、`m61_thirtytwo`：FPTL `3960×68`、big-tile `990×17`，**0 diff**。
  - 默认 cluster 与 `--no-light-cluster`、forward、`--pipeline-settings=config/pipeline.json --frame-settings=config/frame_settings.json` 的 deferred：**0 diff**。
  - `--contact-shadows`、`--no-shadows`：**0 diff**。
  - Vulkan GPU vs CPU：**0 diff**；`--debug-view=light-cluster --debug-light-cluster-slice=-2`：**0 diff**。
  - GPU path 日志 `passes=3`；`--no-gpu-light-list` 保持原 CPU path（M6.1 时为开发期默认 false，M6.4 已切 true）。
- 门禁：`ctest` 2/2（`hbg_tests` + `hbg_idl_check`）通过；未修改 CPU `LightListBuilder` 的 FPTL 集合/升序语义。

### 出口

- FPTL 结果与 CPU reference 的 light index 集合和顺序一致；deferred/forward opaque 画面与 CPU 路径 0 diff。
- GPU 写的 FPTL 纹理不被 CPU 上传覆盖；resize/shutdown 无泄漏。
- 仍保留 `--no-gpu-light-list` 的完整 CPU 路径。

---

## M6.2 GPU cluster

状态：`[x]`（2026-11-13）

目标：在 GPU 上生成 64×64×32 cluster list，替换 CPU cluster；与 M6.1 的 GPU FPTL 一起，使 GPU 开启时不再调用 CPU `LightListBuilder::build()`。

### 实现

- 新增 `shaders/passes/light_cluster.cs.sc`：
  - dispatch 为 `(clusterTilesX, clusterTilesY, 32)`，一个 workgroup 处理一个 `(tile,slice)`，64 threads；
  - 输入 `lightScreenBounds`、`lightBigTileList`、`s_lightData` 和 `s_lightViewRows`；
  - cluster 屏幕矩形 = 64×64 tile；候选来自同一 64×64 big-tile list；深度范围用 light position/range 与 view row2 计算，再使用与 CPU 相同的 log-depth slice 公式；
  - 用升序 mask 写 `count + 32 indices` 到现有 `m_clusterLightListTexture`；未使用槽写 0；总数上限 32，不写越界；
  - 纹理布局保持 `row = tileY * 32 + slice`、`column = tileX * 33`，消费者接口不变。
- `ForwardRenderer`：
  - `addGpuLightListPasses()` 顺序为 AABB → big-tile → FPTL → cluster；cluster 在 contact shadows/light loop 之前；
  - `m_clusterLightListTexture` 创建时设置 `computeWrite=true`；cluster pass 输出 image stage 3；
  - GPU 开启时 `updateLightList()` 只清空 CPU builder，不调用 `build()`；GPU 不可用或 pass 前置条件失败时 `updateCpuLightList()` 完整回退；
  - `--no-light-cluster` 时跳过 cluster pass，transparent/forward-only/fog 回到 FPTL；
  - GPU 路径 `passes=4`，关闭 cluster 时 `passes=3`。
- 消费者不变：`hbgClusterLightListCount/Index` 继续读同一张 `R32_UINT` 纹理；fog、transparent/forward-only、debug 的绑定路径不变。

### 实际实现与验证

- HDRP 参考：`lightlistbuild-clustered.compute` 用 screen bounds + coarse list + `SnapToClusterIdx`/`ClusterIdxToZ` 生成 cluster list；项目固定 64×64×32，因此按 `(tile,slice)` 一 group 实现，并保持 CPU `LightListBuilder` 的升序集合语义。
- 关键修正：
  - **compute 的 `u_cluster_params.w` 在 D3D11/Vulkan compute 中不可靠**（读成 1）；cluster slice 数在 `light_data.sc`、`light_cluster`、debug 中统一使用常量 32。`ClusterUniforms` 仍保留 `.w=32` 供 CPU/debug 使用。
  - **compute 的 `u_pass_lightCounts`/`u_pass_lightScreenSize` 也不可靠**；AABB/big-tile/FPTL/cluster 改读新增的 `u_cluster_lightCounts`、`u_cluster_screenSize` 镜像字段，由 `m_clusterBinder` 上传。
  - **`lightViewRows` 6×1 纹理的 pitch 必须是 6×16=96**；旧值 16 会让 Vulkan 只写入/读取首 texel，导致 view row2 和深度错误。
  - cluster 深度范围不再依赖 AABB depth 字段，而是从 `s_lightData` 的 position/range + `s_lightViewRows` 的 view row2 计算；这与 CPU `viewDepth` 语义一致，同时避开 AABB depth 的跨后端差异。
- 验证：
  - `--debug-view=light-cluster`：D3D11/Vulkan、GPU vs CPU、slice `-1/0/5/15/25/31` 全部 **0 diff**。
  - transparent 1280×720、1920×1080：D3D11/Vulkan CPU vs GPU **0 diff**。
  - Vulkan deferred、fog、`--no-light-cluster`：**0 diff**；D3D11 fog/contact/no-cluster：**0 diff**。
  - 1080p FPTL opaque：**0 diff**；cluster 纹理尺寸保持 `30×17×32`（1080p，`990×544` texels）、`20×12×32`（1280×720，`660×384` texels）。
  - GPU path 日志 `passes=4`；`--no-light-cluster` 为 `passes=3`，`--no-gpu-light-list` 保持原 CPU path。
- 门禁：`ctest` 2/2（`hbg_tests` + `hbg_idl_check`）通过；`ClusterUniforms` 布局/descriptor 单测更新为 4 个 vec4。

### 出口

- GPU cluster 与 CPU cluster 的每 cluster light 集合和顺序一致；transparent/forward-only/fog 画面 0 diff。
- GPU 开启时 CPU `LightListBuilder::build()` 不再执行；GPU 关闭时行为与 M5.4 完全一致。
- 不改变 contact shadows 的 packed mask、fog 的 cluster 采样或 deferred/forward 的 light loop 顺序。

---

## M6.3 集成、回退、调试、性能接线

状态：`[x]`（2026-11-13）

目标：把 GPU light-list 作为完整可选路径接入 forward/deferred/fog/transparent/contact，并补齐回退、诊断、CLI/UI 和性能记录。

### 实现

- `PipelineSettings` capability + CLI/UI 门控（不新增 FrameSettings bit）：
  - M6.3 开发期 `supportGpuLightList=false` 默认（M6.4 已切 true）；`--gpu-light-list` 同时要求 compute + shader program 可用；
  - `--no-gpu-light-list` 强制 CPU；
  - `--no-light-cluster` 只关闭 cluster，不关闭 FPTL。
- `ForwardRenderer::updateLightList()` 拆成：
  - `updateCpuLightList()`：现有 CPU builder + texture upload；
  - `addGpuLightListPasses()`：AABB → big-tile → FPTL → cluster（启用时）；
  - GPU 开启时 CPU 版本只清空/不 build；日志和 `FrameDiagnostics` 标记实际路径。
- 资源生命周期：
  - `lightList`/`clusterLightList`/`lightBigTileList`/`lightScreenBounds` 全部 `computeWrite=true`；
  - resize 重建、shutdown 释放；`--no-gpu-light-list` 时 GPU 纹理可以不创建或保留但不使用。
- Debug：
  - `--debug-view=light-cluster` 的 `-2`（legacy FPTL）、`-1`（per-pixel slice）、`0..31`（fixed slice）在 GPU 纹理上工作；
  - 首帧/每帧日志输出 `gpu_light_list=on/off`、pass 数、FPTL/cluster 尺寸；
  - 可选 DebugUI：GPU light list toggle、当前路径、pass 数。
- 性能：
  - `render performance` 增加 `gpu_light_list`、`light_list_passes`、`light_list_cpu_ms`；
  - CPU 路径记录 `LightListBuilder::build()` wall；GPU 路径该值为 0，GPU 总帧/GPU timer 不退化；
  - 1080p 300 帧分别测 CPU/GPU、forward/deferred、8/16/32 lights、cluster on/off、fog/transparent。
- 错误处理：
  - compute 不可用、program 缺失、纹理创建失败 → 一次性 warning + CPU fallback；不能每帧刷日志；
  - shader/reflection 失败 → 禁用 GPU 路径，保持 CPU baseline。
- 测试：
  - `--gpu-light-list`/`--no-gpu-light-list` 解析与 sanitize；
  - `--no-light-cluster` 与 GPU FPTL 回退；
  - graph 顺序断言（AABB → big-tile → FPTL → cluster → contact/lighting）；
  - resize、render-scale 0.5、shutdown 无泄漏。

### 实际实现

- 参考 HDRP `BuildGPULightList` 的单一 compute pass 描述和 `ProfilingSampler`：本项目的 list 仍拆为 AABB/big-tile/FPTL/cluster 四个同步 compute pass，由 `addGpuLightListPasses()` 统一记录；不引入 HDRP 的 async/indirect。
- `ForwardRenderer::updateLightList()` 保留公共 rows/尺寸/路径判定；CPU 生产拆为 `updateCpuLightList()`（builder + texture upload），GPU 生产为 `addGpuLightListPasses()`。GPU 开启时 `m_lightListBuilder.clear()`，逐帧 CPU build 为 0；GPU pass 创建失败时调用 `updateCpuLightList()` 完整回退。
- DebugUI 增加 `GPU light list` toggle，并显示 `GPU / CPU fallback`、pass 数、CPU build ms；Application 只在该 toggle 变化时调用 `setGpuLightListEnabled()`，不改变其它 FrameSettings。
- `FrameDiagnostics` 增加 `gpuLightListFallback`、`lightListCpuMs`；保留 requested/active/passes。`render performance` 增加 `gpu_light_list_requested`、`gpu_light_list_fallback`、`light_list_cpu_ms`，首帧日志也输出这三项和 FPTL/big-tile/cluster 尺寸。
- `lightList`、`clusterLightList`、`lightBigTileList`、`lightScreenBounds` 均 `computeWrite=true`；resize 通过 `ensure*Texture()` 重建，shutdown 释放。GPU 路径关闭时不依赖 GPU 纹理，CPU 纹理仍可创建/使用。
- 路径不可用（compute/IMAGE_RW/program/texture 任一缺失）只警告一次并回退；进入 GPU 路径后若 pass 记录失败，也会回到 CPU 并输出一次性 warning，不写空 list。
- Debug `--debug-view=light-cluster` 的 `-2`（legacy FPTL）、`-1`（per-pixel）、`0..31`（fixed slice）均读取当前 GPU 纹理；`--no-light-cluster` 仍保留 GPU FPTL（3 pass）。
- CLI 测试覆盖 `--gpu-light-list`、`--no-gpu-light-list`、后写覆盖、`--no-light-cluster` 独立性；M6.4 起 DebugUI 默认 `gpuLightList=true`，并跟随 pipeline 配置。

### 验证

构建/测试：`vs2022-debug` 与 `vs2022-relwithdebinfo` 编译通过；`ctest` 两个配置均 2/2；D3D11/Vulkan `--gfx-test` 两种配置均通过；render-test 全部 `liveResourceCount=0`、`render test passed`。`addGpuLightListPasses()` 的录制顺序固定为 AABB → big-tile → FPTL → cluster（无 cluster 时 3 pass），与 `passes=4/3` 日志一致。

2-frame 对照（640×360，D3D11/Vulkan，除注明外均为 CPU/GPU 同后端 0 diff）：

- forward transparent：**0 diff**；deferred：**0 diff**；contact shadows：**0 diff**；`--no-light-cluster`：**0 diff**。
- opaque 8-light fog：D3D11/Vulkan **0 diff**；8/16 lights forward：D3D11 **0 diff**。
- `--debug-view=light-cluster --debug-light-cluster-slice=-2`：D3D11/Vulkan **0 diff**。
- `--render-scale=0.5`：D3D11/Vulkan 同后端 **0 diff**；Vulkan CPU vs D3D CPU 跨后端约 31 px、max1（基线量级，不是 list 差异）。
- `--debug-light-cluster-slice=-1/0/5/15/25/31`：沿用 M6.2 GPU 纹理验证，D3D11/Vulkan CPU/GPU **0 diff**。
- DebugUI 1280×720 冒烟截图 `m63_ui.png`：GPU requested/active=true、passes=4、cpu_ms=0.000，无 shader/graph/leak 错误。
- 回退实测：临时移走 `shaders/build/dxbc/light_cluster.cs.bin` 后运行 GPU 路径，输出 **1 次** warning，`active=false fallback=true passes=0 cpu_ms=1.227`，截图与 CPU baseline **0 diff**；恢复 binary 后 GPU passes=4。
- 窗口 resize：运行中 `640×360 → 784×411`，GPU tiles `40×23 → 49×26`、cluster `13×7×32`（`429×224` texels），无 graph/shader/leak 错误。

1080p 300 帧性能（D3D11，RelWithDebInfo，`--no-vsync`，同机；CPU 为 `--no-gpu-light-list`，GPU 为 `--gpu-light-list`）：

| 用例 | CPU fps | GPU fps | CPU light-list build（末帧） |
|---|---:|---:|---:|
| 8 punctual，forward，cluster on | 185.3 | 208.4 | 1.236 ms |
| 16 punctual，forward，cluster on | 144.3 | 144.3 | 1.401 ms |
| 32 punctual，forward，cluster on | 132.5 | 142.2 | 1.884 ms |
| 32 punctual，forward，cluster off | 128.0 | 133.3 | 2.400 ms |
| 32 punctual，deferred，cluster on | 113.7–130.4 | 114.8–135.6 | 2.0–2.6 ms |
| 32 punctual，transparent，cluster on | 115.9 | 120.4 | 2.0 ms |
| 32 punctual，opaque fog，cluster on | 39.3–52.4 | 46.2–48.3 | 1.9–2.9 ms |

GPU 路径所有用例 `light_list_cpu_ms=0.000`、`gpu_light_list_requested=true`、`gpu_light_list_fallback=false`；cluster on/off 分别为 `passes=4/3`。forward/deferred/transparent 未见回退；fog 受 GPU 时钟与 CPU 调度影响，区间重叠，GPU cluster 成本与 CPU builder 收益接近。单次 release 日志见 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m61\rel_*.log`。

已知边界（M6.4 已修复）：`transparent + volumetric fog` 的 transparent-only 场景没有 depth prepass，`Camera Motion Vectors` 因缺少 depth pyramid 未写入，fog temporal 却读取该 transient texture，触发 RenderGraph “reads transient Motion Vectors before”。M6.4 让 `Camera Motion Vectors` 在无 depth pyramid 时回退到 scene depth，对齐 HDRP 直接使用 depth buffer 的做法（`HDRenderPipeline.Prepass.cs:1651-1675`）；修复后 transparent+fog D3D/VK CPU/GPU **0 diff**、D3D/VK **84.91 dB**。

### 出口

- 所有光照消费者在 GPU 开启时读取正确的 list；GPU 关闭时与 M5.4 基线 0 diff。
- Debug view、日志、性能字段能明确区分 CPU/GPU 路径。
- 任何 GPU 资源失败都能回退 CPU，不出现空列表/黑屏。

---

## M6.4 M6 验收

状态：`[x]`

目标：完成 D3D11/Vulkan 验收矩阵、CPU/GPU 对照、性能和文档回填；决定是否默认开启 GPU light list。

### 验收结果（2026-11-13）

实现与修复：

- **transparent-only + fog**：`addCameraMotionVectorsPass()` 在没有 depth pyramid（无 depth prepass）时改用 scene depth 作为 fullscreen depth source；HDRP `RenderCameraMotionVectors` 同样直接把 `depthBuffer` 作为 depth attachment 并 fullscreen 采样（`HDRenderPipeline.Prepass.cs:1651-1675`）。修复后 transparent-only 场景也会写入 `Motion Vectors`，`Volumetric Fog Raymarch` 不再读取未写入的 transient texture。
- **默认开关**：`PipelineSettings::supportGpuLightList` 和 `config/pipeline.json` 切为 **true**；`--no-gpu-light-list` 保留完整 CPU 回退。`DebugDisplaySettings::gpuLightList` 默认同步为 true，Application 仍按实际 pipeline 配置覆盖。
- **初始化顺序**：GPU 默认开启时在 `ForwardRenderer::init()` 的 `updateLights()` 前创建 big-tile 纹理，避免首帧前误报一次 fallback warning；若随后 `--no-gpu-light-list` 覆盖为 CPU 路径，纹理保留但不会进入 GPU list 生产者。
- 新增 `PipelineSettings::defaults()` GPU 默认值单测，更新 DebugUI 默认值单测。

视觉矩阵（640×360，4 帧；D3D11/Vulkan 同后端 CPU/GPU 对照；GPU 路径均为 `passes=4`，no-cluster 为 `passes=3`）：

| 用例 | 同后端 CPU/GPU | D3D/VK GPU | passes CPU→GPU |
|---|---|---:|---:|
| forward 32 | **0 diff** | 93.52 dB（20 px，max1） | 25→29 |
| deferred 32 | **0 diff** | 93.10 dB（22 px，max1） | 27→31 |
| transparent 32 | **0 diff** | 93.52 dB（20 px，max1） | 4→8 |
| fog punctual 8 | **0 diff** | 91.90 dB（25 px，max2） | 27→31 |
| transparent + fog 32 | **0 diff** | 84.91 dB（131 px，max2） | 6→10 |
| contact shadows 32 | **0 diff** | 91.48 dB（32 px，max1） | 86→90 |
| 8 lights | **0 diff** | 92.38 dB（26 px，max1） | 25→29 |
| 16 lights | **0 diff** | 93.52 dB（20 px，max1） | 25→29 |
| `--no-light-cluster` | **0 diff** | 93.52 dB（20 px，max1） | 4→7 |
| 0.5 render scale | **0 diff** | 90.09 dB（44 px，max1） | 23→27 |
| spot cone scene | **0 diff** | 99.00 dB（0 px） | 25→29 |

- GPU off vs M5.4 base（`m28_blend_view` + Uffizi + deferred + no shadows/fog，1280×720）：D3D/VK 均 **0 diff**。
- 相机后方/屏幕外：`tests/render/test_light_list.cpp:49-73` 覆盖中心、远屏外、相机后方和近平面大范围 light；spot cone 场景 D3D/VK CPU/GPU **0 diff**，无全屏 light。
- GPU 路径 `light_list_cpu_ms=0.000`，CPU builder 不再逐帧执行；默认路径 D3D/VK 均 `gpu_light_list=true`、`fallback=false`。
- 强制 fallback（临时移除 `light_cluster.cs.bin`，默认开启状态）：1 次 warning、`passes=0`、`fallback=true`、CPU build **0.977 ms**，截图与 CPU baseline **0 diff**；binary 已恢复。

性能（RelWithDebInfo，1080p，300 帧，`--no-vsync`，`m61_thirtytwo` + Uffizi，`postprocess_quality` + TAA/Bloom/DOF/Motion Blur）：

| 路径 | D3D11 CPU | D3D11 GPU | Vulkan CPU | Vulkan GPU |
|---|---:|---:|---:|---:|
| forward 32 full post（3 次范围） | 99.6–114.7 fps | 112.3–122.2 fps | 90.9–94.0 fps | 91.1–99.2 fps |
| deferred 32 full post（单次） | 112.7 fps | 115.7 fps | 97.9 fps | 95.9 fps |

默认 GPU forward 单次复测：D3D11 **121.9 fps**、Vulkan **99.8 fps**；deferred full post 单次 D3D11 CPU/GPU **112.7/115.7** fps、Vulkan **97.9/95.9** fps；均 >60 fps。GPU `passes=115`（CPU 111），`post_passes=24`，`light_list_cpu_ms=0`。

base（无后处理）参考：D3D11 8 lights CPU **184.5–210.4** / GPU **210.9–230.9** fps，32 lights CPU **127.9–139.7** / GPU **127.1–134.3** fps；Vulkan 8 lights CPU **191.7–192.5** / GPU **184.5–192.1** fps，32 lights CPU **106.2–115.3** / GPU **107.4–110.3** fps。base 32 在多次单次测量中接近持平或略慢，不能声称 GPU 路径在所有 workload 都更快；full post、D3D11 forward 和 CPU builder 清零满足 M6.4 门禁。

默认决策：**开启**。理由：视觉全矩阵 0 diff；D3D/VK 最低 84.91 dB（≥60）；GPU 路径移除 CPU `LightListBuilder::build()`；full post 1080p >60 fps 且未出现明显退化。保留 `--no-gpu-light-list` 和失败自动 fallback；M6 v1 仍是 texture-backed、同步、32 lights，不含 buffer/indirect/async。

门禁：`hbg_tests` **203 cases / 19,708 assertions**；`ctest` Debug/RelWithDebInfo **2/2**；D3D11/Vulkan `--gfx-test` 均通过；`git diff --check` 无 whitespace error（仅 CRLF warning）。验收截图和日志在 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m61\m64_*`，临时场景在 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m61\`，未提交到仓库。

### 验收矩阵

| 用例 | 目标 |
|---|---|
| GPU off vs M5.4 base | 0 diff；CPU list 路径未改变 |
| GPU on vs GPU off（forward） | 0 diff（固定顺序）；若仅浮点差异，需逐像素定位并记录 |
| GPU on vs GPU off（deferred） | 0 diff |
| GPU on vs GPU off（transparent/forward-only） | 0 diff |
| GPU on vs GPU off（fog punctual） | 0 diff |
| GPU on vs GPU off（contact shadows） | 0 diff |
| D3D11 vs Vulkan GPU on | 0 diff 或 ≥60 dB（记录实际值） |
| 8/16/32 punctual lights | list 不溢出；GPU/CPU 结果一致 |
| 相机后方/屏幕外/spot 锥体 | 不误剔除；不出现全屏 light |
| `--no-light-cluster` | 正确回退 FPTL |
| 0.5 render scale | FPTL/cluster 尺寸随 render size 正确重建 |
| 1080p 300 帧 full post | >60 fps；GPU 路径不慢于 CPU 路径（至少不明显退化） |
| `hbg_tests`/`ctest`/`hbg_idl_check`/`--gfx-test`/docs/diff | 全部通过，无泄漏 |

### 性能记录

- 记录 CPU `LightListBuilder::build()` wall、GPU light-list pass 数、GPU frame timer、full post fps。
- 对比 CPU/GPU 在 8/16/32 lights、forward/deferred、cluster on/off、fog/transparent 下的差异。
- 若 GPU 路径没有收益或出现明显回退，M6 保持默认关闭，不把“GPU”当成默认性能结论；记录原因并转 M7（buffer/indirect/async）。

### 默认开关

- 只有同时满足：视觉 0 diff、D3D/VK 一致、CPU light-list 工作被移除且 full post 不退化，才把 `supportGpuLightList`/`--gpu-light-list` 设为默认开启。
- 否则保持默认 CPU，保留 GPU 可选路径和 `--no-gpu-light-list` 回退，M6 出口记录为“experimental”。
- **M6.4 实际：条件满足，默认已切 true**；`--no-gpu-light-list` 仍是 CPU 对照/回退，出错自动 fallback。

### 文档回填

- 更新 `docs/m6-checklist.md` 每个子任务状态、实际 HDRP `file:line`、测试/截图/性能结果。
- 更新 `docs/implementation-plan.md` 的 M6 状态、当前验证、下一任务。
- 更新 `AGENTS.md` 的 GPU light-list 约束：texture-backed、无 buffer/indirect、32 lights、CPU fallback、采样 stage 约定。
- 清理/标注临时场景和截图路径，不把验收产物提交到仓库。

### 出口

- M6 的 GPU light-list 在 D3D11/Vulkan 上可验证、可回退、无泄漏。
- GPU 开启时不调用 CPU `LightListBuilder::build()`；关闭时保持 M5.4 行为。
- 所有消费路径（deferred/forward/transparent/fog/contact/debug）使用同一套 GPU light-list 生成路径（按原语义选 FPTL 或 cluster），不再混用 CPU/GPU 生产者。
- 不把 M6 未做的 GPU shadow culling/indirect draw 混入验收。

---

## 风险与处理

1. **GPU FPTL/cluster 与 CPU 结果不完全一致**：先修正 screen AABB、depth slice、tile 边界和升序输出；不允许用 PSNR 阈值掩盖整块 tile 错误。逐 tile 对照可通过 debug view + CPU reference 定位。
2. **compute 输出 image 与 sampler 同 stage 冲突**：沿用 M3.1 的约定，输出 image 从 stage 1/2/3 开始，不与输入 sampler 共用 stage。
3. **GPU list 写入与消费者 graph 依赖缺失**：所有消费者必须显式 `readTexture` GPU 写出的纹理；RenderGraph 的 lifetime/读写声明不能省。
4. **GPU 资源失败**：统一回退 CPU；不要在 frame 中混用半套 GPU list。
5. **性能无收益**：M6 以正确性和可回退为先；无收益时保持默认关闭，buffer/indirect/async 转 M7，不伪造性能结论。
6. **M6 范围膨胀**：GPU shadow culling、indirect draw、64/128 灯、async compute、feature variants 均不属于 M6；出现需求时新建 M7 清单。
