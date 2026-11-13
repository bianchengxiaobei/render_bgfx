# M7 Buffer / Indirect / Async GPU Light List 与 GPU Shadow Culling / Indirect Draw 规划

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M7.0–M7.8 已完成并验收**（2026-11-13）。本文定义 M7 的范围、数据契约、实现顺序、HDRP/bgfx 依据和验收门禁。
> 原则：先读 HDRP 与 vendored bgfx 源码再实现；BGFX 能力不足时不伪造“async/indirect”结果；M6.4 的 CPU/texture GPU 路径必须保持可回退。
> 前置基线：M6.4 已完成，`supportGpuLightList=true` 默认开启；M7.8 默认启用 buffer path，texture/CPU 仍可回退。

### 实现记录（2026-11-13）

- **M7.0 完成**：`BufferType::Storage/StorageVertex`、`Buffer::update/stride/flags`、`Encoder::setStorageBuffer/setInstanceDataBuffer/submitIndirect/dispatchIndirect`、RenderGraph buffer import/read/write 均已接入；storage buffer 自动 `BGFX_BUFFER_INDEX32`，D3D11/Vulkan 均可读写。
- **M7.1 完成**：AABB、big-tile、FPTL、cluster storage buffer producer 与 buffer path 均已完成。`m61_transparent` 的 cluster debug `slice=-2/0/5/15/31` 在 D3D11/Vulkan 上 CPU/texture vs buffer **逐像素 0 diff**；forward/deferred/contact/no-cluster/0.5 scale 的 FPTL 对照 **0 diff**。
- **M7.2 完成**：buffer path 的 forward/deferred/contact/transparent/fog/debug 均直接读 `Buffer<uint>`；`light_list_cpu_ms=0`，不再构建 CPU cluster texture。D3D11/Vulkan 上 `m61_full` deferred/forward、`m61_transparent`、`--fog`、contact、spot、32-light stress、0.5 scale、no-cluster **全部 0 diff**；`light_list_passes=4`（cluster）或 `3`（no-cluster），含 indirect 为 `5/4`。
- **M7.3 完成**：`light_indirect_args.cs.sc` 生成 `dispatchIndirect` 参数；FPTL 使用 indirect dispatch，buffer vs buffer direct **0 diff**。
- **M7.4/M7.5 完成**：GPU shadow caster culling + indirect shadow draw 已接入；D3D11/Vulkan `m61_full` CPU shadow vs GPU shadow **0 diff**，`shadow_cull_path=gpu`、`shadow_indirect=true`，资源释放后无 leak。HDRP 本身没有这套 GPU caster culling/indirect shadow draw，属于项目扩展。
- **M7.6 完成可行性验证**：bgfx 无公开 async compute queue；`--async-gpu-light-list` 仅记录 requested/warning，active=false。
- **M7.7 完成**：CLI/诊断/回退/资源释放已接入。1080p、300 帧、full post（`m61_thirtytwo`, TAA/Bloom/ColorGrading）D3D11：CPU **109.5** / texture **121.4** / buffer **121.8** / buffer+indirect+shadow **119.8** fps；Vulkan：CPU **107.7** / texture **114.8** / buffer **117.7** / buffer+indirect+shadow **115.1** fps。均 >60，buffer/indirect 无明显退化；`light_list_cpu_ms=0`、`light_list_passes=4`（buffer）、`5`（indirect）。
- **cluster buffer 结论**：此前“所有 slice 包含全部光源”是把 debug 视图的平均 RGB（165.67）误当成 light count；实际 debug 图像与 CPU 在 `-2/0/5/15/31` slice 均 **0 diff**。AABB depth 布局为 `min=(minX,minY,minDepth,maxDepth)`、`max=(maxX,maxY,visible,0)`；cluster shader 使用 `minBounds.z/w` 正确。
- **M7.8 完成**：alpha/cache/cascade 组合已在 `m5_shadow_stress`（directional 1/2/4 cascade + point + spot + MASK alpha cube）复核；D3D11/Vulkan、forward/deferred、cache on/off 下 CPU shadow vs GPU shadow **全部 0 diff**，cache 日志 `active=true cached_requests=6`。默认决策：buffer path 默认开启，indirect/shadow GPU culling/indirect/async 保持 opt-in experimental。
- **验证**：`ctest --preset vs2022-debug` **203 cases / 19,713 assertions + hbg_idl_check 通过**；D3D11/Vulkan gfx test 通过；上述 buffer vs CPU 矩阵 **全部 0 diff**。
- **修复记录**：PowerShell 写入的 UTF-8 BOM 使 shaderc 把 include 文件首行 `#ifndef` 当普通文本，导致伪 `#endif without #if`；已改为无 BOM + LF。debug light-cluster pass 删除重复 `m_clusterBinder.upload()`，消除 bgfx “Uniform already set for this draw call”。

参考：

- HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
- HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
- bgfx：`build\vs2022\_deps\bgfx-src`
- 当前 GPU light-list：`src\render\passes\forward_renderer.cpp`、`shaders\passes\light_*.cs.sc`、`shaders\common\light_data.sc`
- 当前 shadow：`src\render\shadows\shadow_manager.*`、`src\render\passes\forward_renderer.cpp` 的 `m_shadowDrawList`/`renderShadowPass()`
- 当前 buffer 封装：`src\gfx\buffer.*`、`src\gfx\encoder.*`、`src\render\graph\render_graph.*`

---

## 0. 结论摘要

M7 的目标不是增加新的光照效果，而是把 M6 的 **texture-backed、同步、CPU 提交阴影剔除** 路径升级为：

1. **buffer-backed GPU light list**：AABB、big-tile、FPTL、cluster 和 indirect 参数改为 storage buffer；M7 暂不引入 HDRP 的 layered offset；
2. **GPU generated indirect arguments**：为 light-list 消费端/调试路径提供 `dispatchIndirect`，为阴影提供 `drawIndexedIndirect`；
3. **GPU shadow caster culling**：CPU 只生成 shadow request/atlas，GPU 对 caster 做逐 request 剔除并压缩实例；
4. **GPU indirect shadow draw**：用 GPU 生成的实例缓冲和 indirect args 提交 shadow pass；
5. **async compute 可行性验证**：按 HDRP `BuildLightListRunsAsync()` 的接口设计，但必须先在当前 bgfx 上验证是否有真正的 async queue。

M7 的硬性边界：

- **不改变 M6 的光照/阴影视觉语义**：FPTL 16×16、cluster 64×64×32、big-tile 64×64、升序 light index、最多 32 个 punctual lights、fixed stride 33 全部保持；目标是 buffer/indirect 路径与 M6 CPU/texture 路径 **0 diff**。
- **不把“buffer”理解为自动扩容**：M7 仍固定 `kMaxGpuLights=32`。M6 文档中“64/128 留 M7”的计划在本文件修订为 **M8**，避免同时改变 `GpuLightData`、shader 数组、descriptor 和 shadow 数据契约。
- **shadow request/atlas 仍由 CPU 生成**：light selection、cascade 拟合、punctual atlas packing、cached shadow 判定沿用 M5/M6；M7 只把 **caster culling 和 shadow draw** 下放 GPU。HDRP 本身也把 request 更新放在 CPU job（`HDGpuLightsBuilder.LightLoop.cs:478-581`），不把 atlas packing 放进 light-list compute。
- **不伪造 async**：当前 bgfx 没有与 HDRP `ComputeGraphContext`/async compute queue 等价的公开接口。M7.6 只能是能力验证；没有可测的 GPU overlap 就保持同步并记录为阻塞，不把“多线程录制”称为 async compute。
- **M7 默认决策（M7.8）**：M6.4 的 `supportGpuLightList=true` 保持；`supportGpuLightListBuffers` 默认 **true**（buffer 不可用时回退 texture/CPU）；`supportGpuLightListIndirect`、`supportGpuShadowCulling`、`supportGpuShadowIndirect`、`supportAsyncGpuLightList` 保持 **false/experimental**。
- **不做**：GPU light selection、light category/layer/feature variants、layered cluster offset 扩容、64/128 lights、GPU shadow atlas packing、cached shadow GPU 更新、D3D12、bindless/descriptor indexing、DXR/RT、Hi-Z occlusion、meshlet/mesh shader、GPU→CPU readback。

---

## 1. 当前基线与缺口

### 1.1 M6.4 已完成的部分

- GPU light-list 由 4 个同步 compute pass 组成：
  - `GPU Light Screen AABB`
  - `GPU Big Tile Light List`
  - `GPU FPTL Light List`
  - `GPU Light Cluster`
  - 入口在 `src/render/passes/forward_renderer.cpp:1631-1755`。
- 输出仍是 `R32_UINT` texture：每个 tile/cluster row 为 `count + 32 indices`，stride 33；消费者用 `texelFetch`：
  - `shaders/common/light_data.sc:14`、`:55-105`；
  - forward/deferred/contact/fog/debug 均按 `s_lightList` stage 7 采样。
- GPU 开启时 `updateLightList()` 跳过 CPU `LightListBuilder::build()`，失败回退 CPU；`--no-gpu-light-list` 保留 M5.4 CPU 基线。
- M6.4 验收：同后端 CPU/GPU 全矩阵 0 diff；D3D/VK GPU on 84.91–93.52 dB；full post 1080p >60 fps。详见 `docs/m6-checklist.md`。

### 1.2 当前 shadow 路径

- `ShadowManager::update()` 在 CPU 生成 `ShadowRequest`（方向光 cascade、point face、spot），包括 view/proj/atlas viewport/frustum planes；见 `src/render/shadows/shadow_manager.cpp`。
- 每帧 `ForwardRenderer::buildDrawLists()` 构建一份**不按 camera frustum 裁剪**的 `m_shadowDrawList`（`forward_renderer.cpp:1589-1597`）。
- `renderShadowPass()` 对每个 request 在 CPU 调用：
  - `shadowList.buildCulled(m_shadowDrawList, request.frustumPlanes)`（`forward_renderer.cpp:2985`）；
  - 对每个 `DrawBatch` 在 CPU 分配 `InstanceData`、填 world 矩阵、`drawShadowBatch()`（`forward_renderer.cpp:2833-2874`）；
  - 每个 request 一个 raster pass，`drawShadowBatch()` 使用 `ShadowInstanced` 变体。
- 也就是说：**CPU 每 request 复制/裁剪 caster，CPU 生成实例数据，直接 submit**。M7.4/M7.5 要把这两步改为 GPU。

### 1.3 当前 gfx/RenderGraph 能力缺口

- `src/gfx/buffer.h:12-17` 只有 `Vertex/Index/DynamicVertex/DynamicIndex/Indirect`，没有 storage/compute buffer 抽象；`BufferDesc` 只有 size/count/flags，没有 element stride/usage。
- `src/gfx/encoder.h:50` 只有固定 dispatch；没有 `setStorageBuffer()`、`dispatchIndirect()`、`submitIndirect()`、从 GPU buffer 绑定 instance data。
- `src/gfx/buffer.cpp` 没有通用 buffer update；compute-write buffer 不能 CPU 更新。
- `RenderGraph` 已有 logical buffer handle、transient buffer pool、`readBuffer()/writeBuffer()`（`render_graph.h:117-118`、`:147-154`），但 buffer desc 需要支持 storage/indirect 语义。
- 现有 `BufferType::Indirect` 只是 `bgfx::createIndirectBuffer(desc.count)`，尚不能作为 compute 输出或 indirect draw 参数使用。

### 1.4 bgfx 已确认能力

- **storage buffer 基础**：`BGFX_BUFFER_COMPUTE_READ/WRITE/READ_WRITE`（`bgfx.h:2836-2839`）；compute 通过 `bgfx::setBuffer(stage, handle, Access, offset, size)` 绑定（`bgfx.h:1917-2005`、`:5131-5204`）。bgfx 没有独立的 `createStorageBuffer`，需要复用 vertex/index/dynamic buffer 并显式带 compute flags。
- **indirect dispatch**：`bgfx::dispatch(view, program, indirectBuffer, start, num)`（`bgfx.h:2053-2088`、`:5260-5288`）；`createIndirectBuffer()`（`bgfx.h:3241`）。
- **indirect draw**：`bgfx::submit(view, program, indirectBuffer, start, num, depth)`（`bgfx.h:1833-1905`、`:5040-5118`）；`bgfx_compute.sh:330-360` 的 `dispatchIndirect`/`drawIndexedIndirect` 宏给出 shader 侧记录写法。
- **GPU 写 instance buffer + indirect draw**：bgfx 示例 48 用 compute 写 `m_instanceBuffer` + indirect args，然后 `setInstanceDataBuffer(m_instanceBuffer, 0, num)` + `submit(..., m_indirectBuffer)`（`examples/48-drawindirect/drawindirect.cpp:443-494`）；示例 37 用 GPU culling + 间接绘制（`examples/37-gpudrivenrendering/gpudrivenrendering.cpp:858-936`）。
- **vertex stage storage buffer**：示例 41 在 vertex shader 使用 `BUFFER_RO`（`examples/41-tess/vs_terrain_render.sc:6-8`）；M7 可优先用 GPU 写 `mat4` instance buffer，避免立即新增 shadow vertex storage-buffer 变体。
- **D3D11 的 indirect count**：vendored D3D11 后端只 advertise `BGFX_CAPS_DRAW_INDIRECT`，没有 `BGFX_CAPS_DRAW_INDIRECT_COUNT`（`renderer_d3d11.cpp:1305-1360`）；Vulkan 才有 `KHR_draw_indirect_count`。M7 的**基础路径不得依赖 indirect count**：CPU 已知 batch 数，每个 batch 一次 `submit(indirect)`，实例数由 GPU 写入间接参数。
- **async compute**：bgfx 的 multithreaded 是 CPU 提交/encoder 并行，不是独立 async compute queue；Vulkan 后端选择 combined graphics+compute queue（`renderer_vk.cpp:2362-2381`）。因此 HDRP 的 `EnableAsyncCompute` 不能直接映射到当前 bgfx。

---

## 2. HDRP 源码结论

### 2.1 Light list

HDRP 的 `BuildGPULightList` 是一次 compute pass，内部顺序为：

```text
ClearLightLists
GenerateLightsScreenSpaceAABBs
BigTilePrepass
BuildPerTileLightList
VoxelLightListGeneration
BuildDispatchIndirectArguments
```

依据：

- pass data 与输出 buffer：`RenderPipeline/HDRenderPipeline.LightLoop.cs:67-146`；
- clear：`:154-196`；
- screen AABB：`:199-219`；
- big-tile：`:222-238`；
- FPTL：`:240-282`；
- cluster：`:284-314`；
- indirect args：`:316-390`；
- buffer 分配：`:621-657`；
- pass 顺序与 async 开关：`:665-697`；
- deferred lighting 使用 indirect dispatch：`:859-871`；
- light data/structured buffer 与 `TileAndClusterData`：`Lighting/LightLoop/LightLoop.cs:366-413`；
- 调度顺序备忘：`Lighting/LightLoop/LightLoop.cs:2041-2044`；
- async 门控：`RenderPipeline/Settings/FrameSettings.cs:601`。

关键结论：

- HDRP 的 list 输出是 **GraphicsBuffer/StructuredBuffer**，不是 texture；FPTL/cluster 消费者按 buffer 读取。
- `BuildDispatchIndirectArguments` 为 deferred feature variants 生成 dispatch args；它不是通用的“所有 pass 一律 indirect”。
- `BuildLightListRunsAsync` 依赖 engine 的 async compute 支持；当前项目不能假设有同等能力。
- HDRP 的 FPTL 上限/可见灯/粗 tile 上限与本项目不同；M7 不扩容。

### 2.2 Shadow

HDRP 的 shadow request 是 CPU job 生成的：

- `HDGpuLightsBuilder.UpdateShadowRequestsAndCalculateShadowIndices()`：`Lighting/Light/HDGpuLightsBuilder.LightLoop.cs:478-581`；
- shadow culling split / request 数据：`Lighting/Shadow/HDShadowCullingUtils.cs:64-132`；
- atlas 在 `RenderShadowMaps()` 中逐 request 调 `CreateShadowRendererList` + `DrawRendererList`：`Lighting/Shadow/HDShadowAtlas.cs:442-510`；
- `RenderShadows()` 遍历 atlases：`Lighting/Shadow/HDShadowManager.cs:1420-1450`；
- `PrepareGPUShadowDatas()` 只是 CPU 填 shadow 数据：`HDShadowManager.cs:1110` 起。

关键结论：

- **HDRP 这条路径没有 GPU caster culling，也没有 indirect shadow draw**；它使用 CPU job 生成 request，然后交给 engine 的 shadow renderer list 做 caster culling/draw。
- 因此 M7.4/M7.5 是项目自己的扩展，不能写成“移植 HDRP”。可复用的是 HDRP 的 request 数据形状、atlas 语义和逐 request 循环；GPU 剔除/压缩/indirect 需要新设计。
- M7 不应把 GPU shadow path 混入 `BuildGPULightList`：light-list compute 只处理光源，shadow caster 数据、request 矩阵和 atlas viewport 是另一组资源。

---

## 3. M7 任务拆分

| 子任务 | 目标 | 依赖 | 初始默认 |
|---|---|---|---|
| **M7.0 Buffer/indirect 基础设施** | storage buffer、compute bind、indirect dispatch/draw、GPU instance data、capability gate | M6.4 | 关闭 |
| **M7.1 GPU light-list buffer producer** | AABB/big-tile/FPTL/cluster 写 storage buffer；保留 texture/CPU 回退 | M7.0 | 关闭 |
| **M7.2 Buffer light-list consumers** | forward/deferred/contact/fog/debug 用 buffer 读取；buffer 路径移除 texture 依赖 | M7.1 | 关闭 |
| **M7.3 Light-list indirect dispatch** | 生成 dispatch args，验证 indirect dispatch；无 consumer 时只保留测试路径 | M7.2 | 关闭 |
| **M7.4 GPU shadow caster culling** | 每 request/batch GPU 剔除 caster，压缩 instance，输出 indirect args | M7.0、M7.3 | 关闭 |
| **M7.5 GPU indirect shadow draw** | GPU instance buffer + `drawIndexedIndirect` 提交 shadow pass | M7.4 | 关闭 |
| **M7.6 Async light-list 可行性** | 验证 bgfx 是否有真正的 async compute；无则记录阻塞 | M7.2 | 关闭 |
| **M7.7 集成/回退/debug/性能** | 三层回退、CLI/UI、诊断、resize/shutdown、性能矩阵 | M7.0–M7.6 | 关闭 |
| **M7.8 M7 验收** | 视觉/数值/性能/文档/默认决策 | M7.7 | - |

### M7 明确不做

- 不做 64/128 light 扩容；M7 固定 32 lights、fixed stride 33。
- 不把 cluster 改成 HDRP 的 layered offset/atomic append；M7 先保持 `count + 32 indices` 的固定布局，降低与 M6 结果的差异面。layered 布局若需要，单独 M8（M8 规划复核后转入 M9，见 `docs/m8-checklist.md`）。
- 不做 feature variants、light category/layer、decal/env light list、per-tile material flags。
- 不做 GPU shadow request 更新、cascade 拟合、atlas packing、cached shadow 内容生成；这些仍 CPU。
- 不做 bindless、descriptor indexing、D3D12、RT、VRS、mesh shader、Hi-Z occlusion、GPU-driven opaque/transparent。
- 不做每帧 GPU→CPU readback；GPU list/cull 结果通过 debug pass、shadow atlas 截图和双路径像素对照验证。
- 不把“CPU 多线程录制”或“bgfx render thread”包装成 async compute。

---

## 4. 数据契约

### 4.1 三层路径

```text
CPU 路径（M5.4/M6.4）:
  --no-gpu-light-list
  现有 LightListBuilder + texture upload

M6 texture GPU 路径:
  --gpu-light-list --gpu-light-list-path=texture
  compute -> R32_UINT texture -> texelFetch

M7 buffer GPU 路径:
  --gpu-light-list --gpu-light-list-path=buffer
  compute -> storage buffer -> BUFFER_RO
```

回退顺序：

```text
buffer path 创建/绑定/能力失败
  -> texture GPU path（如果可用）
  -> CPU path
```

不允许在 frame 中混用半套 buffer/texture list；每个 frame 只能有一个 list producer 和一个 consumer 约定。

### 4.2 Light-list buffer 布局

保持 M6 的语义，只改存储介质：

| buffer | 元素 | 数量 | 布局 |
|---|---|---|---|
| `lightScreenBounds` | `vec4` | `2 * kMaxGpuLights` | `2*i+0=(minX,minY,minDepth,maxDepth)`；`2*i+1=(maxX,maxY,visible,0)` |
| `lightBigTileList` | `uint` | `bigTileCount * 33` | 每 big tile：`count` + 32 个升序 light index |
| `lightFptlList` | `uint` | `fptlTileCount * 33` | 每 16×16 tile：`count` + 32 个升序 light index |
| `lightClusterList` | `uint` | `clusterTileCount * 32 * 33` | `(tileIndex * 32 + slice) * 33 + [count,index...]` |

约束：

- `count` 必须 clamp 到 32；未使用槽写 0。
- light index 仍是绝对 `GpuLightData` 索引，升序；方向光不进 list。
- 不改变 `LightListBuilder::projectLightScreenBounds()` 的 8-corner 语义。
- buffer 的 stride 是 `uint`；不要按 texture 的 `width=33*tileCountX` 计算 buffer 下标。
- `s_lightList` 的 stage 7 在 buffer 变体中是 `BUFFER_RO`，不是 sampler；同一个 shader 不能同时声明 texture 和 buffer 的同名符号。

### 4.3 Shadow buffer 布局

M7.4/M7.5 只新增**临时帧内 GPU 数据**，不改变 `ShadowRequest` 的 CPU 生成和 atlas 布局：

| buffer | 内容 | 生命周期 |
|---|---|---|
| `shadowRequests` | 每 request 的 `viewProj`、6 个 frustum planes、atlas viewport、light/request index、flags | 每帧上传/图内 transient |
| `shadowCasters` | 每 caster 的 AABB、world matrix、`indexOffset/indexCount`、`batchKey`/batch slot | 每帧上传/图内 transient |
| `shadowInstanceData` | 每 request/batch 的紧凑 `mat4` 实例数据（按 batch 区域固定容量） | GPU 写、shadow pass 读 |
| `shadowIndirectArgs` | 每 request/batch 的 `drawIndexedIndirect` 参数 | GPU 写、`submit(indirect)` 读 |
| `shadowCullStats` | 可选：每 request/batch 可见数/剔除数，仅 debug | GPU 写、debug pass 读 |

间接参数必须通过 bgfx 的 `drawIndexedIndirect` 宏生成（`bgfx_compute.sh:351-360`），不要手写与后端不一致的 5-uint 布局。每个 draw call 的记录数、stride 和 offset 以 bgfx 的 `createIndirectBuffer()`/`submit(indirect)` 约定为准。

### 4.4 Shadow instance 方案

M7 先采用**GPU 写 `mat4` instance buffer + 现有 `ShadowInstanced` shader**：

- `shadowInstanceData` 使用 `DynamicVertexBuffer`，layout 为 4×`vec4`（即 `kInstanceStride = 16 * sizeof(float)`），带 `BGFX_BUFFER_COMPUTE_WRITE`；
- compute 把每个通过裁剪的 caster 的 world matrix 写到该 request/batch 的固定区域；
- CPU 每 batch 绑定 `setInstanceDataBuffer(shadowInstanceData, regionStart, regionCapacity)`；
- `submit(id, program, shadowIndirectArgs, argOffset, 1)` 使用 GPU 写入的 instanceCount；
- 现有 `shadowcaster_instanced.vs.sc` 不改，继续从 `i_data0..3` 读取 world 矩阵。

内存上界：

- 每个 request 的 caster 容量 = `m_shadowDrawList.size()`；
- 每个 batch 的容量 = 该 batch 的 itemCount；
- 总计约 `requestCount * casterCount * 64B`；M7 需按实际场景限制/复用，不能用无限上界。
- 如果实测内存或实例缓冲绑定不可接受，备选方案：GPU 输出可见 caster index，shadow vertex shader 用 `gl_InstanceID` 从 `BUFFER_RO` 取 caster world（bgfx 示例 41 证明 vertex stage 可用 buffer）；该备选不在 M7 默认路径。

### 4.5 Pipeline/CLI/UI

计划新增（名称以实际实现为准）：

```text
PipelineSettings:
  supportGpuLightListBuffers = true
  supportGpuLightListIndirect = false
  supportGpuShadowCulling = false
  supportGpuShadowIndirect = false
  supportAsyncGpuLightList = false

CLI:
  --gpu-light-list-path=texture|buffer
  --gpu-light-list-indirect
  --gpu-shadow-culling
  --gpu-shadow-indirect
  --async-gpu-light-list
  --no-gpu-light-list-buffers
  --no-gpu-light-list-indirect
  --no-gpu-shadow-culling
  --no-gpu-shadow-indirect
```

约束：

- `--no-gpu-light-list` 仍是总开关，优先级最高；关闭后不得创建/绑定 M7 GPU 资源。
- buffer path 不可用时回退 texture/CPU，不能黑屏或输出空 list。
- shadow GPU path 不可用时逐 request 回退 CPU `buildCulled`/`drawShadowBatch`。
- async flag 在没有能力时必须强制同步并只警告一次。
- DebugUI 显示 `LightList path`、`Shadow cull path`、`Indirect dispatch/draw`、`Async requested/active`、pass/dispatch/draw 计数；没有 GPU readback 时不显示伪造的 GPU 剔除数。

### 4.6 帧顺序

```text
Shadow request build (CPU)
GPU Shadow Caster Culling (M7.4, 可选)
GPU Shadow Indirect Draw (M7.5, 可选)
GPU Light Screen AABB
GPU Big Tile
GPU FPTL
GPU Light Cluster
GPU Light-list indirect arguments (M7.3, 可选)
Prepass / GBuffer / Contact / Deferred / Fog / Transparent / Forward
```

- shadow culling 需要 request 矩阵，必须在 `ShadowManager::update()` 之后、shadow pass 之前。
- light-list buffer producer 仍在 GBuffer/light loop 之前；不因为改成 buffer 而改变 M6 的帧顺序。
- GPU pass 一律 `allowCulling(false)`；所有消费者显式 `readBuffer()`/`readTexture()`，不能依赖隐式状态。
- M7.2 的 buffer consumer 必须显式 `readBuffer(lightFptlList/lightClusterList)`；M7.4/M7.5 必须显式 `writeBuffer(shadowInstanceData/shadowIndirectArgs)` 和 shadow pass `readBuffer`。

---

## 5. 子任务详细设计

### M7.0 Buffer / indirect 基础设施

状态：`[x]` 已完成（storage buffer、GPU instance、dispatchIndirect、submitIndirect、RenderGraph buffer）

目标：让 gfx/RenderGraph 能安全表达 storage buffer、GPU instance buffer、indirect dispatch 和 indirect draw，并在能力不足时可检测、可回退。

实现：

- `src/gfx/buffer.h/.cpp`：
  - 扩展 `BufferType`：至少增加 `Storage`（uint/struct array）和 `StorageVertex`（GPU instance/data buffer）；或增加 `BufferUsage` 字段，内部映射到 `createDynamicVertexBuffer`/`createDynamicIndexBuffer`。
  - `BufferDesc` 增加 `elementSize`/`stride`、`flags`、`usage`、`computeWrite` 等明确字段；禁止调用方在业务层拼 `BGFX_BUFFER_*`。
  - 增加 `Buffer::update(const void*, uint32_t size, uint32_t offset)`；只有 CPU 可写 buffer 允许更新，`COMPUTE_WRITE` buffer 明确拒绝并打日志。
  - 增加 buffer 的 `native()` 访问或内部 `setStorageBuffer()` 所需信息；不要把裸 `bgfx::*Handle` 暴露到 `src/render`。
- `src/gfx/encoder.h/.cpp`：
  - `setStorageBuffer(uint8_t stage, BufferHandle, bgfx::Access::Enum, uint32_t offset=0, uint32_t size=UINT32_MAX)`；
  - `dispatchIndirect(bgfx::ViewId, ProgramHandle, BufferHandle indirect, uint32_t start=0, uint32_t num=1, uint8_t flags=...)`；
  - `submitIndirect(bgfx::ViewId, ProgramHandle, BufferHandle indirect, uint32_t start, uint32_t num, uint32_t depth=0, uint8_t flags=...)`；
  - `setInstanceDataBuffer(BufferHandle, uint32_t startVertex, uint32_t num)`（仅 dynamic vertex buffer）。
- `RenderGraph`：
  - buffer desc 保留 usage/flags；transient pool key 包含这些字段，避免复用错误；
  - `readBuffer`/`writeBuffer` 已有，补 storage/indirect 的验证；
  - buffer 生命周期/resize 与 texture 一致；buffer 不能挂 FBO。
- 能力检测：
  - 要求 `BGFX_CAPS_COMPUTE`、`BGFX_CAPS_DRAW_INDIRECT`（indirect draw 路径才要求）；
  - 不要求 `BGFX_CAPS_DRAW_INDIRECT_COUNT`；D3D11 基础路径只使用 CPU 固定 draw 数和 GPU 写入的 instanceCount；
  - `maxComputeBindings` 足够绑定所有 list/shadow buffer。
- 测试：
  - buffer 创建/stride/flags/update/destroy；
  - compute 写 storage buffer 的 gfx smoke；
  - `dispatchIndirect` 与固定 dispatch 结果一致；
  - `submitIndirect` 在 D3D11/Vulkan 的 caps 下可用；
  - RenderGraph transient buffer 复用不串数据；resize/shutdown 无泄漏。

出口：

- 两个后端都能创建/绑定/写 storage buffer 和 indirect buffer；无裸 bgfx handle 泄漏到业务层。
- 缺失 caps 时返回明确错误并禁用对应路径，不崩溃。
- `hbg_tests`/`ctest`/`--gfx-test` 通过；M6 画面不变。

### M7.1 GPU light-list buffer producer

状态：`[x]` 完成：AABB/big-tile/FPTL/cluster storage buffer producer 全部启用；cluster debug 各 slice 与 CPU **0 diff**

目标：四个 GPU light-list compute pass 写 storage buffer；先不切换消费者，保留 M6 texture 路径和 CPU 路径。

实现：

- 新增 `GpuLightListBuffers`（名称待定）：
  - `lightScreenBounds`、`lightBigTileList`、`lightFptlList`、`lightClusterList`；
  - 尺寸随 render size 变化；`ForwardRenderer` 负责 `ensure*/release*`；buffer 总大小和 stride 写入日志。
- shader：
  - `light_screen_aabb.cs.sc` 输出从 image 改为 storage buffer（`BUFFER_WO` 或 `BUFFER_RW`）；
  - `light_big_tile.cs.sc`、`light_fptl.cs.sc`、`light_cluster.cs.sc` 输出 buffer；
  - 输入 screen bounds/big-tile 改为 buffer 读取（仍可按 stage 绑定）；
  - 保持 thread group 布局、`gl_WorkGroupID` tile 映射、升序 mask、32 clamp 完全不变。
- `ForwardRenderer::addGpuLightListPasses()`：
  - buffer path 创建 graph buffer 并 `writeBuffer`；
  - 保留 M6 texture path 作为 `texture` 分支；
  - buffer path 不与 texture 同时写同一语义 list；若保留 texture，仅作为 debug/回退。
- Debug：
  - 增加只读 debug pass（或 debug shader）直接读 buffer 显示 count/index/overflow；
  - 不依赖 texture readback；不伪造 CPU 数值。
- 测试：
  - `GpuLightListBuffers` 尺寸公式单测（不同 render size / 0.5 scale / no cluster）；
  - CPU reference 布局写入/读取 helper 单测；
  - D3D11/Vulkan buffer producer 不报 validation；
  - M7.1/M7.2 完成后消费者已全部切到 buffer；cluster debug `-2/0/5/15/31` 与 forward/deferred/contact/transparent/fog 均与 CPU **0 diff**，`light_list_cpu_ms=0`。

出口：

- buffer 内容布局与 M6 texture 语义一致（通过 debug pass/CPU reference 验证）；
- buffer 分配失败自动回退 M6 texture/CPU；
- resize/shutdown 无泄漏；M6 默认路径不退化。

### M7.2 Buffer light-list consumers

状态：`[x]` 完成：forward/deferred/contact/transparent/fog/debug 全部读 buffer；`light_list_cpu_ms=0`

目标：让所有 light-list 消费者直接读 storage buffer，完成从 texture 到 buffer 的迁移。

实现：

- `shaders/common/light_data.sc`：
  - 增加编译分支 `HBG_LIGHT_LIST_BUFFER`；
  - buffer 分支：`BUFFER_RO(s_lightList, uint, 7)`；texture 分支保留 `USAMPLER2D`；
  - `hbgLightListCount/Index`、`hbgClusterLightListCount/Index` 分别实现 buffer/texture 下标；
  - buffer 下标按 §4.2 的 flattened 布局，不按 texture 的 `x=33*tileX`。
- Shader variants：
  - `shader_key.h`/`ShaderLibrary`/`manifest.json` 增加 light-list buffer 变体；`build_shader` 产生 texture/buffer 两套；
  - 变体覆盖 forward、deferred lighting、contact shadows、volumetric fog、debug light cluster、任何直接 include `light_data.sc` 的 pass；
  - `ProgramKey` 增加路径位/字段，`ForwardRenderer` 按 pipeline 选变体，不能运行时混用。
- Binding：
  - buffer path 在 stage 7 绑 `lightFptlList`/`lightClusterList`，不再绑 `m_lightListTexture`；
  - RenderGraph pass 对 buffer 显式 `readBuffer`；graph 编译后 buffer 必须 live；
  - texture path 继续使用现有 `m_lightListSampler`。
- 移除 M6 CPU upload：
  - buffer path 开启时不再每帧 `updateTexture(m_lightListTexture, ...)`；
  - M6 texture path 仍保留 `--gpu-light-list-path=texture` 对照；
  - CPU path 仍保留 `--no-gpu-light-list`。
- 测试：
  - M6.4 全矩阵（forward/deferred/transparent/fog/contact/8/16/32/no-cluster/0.5/spot）三路径 CPU vs texture vs buffer 同后端 0 diff；
  - D3D11/Vulkan buffer path 0 diff 或 ≥60 dB（按 float 差异记录）；
  - debug `-2/-1/0..31` 读 buffer 正确；
  - shader variant 缺失时只禁用 buffer path，回退 texture，不破坏 M6。

出口：

- buffer path 不再需要 `R32_UINT` list texture；所有消费者读同一份 buffer。
- 画面与 M6.4 texture/CPU 路径 0 diff；无每帧 texture upload。
- 任何 consumer 失败能回退 texture/CPU；无空 list/黑屏。

### M7.3 Light-list indirect dispatch

状态：`[x]` 已完成；buffer + indirect 与 direct **0 diff**

目标：接入 `dispatchIndirect`，验证 GPU 生成的 dispatch args；在项目没有 feature variants 的前提下，不把 indirect 当成自动性能收益。

实现：

- 新增 `lightTileList` / `dispatchIndirectBuffer`（名称待定）；
- 按 HDRP `BuildDispatchIndirectArguments`（`HDRenderPipeline.LightLoop.cs:316-390`）的结构：
  - clear indirect args；
  - 生成 active tile/tile list；
  - 用 `dispatchIndirect` 驱动一个**可选的** tile-list consumer 或 debug pass；
- 由于当前项目没有 HDRP 的 material/light feature variants，先不做 deferred feature-variant dispatch；不为“indirect”而改变 deferred lighting 的像素语义；
- 间接 dispatch 结果必须与固定 dispatch 的同一 pass 输出一致；
- 若没有实际 consumer，保留 `--gpu-light-list-indirect` 测试路径，默认关闭，不声称性能收益；
- 为 M7.4 shadow culling 的 indirect dispatch 提供复用接口。

HDRP 依据：

- indirect args 构建：`HDRenderPipeline.LightLoop.cs:316-390`；
- deferred indirect 使用：`:859-871`；
- async 选择：`:677`；
- 项目差异：无 feature variants、无 engine async queue。

出口：

- D3D11/Vulkan `dispatchIndirect` 可用，args 布局/清零正确；
- indirect 与 direct 同 pass 输出 0 diff；
- 缺失 `BGFX_CAPS_DRAW_INDIRECT` 或 args 生成失败时回退固定 dispatch；
- 默认关闭，除非 M7.8 有明确消费端和收益。

### M7.4 GPU shadow caster culling

状态：`[x]` 已完成；D3D11/Vulkan CPU shadow vs GPU shadow **0 diff**

目标：CPU 仍生成 shadow request/atlas，GPU 对每个 dynamic request 的 caster 做 frustum culling，压缩可见实例并写 indirect 参数。

实现：

- CPU 每帧：
  - 保留 `m_shadowDrawList` 的构建（不做 camera frustum cull）；
  - 保留 `ShadowManager::update()` 生成 request（cascade/point/spot/atlas viewport）；
  - 新增 `ShadowCasterGpu` 数据：world matrix、AABB、indexOffset/indexCount、batch slot；
  - 新增 `ShadowRequestGpu` 数据：viewProj、6 planes、atlas viewport、request/light index、flags；
  - 按 `DrawBatch` 分区，记录每 batch 的 item 范围/容量；CPU 仍知道 draw 数，但不再为每个 request `buildCulled`。
- GPU compute：
  - 输入 `shadowRequests`、`shadowCasters`；
  - 每 request/batch 一个 workgroup（或 request 一个 workgroup，batch 分区；先实现前者，简单且保持 batch 顺序）；
  - 逐 caster 用 6 个 frustum planes 做 AABB 测试；
  - 通过者在 batch 区域内 `atomicAdd` 得到槽位，写 `mat4 world` 到 `shadowInstanceData`；
  - 写 `drawIndexedIndirect` 参数：`numIndices`、GPU 可见 instanceCount、`startIndex`、`startVertex`、`startInstance`；
  - 失败/无可见者 instanceCount=0。
- CPU 绘制循环：
  - 每 request 设置 viewport/frame buffer；
  - 每 batch 绑 mesh vertex/index、material program，绑 `shadowInstanceData` 区域，`submit(indirect, argOffset)`；
  - alpha mask/material variant、cached request、point/spot/directional 分支保持原有语义；
  - cached shadow 请求不走 GPU culling（继续使用 M5.3 CPU/缓存路径），或使用已缓存的静态 list；M7 先明确“cached 不重绘/不重剔除”。
- 回退：
  - 任一 buffer/dispatch/indirect 条件失败，逐 request 回退现有 `buildCulled` + `drawShadowBatch`；
  - GPU/CPU 路径不能在同一 request 内混用半套 list。
- 诊断：
  - CPU 侧记录 `shadow_cull_path`、GPU dispatch 数、indirect draw 数、fallback；
  - GPU 剔除数不依赖 readback；可加 debug buffer/view 或只在 debug 模式下显示；
  - 保留 `shadow_drawn/shadow_culled` 的 CPU 语义，不伪造 GPU 计数。
- 测试：
  - 方向光 4 cascade、spot、point 6 face、多个 request；
  - 单 caster、空 batch、单 item batch、透明/alphaMask；
  - cached 静态场景、dynamic 每帧更新；
  - CPU vs GPU shadow atlas 逐像素 0 diff；最终画面 0 diff；
  - D3D11/Vulkan；resize/0.5 scale；无泄漏。

HDRP 依据/边界：

- request 更新：`HDGpuLightsBuilder.LightLoop.cs:478-581`；
- culling split/CPU jobs：`HDShadowCullingUtils.cs:64-132`；
- atlas 逐 request 绘制：`HDShadowAtlas.cs:442-510`；
- HDRP 没有本项目要新增的 GPU caster culling/indirect draw；M7 只复用 request/atlas 结构。

出口：

- GPU culling 与 CPU `DrawList::buildCulled` 的可见集合/顺序一致；
- shadow atlas/最终画面 0 diff；
- 多 request、point/spot/directional、alpha mask、cached 场景均验证；
- 失败回退 CPU，无空 shadow/黑屏。

### M7.5 GPU indirect shadow draw

状态：`[x]` 已完成；GPU instance + `submitIndirect` 在 D3D11/Vulkan 通过

目标：用 M7.4 的 GPU instance data + indirect args 提交 shadow pass，移除 CPU 实例填充和逐 item 提交。

实现：

- `Encoder` 增加 `submitIndirect()`（M7.0 已具备）和 GPU instance buffer 绑定；
- shadow pass：
  - 每 request/batch 绑定 `shadowInstanceData` 的固定区域；
  - `setIndexBuffer(mesh.indexBuffer, first.indexOffset, first.indexCount)` 仍用 CPU 已知 batch 的首 item；
  - `submit(view, shadowProgram, shadowIndirectArgs, argOffset, 1)`；
  - 单 item batch 也走 instanced 变体（instanceCount=1），或保留 CPU direct fallback；
- **不依赖 `BGFX_CAPS_DRAW_INDIRECT_COUNT`**：draw 数 CPU 固定为 batch 数；GPU 只决定 instanceCount；
- `setInstanceDataBuffer(buffer, startVertex, maxInstances)` 的 `maxInstances` 用 batch 容量；实际绘制数量由 indirect args 的 instanceCount 控制；必须在 D3D11/Vulkan 上实测该组合；不通过则整个 shadow GPU 路径回退 M6 CPU shadow，而不是假设 M7.4 可以脱离 indirect draw 工作；
- `shadowIndirectArgs` 每个 draw 记录必须按 bgfx `drawIndexedIndirect` 宏生成；CPU 不重排/不修改 GPU 参数；
- 保持 shadow atlas viewport、cascade/point face、material/alpha variant 不变；
- 失败/不支持时回退 M6 CPU direct shadow path；不把 indirect 作为默认直到 M7.8。

出口：

- D3D11/Vulkan shadow atlas 与 CPU 路径 0 diff；
- GPU instance data/indirect args 正确；draw 数、instanceCount、viewport 对应；
- 单 item/多 item/空 batch/alpha/cached 边界通过；
- 缺失 `DRAW_INDIRECT` 时回退；无 validation/leak。

### M7.6 Async light-list 可行性

状态：`[x]` 可行性验证完成；当前结论：**bgfx 公开 API 无独立 async compute queue**

目标：验证能否在现有 bgfx 上实现类似 HDRP `BuildLightListRunsAsync()` 的 GPU 异步；不能实现时明确记录，不伪造。

实现：

- 参考 HDRP `FrameSettings.cs:601` 的 async gate 和 `HDRenderPipeline.LightLoop.cs:677` 的 `EnableAsyncCompute`；
- 方案 A（优先评估）：独立 bgfx `Encoder`/view + 工作线程录制 light-list，主线程继续录制；这只重叠 CPU 录制，不是 GPU async；
- 方案 B（只有确认 API 存在才做）：独立 compute queue/跨队列 fence；当前 `Encoder`/RenderGraph 没有该能力，不能通过业务层拼出来；
- 增加 `--async-gpu-light-list` 和 debug diagnostics（requested/active/overlap）；
- 若没有可测的 GPU overlap：
  - 强制同步执行；
  - 记录“bgfx 限制：无 async compute queue；Vulkan 后端使用 combined graphics+compute（`renderer_vk.cpp:2362-2381`）”；
  - 不把 async 作为 M7 验收门禁，不默认开启；
- 如未来切换到支持 async 的后端，保留接口/数据布局，不重写 list。

出口：

- 有明确的 capability/profiling 结论；
- 若支持，视觉不变、GPU timer 有可重复的 overlap 收益；
- 若不支持，文档记录阻塞，默认同步，不伪报性能。

### M7.7 集成、回退、debug、性能

状态：`[x]` 完成：CLI/诊断/回退/资源释放/功能矩阵/1080p 300 帧性能已接入

实现：

- 三层路径选择与逐层 fallback（§4.1）；
- CLI/UI 状态、首帧/一次性 warning；
- 资源 resize/release：
  - light-list buffers 随 render size；
  - shadow buffers 随 request/caster 数量；
  - buffer pool 不持有跨帧失效引用；
- diagnostics 扩展：
  - `gpu_light_list_path=texture|buffer`
  - `gpu_light_list_indirect`
  - `shadow_cull_path=cpu|gpu`
  - `shadow_indirect`
  - `shadow_gpu_cull_dispatches`
  - `shadow_indirect_draws`
  - `shadow_cull_cpu_ms`
  - `async_light_list_requested/active`
  - 保留 `light_list_cpu_ms`、`gpu_light_list_fallback`；
- 性能记录：
  - CPU cull wall、GPU cull dispatch/draw 数、GPU frame timer、full post fps；
  - shadow-heavy 场景单独测；
  - 不允许用 GPU timer 代替 CPU cull 的收益结论；
- 文档：M7 清单、implementation-plan、AGENTS、CLI 命令、临时截图路径。

出口：

- 所有路径可切换、可回退、可诊断；
- resize/shutdown 无泄漏；
- M6.4 默认路径不受影响。

### M7.8 M7 验收

状态：`[x]` 已完成：功能、性能、alpha/cache/cascade 组合复核和默认路径决策均完成。

复核记录（2026-11-13）：
- 场景：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m5_stress\m5_shadow_stress.gltf`（directional + point + spot、MASK alpha cube、sidecar `onEnable` cache）；`--shadow-profile=config/shadows.json`，`--shadow-cascades=1/2/4`。
- D3D11：forward alpha、forward cache、2/1 cascade、deferred alpha 的 CPU vs GPU shadow 均 **0 diff**；cache 路径 `shadow_cache=true`、`shadow_cached=6`，GPU 只提交 dynamic request（`shadow_indirect_draws=12`）。
- 默认路径：D3D11/Vulkan 默认 buffer vs 显式 texture **0 diff**；640×360 默认 buffer vs `--no-gpu-light-list` CPU **0 diff**，CPU 回退 `light_list_cpu_ms=0.169`、无黑屏。
- Vulkan：forward alpha/cache、deferred alpha/cache、4 cascade 的 CPU vs GPU shadow 均 **0 diff**；alpha 不改变 culling 输入，shadow pass 仍使用 `ALPHA_CLIP` material variant。
- HDRP 依据：`HDShadowCullingUtils.cs:765-838` 用 `ExtractDirectionalLightData` 生成 cascade split，并以 `splitExclusionMask` 排除 cached split；`HDShadowManager.cs:1414-1452` 的 `RenderShadows()` 分离 cached/dynamic atlas。本项目 cached request 在 `renderShadowPass()` 入口跳过 GPU culling/draw，等价保留 M5.3 “cached 不重绘/不重剔除”语义。
- 像素对比文件：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m78\*`；alpha/cache/cascade 全部 `max=0 mean=0 diffpix=0`。

验收矩阵：

| 用例 | 目标 |
|---|---|
| CPU vs M6 texture vs M7 buffer | forward/deferred/transparent/fog/contact/8/16/32/no-cluster/0.5/spot 全部同后端 0 diff |
| D3D11 vs Vulkan buffer | 0 diff 或 ≥60 dB，记录实际值；不得有整 tile/整 list 错误 |
| buffer consumers | 所有 shader 变体存在；运行时不绑 texture list；debug slice 正确 |
| indirect dispatch | direct vs indirect 0 diff；args 清零/计数正确；失败回退 |
| GPU shadow culling | CPU vs GPU shadow atlas/最终画面 0 diff；directional/point/spot/cascade/alpha/cached |
| GPU indirect shadow draw | CPU vs GPU atlas/最终画面 0 diff；单/多 item、空 batch、D3D11/Vulkan |
| caps fallback | 缺 compute/indirect/程序时逐层回退，只警告一次，不黑屏/空 list |
| resize/0.5 scale | list/shadow buffer 重建，尺寸正确，无泄漏 |
| async | 有 capability/overlap 证据才启用；否则默认同步并记录阻塞 |
| 1080p 300 帧 full post | >60 fps；buffer/indirect 路径不显著退化；shadow-heavy 单独记录 |
| `hbg_tests`/`ctest`/`hbg_idl_check`/`--gfx-test`/docs/diff | 全部通过 |

默认决策：

- **buffer path：默认开启**。`supportGpuLightListBuffers=true`；D3D11/Vulkan 全部 0 diff，Vulkan 1080p 300 帧 buffer **117.7** vs texture **114.8** fps；buffer 不可用时仍逐层回退 M6 texture -> CPU。HDRP `BuildGPULightList` 本身始终输出 structured buffer，因此该默认方向与 HDRP 一致。
- **light-list indirect：保持 false**。`light_indirect_args` 已验证 direct vs indirect 0 diff，但多一个 dispatch/pass 且无可见收益，保留 `--gpu-light-list-indirect` opt-in。
- **GPU shadow culling/indirect：保持 false/experimental**。alpha/cache/cascade 全部 0 diff，但 1080p 性能没有超过 CPU shadow（D3D11 119.8 vs 121.8，Vulkan 115.1 vs 117.7），且 GPU 路径暂不写 CPU culling 统计；保留 `--gpu-shadow-culling --gpu-shadow-indirect`。
- **async：保持 false**。bgfx 无公开 async compute queue，不伪造 overlap。
- M6 texture/CPU 路径继续保留，作为能力不足和失败时的回退。

### M7 验收出口

- buffer light-list 与 M6 的 tile/cluster 集合、顺序、边界语义一致；
- GPU shadow culling/indirect draw 与 CPU shadow 路径一致，cached/alpha/point/spot/cascade 无回退；
- 所有新路径可回退，不产生空 list/黑屏/资源泄漏；
- async 有诚实结论（支持则验证收益，不支持则记录阻塞）；
- 文档、测试、性能记录齐全；M6.4 默认路径保持可用。

---

## 6. 风险与处理

1. **bgfx 没有独立 storage buffer 创建 API**：用 dynamic vertex/index buffer + compute flags 封装；先写 `M7.0` 小测试，不直接在 light-list 里试错。
2. **storage buffer stride/对齐**：uint list 用 4B stride；instance data 用 16B/64B 对齐；`setBuffer` offset 必须是 256B 对齐（`bgfx.h` 文档）。所有 offset/size 在 `gfx` 层校验。
3. **indirect draw 的 instanceCount 语义**：D3D11 不支持 indirect count；必须验证 `submit(indirect)` 使用 GPU 写入的 instanceCount，而 `setInstanceDataBuffer` 只负责 capacity。若后端行为不一致，整个 shadow GPU 路径回退 M6 CPU shadow，不伪造 M7.4“直接 draw”。
4. **buffer→texture mirror 不是可靠回退**：M7 不依赖 texture mirror 验证 buffer；用 debug pass 直接读 buffer + CPU reference + M7.2 消费者 0 diff。
5. **shader variant 爆炸**：`light_data.sc` 被多个 pass include；M7.2 必须一次列出所有变体并让 manifest 构建失败即报错，不能运行时静默 fallback 到空 list。
6. **shadow 内存/间接参数**：按 request×caster 分配可能较大；先限制 `kMaxGpuShadowCasters`/request 数并记录上界；超过则回退 CPU 或分批，不静默截断。
7. **cached shadow 语义**：缓存 atlas 不重绘；GPU culling 只能作用于 dynamic request，避免破坏 M5.3 的 cache 行为和 `shadow_cache` 诊断。
8. **async 误判**：CPU 多线程录制不是 GPU async；没有 queue/fence 证据不标记完成。
9. **无 GPU readback**：GPU list/cull 结果只能通过 debug pass、shadow atlas、截图、PSNR 验证；不要声称“CPU 回读校验”。
10. **scope 膨胀**：M7 不扩容、不做 feature variants/layered cluster、不做 atlas packing；出现这些需求新建 M8 清单。

---

## 7. 待确认/实现前检查

- [ ] `bgfx::createIndirectBuffer()` 生成的 buffer 是否能同时作为 compute 输出与 `submit(indirect)` 输入；用最小 gfx test 验证。
- [ ] D3D11/Vulkan 上 `setInstanceDataBuffer(GPU-written dynamic vertex buffer, start, capacity)` + `submit(indirect)` 是否使用间接参数中的 instanceCount；若否，M7.5 改为直接 draw 或新 shader 方案。
- [ ] `DynamicVertexBuffer` 的 layout 是否必须用于 compute 写 `mat4`；shadow instance buffer 的 stride 与 `ShadowInstanced` 的 `i_data0..3` 完全匹配。
- [ ] buffer path 的 shader variant 是否能在现有 shader manifest/ProgramKey 中表达，不引入运行时反射。
- [ ] `RenderGraph` transient buffer pool 对 storage/indirect flags 的 key 是否足够；避免把 indirect buffer 复用为普通 storage。
- [ ] M7.6 是否能在当前 bgfx 上找到任何公开 async compute API；没有则直接记录阻塞，不做后端私有 hack。
- [ ] buffer path 的默认值/CLI 名称在实现前与现有 `--gpu-light-list` 语义对齐，避免两个总开关冲突。

---

## 8. 文档与代码边界

本文只做规划，不包含功能实现。进入实现后：

- 每个子任务完成后更新本文件状态、实际 HDRP `file:line`、测试/截图/性能结果；
- 更新 `docs/implementation-plan.md` 的 M7 状态、当前任务、M7 出口标准；
- 更新 `AGENTS.md` 的 buffer/indirect/shadow 约束（尤其：D3D11 不依赖 indirect count、async 无 queue、shadow request 仍 CPU）；
- 更新 CLI/README/命令示例；
- 临时场景、截图、日志保存在 `C:\Users\chenfuling\AppData\Local\Temp\`，不提交到仓库；
- 未完成/阻塞的能力明确写“未实现/阻塞”，不把规划写成已完成。
