# M8 Scalable Light Lists（32/64/128 容量）规划
> 工程根：`E:\Learn\hdrp-bgfx`。> 状态：**M8.0–M8.7 完成**（2026-11-14）；M8 验收通过。> 原则：先读 HDRP 与 vendored bgfx 源码再实现；不改动 M7 已验证的视觉语义；容量扩展必须保留 32-light M7 基线的 **0 diff** 回退。> 前置基线：M7.8 已完成，`supportGpuLightList=true`、`supportGpuLightListBuffers=true` 默认开启；texture/CPU 仍可回退；cluster 每 cell 固定 32。
---

## 0. 结论摘要

M8 的目标是把 punctual light 容量从固定 32 扩展到 **32/64/128 三档**，同时保持 M7 的 FPTL 16×16、big-tile 64×64、cluster 64×64×32、升序 light index 和 CPU/texture/buffer 可对照语义。
M8 的核心工作：

1. **容量契约**：`lightListCapacity = 32|64|128`，总 slot 包含最多 1 个 directional；punctual 最多为 capacity-1（有方向光时）。默认候选 64；32 为基线回退，128 为 experimental。
2. **动态 list stride**：FPTL/big-tile 的 `count + capacity` 行布局从固定 33 改为运行时 `capacity+1`；cluster 保持固定 33（每 cell 最多 32）。
3. **GPU producer/consumer 扩容**：screen AABB、big-tile、FPTL、cluster 和所有读取 list 的 shader 移除硬编码 32；128 slot 用 4 个 32-bit mask；4 threads 循环写出。
4. **容量选择与回退**：buffer path 支持 128；texture path 受 `maxTextureSize` 限制，超限时按 128→64→32 降低 effective capacity 并一次性告警，不静默截断或越界。
5. **验收与性能**：32 与 M7 0 diff（64 双后端、CPU/texture/buffer 0 diff，128 buffer 0 diff；64/128 的 1080p 性能和内存有记录，M8.6 再决定默认容量。
M8 **不引入** HDRP 的 16-bit packed list、layered cluster offset、light category/feature variants、light layers、cookies、area lights、GPU shadow atlas packing。这些在 M9 或后续模块处理。
### 0.1 与 M7 计划的差异
M7 文档曾把 layered cluster offset 和 64/128 lights 留给 M8。M8 重新评估后做如下收敛：
- **64/128 容量进入 M8**，这是 M8 的主体。
- **layered offset/16-bit packing 不进入 M8**：当前固定 32-bit row 在 128 lights、 1080p 下约 6.6 MB，仍可接受；先保证 0 diff、texture fallback 和容量正确，避免同时改 list 编码、cluster 分配和所有消费者。
- **cluster 每 cell 上限保持 32**：HDRP 的 cluster cell 硬上限是 24（默认资产值 8，`ShaderConfig.cs:70`/`GlobalLightLoopSettings.cs:207`），本项目 32 已高于 HDRP；64/128 只提高总容量和 FPTL/big-tile 容量，不改变 cluster cell 语义。
- **category/layer/feature variants/cookies** 不进入 M8；它们会改变材质 lighting variant、GBuffer channel 与 light data 契约，单独作为 M9 规划。
### 0.2 默认决策（暂定）

- 开发期默认仍为 32，M8.6 通过门禁后才考虑把默认改为 64。
- 128 永远先作为 opt-in/experimental；不因 128 能跑就默认开启。
- `supportGpuLightListBuffers` 维持 M7.8 的默认 true；buffer 不可用时 texture/CPU 回退。
- M7 的 GPU shadow culling/indirect 默认仍为 false，M8 不改变。
---

## 1. 当前基线与缺口
### 1.1 当前 32-light 契约

- `src/render/lighting/gpu_light_data.h:11-18`：
  - `kMaxGpuLights=32`、`kLightListMaxLights=32`、`kLightListStride=33`；
  - `kLightTileSize=16`、`kLightBigTileSize=64`、`kClusterTileSize=64`、`kClusterSliceCount=32`；
  - `GpuLightData` 为 `32 * 6` 个 `Vec4`。
- `src/render/lighting/light_builder.cpp:92-95`：directional 最多 1 个，`maxPunctual = kMaxGpuLights - 1`；按 importance 排序后截断。
- `src/render/lighting/light_list_builder.cpp:176-332`：CPU 同时构建 FPTL、big-tile、cluster，输出 `count + 32 indices`，cluster 固定 33 texel row。
- `shaders/common/light_data.sc:14-140`：`HBG_LIGHT_LIST_STRIDE=33`、`HBG_LIGHT_CLUSTER_STRIDE=33`、`HBG_LIGHT_CLUSTER_MAX_LIGHTS=32`，并硬编码 32 的 loop 上限。
- `shaders/lighting/light_loop.sc:78-116`：`lightCount` 与 tile/cluster count 部 `min(..., 32)`。
- `src/render/passes/forward_renderer.cpp:559-566`：light data texture 高度 32、screen bounds 宽度 `32*2`。
- `src/render/passes/forward_renderer.cpp:1327-1331`、`:1758-1766`：buffer 大小按固定 33 stride 计算并校验。
### 1.2 当前 GPU producer

- `light_screen_aabb_buffer.cs.sc`：64 threads 覆盖 32 个绝对 slot；无效 slot 写零。
- `light_big_tile_buffer.cs.sc`：一个 workgroup 对应 64×64 big tile，`uint` mask 只能覆盖 32 个 light。
- `light_fptl_buffer.cs.sc`：一个 workgroup 对应 16×16 tile，mask/32-slot shared list。
- `light_cluster_buffer.cs.sc`：一个 workgroup 对应 `(64×64 tile, slice)`，最多 32 light。
- M7 的 `light_list_passes=4`（cluster； `3`（no-cluster； `5`（indirect）保持不变。
### 1.3 当前限制

- 超过 32 个 punctual lights 时，`LightBuilder` 直接截断；没有诊断说明被截断的数量。
- FPTL/big-tile/cluster 的 stride 和 mask 都是编译期 32，无法只改一个设置扩容。
- cluster 每 cell 已固定 32；扩容总容量时如果直接沿用，会出现“FPTL 有 64、cluster 只有 32”的预期差异，需要明确测试和 debug。
- texture path 的 list 宽度随 capacity 增长，可能超过 backend `maxTextureSize`；buffer path 无该限制。
---

## 2. HDRP 源码结论

### 2.1 容量与上限
HDRP 的 FPTL 容量是预设档位，不是任意值：

- `com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:10-19`：
  - `FPTLMaxLightSizes.Low=31`、`High=63`、`Higher=127`、`Ultra=255`。
- `ShaderConfig.cs:58-70`：默认 `FPTLMaxLightCount=High (63)`；`LightClusterMaxCellElementCount=24`。
- `ShaderConfig.cs:132-151`：
  - big-tile `s_MaxNrBigTileLightsPlusOne = clamp((FPTLMax+1)*8, 512, 1024)`；
  - FPTL `s_LightDwordPerFptlTile = (FPTLMax+1)/2`； 6-bit packed）；
  - cluster block/offset packing bits。
- `GlobalLightLoopSettings.cs:201-207`：`maxDirectionalLightsOnScreen=16`、`maxPunctualLightsOnScreen=512`、`maxLightsPerClusterCell=8`（运行时资产可配，ShaderConfig 写 cap 24）。
- `LightLoop.cs:685-686`：从 `lightLoopSettings` 读取 directional/punctual 上限。
对 M8 的含义：HDRP 的 63/127 对应本项目的 64/128 总 slot（含 1 个 directional，punctual 63/127），容量方向和 HDRP High/Higher 一致；本项目仍远低于 HDRP 的 512 punctual 上限。
### 2.2 FPTL/big-tile 列表

HDRP 使用 16-bit light index，count 放在第一个 dword 的低 16 位：

- `LightLoopDef.hlsl:232-262`；
  - `GetCountAndStartTile` 读 `g_vLightListTile[...] & 0xffff`；
  - `FetchIndex` 用 `(lightOffset+1)>>1` 和 16-bit shift 取 index。
- `lightlistbuild.compute:413-422`；
  - 每个 category 的 count clamp 到 `SHADEROPTIONS_FPTLMAX_LIGHT_COUNT`：
  - 每个 dword 写两个 16-bit index。
- `HDRenderPipeline.LightLoop.cs:621-657`：
  - FPTL buffer 为 `LightCategory.Count * s_LightDwordPerFptlTile * nrTiles`；
  - big-tile buffer 为 `s_MaxNrBigTileLightsPlusOne * nrBigTiles / 2`；
  - cluster offset/index 为独立 buffer。
M8 **不移植** 16-bit packing：当前 32-bit 行布局在 64/128 下内存可接受，且能保持 texture fallback 与 M7 0 diff； 6-bit packed list 作为 M9 的可选优化，届时和 category/layered 一起做。
### 2.3 Cluster 不 layered offset

HDRP cluster 由两部分组成：
- `ClusteredUtils.hlsl:106-111`：`GenerateLayeredOffsetBufferIndex` 按 category/tile/cluster 计算 offset buffer 下标。
- `LightLoopDef.hlsl:286-315`；
  - `UnpackClusterLayeredOffset` 从 packed uint 取 offset/count；
  - `GetCountAndStartCluster` 先读 offset/count，再以 `g_vLightListCluster[start+i]` 可 index。
- `lightlistbuild-clustered.compute:76-78`：`PackClusterLayeredOffset(offset,count)`。
- `lightlistbuild-clustered.compute:482-502`：写入 `g_vLayeredLightList`，并用 `g_LayeredOffset` 记录每个 category/tile/cluster 的 start/count。
- `LightLoop.cs:871-874`：每 cluster tile 的 index 容量约为 `(FPTLMax+1) * (1 << (log2Clusters+1))`。
M8 保持本项目固定 row：`count + 32 indices`，cluster stride 33；原因是：
1. M8 的 64/128 只扩总容量，cluster cell 上限仍 32：
2. 固定 row 可保持确定性升序输出和 M7 0 diff：
3. layered/atomic append 会引入非确定性顺序或多个 buffer，适合和 category/layer 一起在 M9 做。
### 2.4 HDRP 与本项目映射

| 项
| HDRP | M8 本项目 |
|---|---|---|
| FPTL 容量 | 31/63/127/255（`ShaderConfig.cs:10-19`）
| 32/64/128 总 slot（含 1 directional） |
| big-tile 上限 | `clamp((FPTLMax+1)*8,512,1024)`（`ShaderConfig.cs:133`）
| effective capacity（32/64/128； |
| tile 尺寸 | FPTL 16、big-tile 64、cluster 32、 2 slices（`LightLoop.cs:106-108`）
| FPTL 16、big-tile 64、cluster 64×32 slices（保持 M6/M7； |
| cluster cell 上限 | 默认资产 8，ShaderConfig cap 24（`GlobalLightLoopSettings.cs:207`、`ShaderConfig.cs:70`）
| 固定 32 |
| FPTL 编码 | 16-bit packed + count low 16（`LightLoopDef.hlsl:242-262`）
| 32-bit row + count dword（M8 暂不 packed） |
| cluster offset | layered `g_LayeredOffset` + 全局 index list（`LightLoopDef.hlsl:286-315`）
| 固定 33 row，确定性升序（M8 不 layered） |
| category/feature | Punctual/Area/Env/Decal + feature flags（`LightLoop.cs:66-83`）
| 仍 punctual + 1 directional |
| 默认容量 | FPTL High=63（`ShaderConfig.cs:65`）
| 32 baseline；64 候选；128 experimental |

### 2.5 不进入 M8 的 HDRP 能力

- **Category/feature flags**：`LightLoop.cs:66-83`（Punctual/Area/Env/Decal 与 feature flags），`lightlistbuild.compute:356-386` 的分类计数 feature flags；M8 只有 punctual + 1 directional。
- **Light layers**：`LightLoop.hlsl:305` 的 `IsMatchingLightLayer`，`HDAdditionalLightData.cs:1033-1073` 的 light layer mask；需要 GBuffer/prepass rendering-layer channel，M9 单独规划、
- **Cookies/IES**：`LightCookieManager.cs:47-62`、`:242-317` 的 cookie atlas/atlas packing；M9 或独立模块。
- **Shadow request/atlas**：`HDShadowCullingUtils.cs:64-132`、`HDShadowManager.cs:1414-1452` 仍是 CPU request/atlas；M8 不改 shadow 数据契约。
---

## 3. M8 任务拆分

| 任务 | 产出 | 状态 |
|---|---|---|
| **M8.0 容量契约/基线锁 * | `32/64/128` 设置、CLI/JSON/UI、IDL、 2 baseline 回归 | `[x]` 完成（GPU >32 暴 gate； |
| **M8.1 CPU reference** | `LightBuilder`/`LightListBuilder` 容量参数、边界 截断测试 | `[x]` 完成 |
| **M8.2 GPU FPTL/big-tile** | 128-slot AABB、4×mask、动态 stride、64/128 0 diff | `[x]` 完成（2026-11-13） |
| **M8.3 GPU cluster** | 32/cell 保持、128 输入、overflow 打包/debug、0 diff | `[x]` 完成（2026-11-13） |
| **M8.4 Buffer/texture consumers** | forward/deferred/contact/fog/debug 移除 32 硬编码；texture/CPU 128→64→32 clamp | `[x]` 完成（2026-11-14） |
| **M8.5 集成/回退/CLI/UI** | capacity 选择、texture 宽度 clamp、resize/4K、诊断 | [x] 完成（2026-11-14） |
| **M8.6 性能与默认决策** | 32/64/128 1080p 300 帧、内存、默认值 | [x] 完成（2026-11-14） |
| **M8.7 M8 验收** | 双后端矩阵、测试/IDL/gfx/docs、边界 | [x] 完成（2026-11-14） |

依赖：M8.0 → M8.1 → M8.2/M8.3 → M8.4 → M8.5 → M8.6 → M8.7。M8.2 与 M8.3 在 M8.1 后可以并行，但都必须先有 CPU reference。
---

## 4. 数据契约

### 4.1 容量档位

| 设置 | 总 slot | 有 directional 时 punctual 上限 | FPTL/big-tile stride | cluster stride | cluster cell 上限 |
|---|---:|---:|---:|---:|---:|
| 32 | 32 | 31 | 33 | 33 | 32 |
| 64 | 64 | 63 | 65 | 33 | 32 |
| 128 | 128 | 127 | 129 | 33 | 32 |

- `lightListCapacity` 是总 slot；没有 directional 时 punctual 可以使用全部 capacity。
- directional 仍只支持 1 个主方向光；失 directional 不在 M8。
- cluster 每 cell 上限保持 32；即使总容量 64/128，cluster 也不会超过 32。`--no-light-cluster` 用 FPTL 时可以看到完整 64/128。
- FPTL/big-tile 每 tile 的 light 上限等于 effective capacity。
- 所有 list 仍按绝对 `GpuLightData` index 升序；`LightBuilder` 的 importance 排序只决定截断哪些 light，不改变 list 内部升序。
### 4.2 GpuLightData 不 uniform

- `kMaxGpuLights` 编译期上限改为 **128**；`GpuLightData::texels` 为 `128 * 6` 个 `Vec4`。
- light data texture 高度上 **effective capacity**（ 2/64/128），宽度仍 6；consumer 读取 `index < capacity`。
- `LightScreenBounds` buffer 固定 `2 * 128` 个 vec4；超出 effective capacity 的 slot 写零。
- `PassUniforms`/`ClusterUniforms` 新增或镜像 `lightListParams2 = (listStride, capacity, maxFptlLights, maxBigTileLights)`：
  - `listStride = effectiveCapacity + 1`：
  - `capacity = effectiveCapacity`：
  - `maxFptlLights = maxBigTileLights = effectiveCapacity`：
  - cluster cell 上限仍由 `HBG_LIGHT_CLUSTER_MAX_LIGHTS=32` 表达。
- 不改变 `lightListParams = (tileSize,tileCountX,tileCountY,enabled)` 的既有语义。
### 4.3 FPTL/big-tile 行布局

保持 M6/M7 的 32-bit 行布局，只把 stride 变成运行时：

```text
row = (tileY * tileCountX + tileX) * listStride
row[0] = lightCount
row[1..capacity] = absolute light index（升序）
```

- texture path：`width = tileCountX * listStride`，`height = tileCountY`：
- buffer path：`count = tileCountX * tileCountY * listStride`；
- big-tile 同样使用 `listStride`，每 big tile 最多 effective capacity 不 light：
- 不改变 16×16/64×64 tile 尺寸和升序语义。
### 4.4 Cluster 行布局

保持固定 33 stride：
```text
row = (tileY * 32 + slice) * tileCountX + tileX
row[0] = lightCount； = 32； row[1..32] = absolute light index（升序）
```

- cluster 输入仓 big-tile list 读取，big-tile 可以有 64/128 不 light：
- cluster 输出 clamp 到 32；超过 32 的 light 不进入 cluster，但计入 overflow 诊断；
- debug slice、transparent/forward-only、fog 的 cluster 读取保持 M7 语义；
- 不引入 `g_LayeredOffset`/全局 atomic append；M8 的 cluster 是固定 row，确定性升序。
### 4.5 路径不 effective capacity

三层路径保持：
```text
buffer GPU -> texture GPU -> CPU fallback
```

- **buffer**：支持 32/64/128，不参 texture width 限制；所有 list buffer 持 effective capacity 重建。
- **texture**：支持 32/64/128，但 `width = tileCountX * (capacity+1)` 必须 <= `bgfx::getCaps()->limits.maxTextureSize`；cluster 宽度 33×tileCountX 不变。
- **CPU fallback**：和 texture 一样受 width 限制；如果超限，effective capacity 逐档降低。
- `effectiveCapacity` 选择顺序：请求 128 → 64 → 32；buffer 可用时不降低。
- 降低发生时只警告一次，并在 diagnostics 中记录 `light_list_capacity_clamped=true`、`light_list_capacity_requested/effective`。
- 不允许把请求 128 静默变成 128 个 light 但只写 32 个；截断必须来自明确的 capacity 规则。
### 4.6 帧顺序
M8 不改变 M6/M7 的 light-list 帧顺序：

```text
ClearLightLists
GPU Light Screen AABB
GPU Big Tile Light List
GPU FPTL Light List
GPU Light Cluster
GBuffer / Forward / Contact / Fog / Debug consumers
```

- `light_screen_aabb_buffer` 的 slot 循环覆盖 128：
- cluster 仍输出 32/cell：
- indirect dispatch 的 group 数不回 light capacity 改变，只改变每个 group 内的 slot 数。
### 4.7 不 shadow/contact/fog 的边界
- 1 个 directional shadow 不变；shadow request 上限仍为 64。
- contact shadow mask 实际为 **8-bit**；最多 8 个 contact shadow light，超过部分不分配 contact mask（保持 M5 语义）。
- 体积雾 punctual light 上限仍由 fog settings（默认 8）控制。
- shadowedCount/阴影索引和 light capacity 解耦；不因 capacity=128 自动创建 128 个 shadow request。
- M7 GPU shadow culling/indirect 仍是 opt-in，M8 不修改其数据布局。
---

## 5. 子任务详细设计
### M8.0 容量契约与基线锁

目标：先固定数据契约和 32-light 回归，不改视觉。
- 新增 `PipelineSettings::lightListCapacity`（32/64/128），`sanitize` 到合法档位、
- 新增 CLI `--light-capacity=32|64|128`；JSON key `lightListCapacity`；DebugUI 显示 requested/effective。
- `kMaxGpuLights=128`、`kLightListMaxLights=128`、`kLightListStrideMax=129`；删除固定 `kLightListStride=33` 的 C++ 编译期用法，`LightListBuilder` 使用运行时 `capacity+1`。GPU shader 的 33/32 固定值在 M8.2 才替换；在此之前 GPU 路径把请求容量 clamp 到 32 并只警告一次。
- 先锁定 M7 32-light 基线：`m61_thirtytwo`、`m61_transparent`、fog、contact、deferred、forward，在 capacity=32、buffer/texture/CPU 下与 M7 截图 **0 diff**。
- 单测：capacity 合法值、非法值回退、effective capacity、 2 baseline 不走 128 分支。
验收： 2-light 全部既有测试/截图不变；新设置可解析 序列化；无 shader 编译错误。
M8.0 验证：ctest **207 cases / 20,170 assertions** + IDL check 通过；D3D11/Vulkan 默认 buffer、texture 和 --no-gpu-light-list CPU 路径对 M7 default_*.png **0 diff**；M8.2 后 --light-capacity=64/128 不再 clamp，见第 12 节。
### M8.1 CPU reference

目标：CPU `LightListBuilder` 和 `LightBuilder` 支持 32/64/128，cluster 仍 32/cell。
- `LightBuilder::build(..., capacity)`：按 importance 排序后截断到 `capacity`；记录 `truncatedLightCount`（候选数 - 选中数）。
- `LightListBuilder::build(..., capacity)`；
  - `lightRects/lightVisible` 扩到 128：
  - FPTL/big-tile 的 list 上限和 stride 使用 effective capacity；
  - cluster 仍截断 32，并记录 `overflowClusters`；
  - `textureWidthFor(width, capacity)`、`bigTileTextureWidthFor(width, capacity)`、cluster width 保持 33：
  - `maxLightsPerTile`/`maxLightsPerBigTile`/`maxLightsPerCluster` 统计准确。
- 单测覆盖；
  - 31/32/33、 3/64/65、 27/128/129 灯；
  - 有 时 directional：
  - 64/128 灯同时落在同一 tile、big tile、cluster：
  - cluster 只有前 32 个升序 index）
  - 截断顺序为 importance 排序一致；
  - texture width 计算。
- 为 M7 32-light CPU reference 逐 tile/逐 cluster 对照 0 diff。
验收：CPU 测试全过；128 capacity 下无越界。
M8.1 验证：`LightBuilder` 在 capacity=32 截断 33 个候选为 32/truncated=1，capacity=64 保留 33；`LightListBuilder` capacity=64/128 的 FPTL stride 为 65/129、128 灯同 tile 计数为 128、cluster 仍 32/cell 且记录 overflow；32-light 默认路径不 M7 0 diff。
### M8.2 GPU big-tile/FPTL

目标：GPU producer 支持 128 slot 和动态 stride。M8.0/M8.1 已完成 CPU/setting；M8.2 同时移除 `ForwardRenderer::setSettings` 的 32 容量临时 gate 和 shader 的固定 33/32。
- `light_screen_aabb_buffer.cs.sc`：
  - 64 threads，每个 thread 负责 2 个绝对 slot； ..127）；
  - 超出 effective capacity 的 slot 写 `visible=0`；
  - 保持 AABB `min/max` 布局不变。
- `light_big_tile_buffer.cs.sc` / `light_fptl_buffer.cs.sc`：
  - 使用 `uint s_mask[4]`； 28 bit）；
  - shared list `uint s_list[128]`：
  - 输入 big-tile count/pass uniform 使用 effective capacity；
  - `atomicOr` 用 `mask[index >> 5]`：
  - thread 0 持 0..capacity-1 扫描 mask 生成升序 list；
  - 64 threads 循环写出 `row[1..capacity]`（不能假设 thread 数 = capacity）；
  - 输出 stride 仍 `lightListParams2.x` 读取，不写死 33。
- `light_indirect_args.cs.sc`：dispatch group 数不变，不依赖 capacity。
- buffer 大小/校验使用 `capacity+1`，并检查 `bufferCount == tileCountX*tileCountY*stride`。
- D3D11/Vulkan、capacity 32/64/128、buffer vs CPU reference **0 diff**。
- 32-light M7 回归 0 diff。
验收： 4/128 的 tile/big-tile light 集合和顺序与 CPU 一致；无 shared memory/越界/validation 错误。
### M8.3 GPU cluster

目标：在总容量 64/128 下保持 cluster 32/cell，并保证输入/overflow 正确。
- `light_cluster_buffer.cs.sc`：
  - 输入 big-tile list 可含 64/128 个 light，按 FPTL/big-tile 的动态 `listStride` 读取；输入循环不能继续用 `HBG_LIGHT_CLUSTER_MAX_LIGHTS` 截断 big-tile，只能用它限制输出；
  - 输出 shared list 仍 32：
  - 每个候选检查 `lightIndex < effective capacity`、AABB、depth slice：
  - 超过 32 的候选计入 overflow（shared/atomic 成 CPU 可见统计），不写越界；
  - 输出 count 和 32 个升序 index。
- cluster stride/纹理布局保持 33：
- `light_cluster_buffer` 的 debug slice 仍与 CPU 同 slice 0 diff；
- 64/128 lights 下：
  - cluster 前 32 个升序 index 不 CPU 一致；
  - 第 33 个及以后不出现；
  - transparent/forward-only cluster 路径仍工作；
  - `--no-light-cluster` 回退 FPTL 能看到完整 64/128。
- 不在 M8 引入 layered offset；如果实测 fixed row 32/cell 不可接受，M8.3 只记录数据，layered 转入 M9，不作为 M8 阻塞项。
验收：cluster 32/cell 边界。 64/128 输入、debug slice 全部 0 diff/无越界。
### M8.4 Buffer/texture consumers

目标：所有消费者不再硬编码 32，并保持 M7 绑定路径。
- `shaders/common/light_data.sc`：
  - `HBG_LIGHT_LIST_STRIDE` 改为 `u_pass_lightListParams2.x`（buffer/texture 都用运行时 stride）；
  - `HBG_LIGHT_LIST_MAX_LIGHTS` 不 128：
  - cluster 宏保持 `33/32`；
  - `hbgLightListCount/Index` 用 `tileCountX * stride`；
  - texture path 的 `texelFetch` x 坐标用运行时 stride：
  - 所有 index 读取动 `index < capacity` 防护。
- `shaders/lighting/light_loop.sc`：
  - FPTL `lightCount`/tile count clamp 分 128：
  - cluster clamp 仍 32：
  - fallback loop 最多 128：
  - 不改变 directional 单独循环和 punctual 求值顺序。
- forward/deferred/contact/fog/debug：
  - 绑定 `s_lightList` 的 stage 7 不变；
  - `bindLightList` 的 buffer/texture 条件不变；
  - debug light-cluster 继续读 cluster 33 stride：
  - fog punctual 仍由 fog settings 限制，不回 128 扩容。
- `debug` 输出 capacity/stride/cluster cap，便于定位 tile/list 错误。
验收：capacity 32/64/128、buffer/texture、forward/deferred/transparent/fog/contact/debug 同后端 0 diff；M7 32 基线 0 diff。
### M8.5 集成、回退、CLI、UI

目标：容量选择可配置、可诊断、可回退。
- CLI/JSON/UI：
  - `--light-capacity=32|64|128`：
  - `config/pipeline.json` 增加 `lightListCapacity`（默认 32，M8.6 再决定是否切 64）；
  - DebugUI 显示 requested/effective capacity、stride、path、truncated/overflow。
- effective capacity；
  - buffer path 直接使用请求值；
  - texture/CPU path 检查 `(capacity+1)*tileCountX <= maxTextureSize`，不满足则逐档降低：
  - 发生 clamp 只警告一次，记录 requested/effective；
  - 不因 capacity 降低而改变 32-light 基线。
- 资源：
  - light data texture 高度随 effective capacity；
  - screen bounds 固定 2*128：
  - FPTL/big-tile buffer/texture 随 stride 重建；
  - cluster 资源不变；
  - resize/RT scale/4K 后重建并校验，无泄漏。
- diagnostics：
  - `light_list_capacity_requested/effective`；
  - `light_list_stride`；
  - `light_truncated`：
  - `cluster_overflow`：
  - 保留 M7 的 `light_list_passes/cpu_ms/path`。
- 4K/1440p：
  - capacity 64/32 在 4K texture 宽度内（按实际 maxTextureSize 校验）；
  - capacity 128 在 1080p texture 可运行，1440p 以上应走 buffer 成 clamp：
  - buffer path 作为 128 的主路径。
- 失败回退：buffer 不可用 → texture；texture 宽度不足 → 限 capacity；再失败 → CPU reference；每一步只警告一次。
验收：CLI/JSON/UI/诊断正确；resize/0.5/4K 无越界 泄漏； 2 回归不变。
### M8.6 性能与默认决策
目标：给出 32/64/128 的真实成本和默认值。
- 1080p 300 帧 full post（沿用 M7.7 场景/设置）：
  - capacity 32 baseline：
  - 64 目标 >60 fps，且不显著慢于 32：
  - 128 记录 fps/内存，不要求默认达标。
- 记录：
  - FPTL/big-tile/cluster buffer 字节数；
  - `light_list_stride`、max lights、truncated/overflow；
  - GPU/CPU frame、shadow/contact/fog 是否受影响。
- 默认决策：
  - 64 通过双后端 0 diff、 2 回归。 1080p >60、无泄漏后才可默认；
  - 128 不作为默认容量，保持 opt-in/experimental：
  - 32 始终是安全回退。
- 如果 64 在目标 GPU 上明显退化，保留 32 默认并记录 64 opt-in；不为了“容量”牺物 60 fps 门禁。
验收：性能日志、内存表、默认决策写入文档。
### M8.7 M8 验收

- 视觉： 2 M7 基线 0 diff；64/128 CPU vs GPU vs texture/buffer 0 diff；cluster 32/cell 边界正确。
- 数值：max lights/stride/count/overflow 统计正确；无越界/空 list。
- 后端：D3D11/Vulkan 双后端、
- 场景：forward/deferred/transparent/fog/contact/debug/0.5/4K。
- 性能（1080p 300 帧 64 >60；128 记录。
- 门禁：`hbg_tests`、`ctest`、`hbg_idl_check`、`--gfx-test`、`git diff --check`、文档更新。
- 回退：buffer → texture → CPU；texture 宽度 → capacity clamp；资源释放无泄漏。
---

## 6. 验收矩阵

| 用例 | 目标 |
|---|---|
| 32-light M7 回归 | m61_thirtytwo/transparent/fog/contact/deferred/forward 在 CPU/texture/buffer 不 0 diff |
| 64-light CPU vs GPU | tile/big-tile 集合/顺序 0 diff；cluster 前 32 0 diff；D3D11/Vulkan |
| 128-light buffer | 1080p 0 diff；tile/big-tile 完整；cluster 32/cell |
| 128-light texture | 1080p 宽度可容纳时 0 diff；超限时 clamp/buffer |
| 64-light texture | 4K 宽度可容纳时 0 diff；否则 clamp |
| capacity 边界 | 31/32/33、 3/64/65、 27/128/129；截断顺序正确 |
| cluster 边界 | 32/33 个重叠 light；第 33 个不进 cluster；无越界 |
| transparent/forward-only | cluster 32/cell；`--no-light-cluster` 回退 FPTL 64/128 |
| fog/contact | fog 8-light 上限、contact 32-bit mask 不变；不出现空 list/错误 shadow |
| buffer/texture/CPU | 同后端 0 diff；失败逐层回退只警告一次 |
| resize/0.5/4K | list/buffer/texture 重建、stride/尺寸正确、无泄漏 |
| 性能 | 1080p 300 帧 64 >60；128 记录； 2 baseline |
| 门禁 | 单测/ctest/IDL/gfx/docs/diff 全通过 |

---

## 7. 性能与内存预算（估算）
1080p 下（`tileCountX=120`、`bigTileCountX=30`、cluster tiles `30×17`； 2 slices）：

| capacity | FPTL stride | FPTL bytes | big-tile bytes | cluster bytes； 3：
| 合计（不同 light data） |
|---:|---:|---:|---:|---:|---:|
| 32 | 33 | 120×68×33×4 ≁ 1.08 MB | 30×17×33×4 ≁ 67 KB | 30×17×32×33×4 ≈ 2.15 MB | ≁ 3.30 MB |
| 64 | 65 | ≁ 2.12 MB | ≁ 133 KB | 2.15 MB | ≁ 4.40 MB |
| 128 | 129 | ≁ 4.21 MB | ≁ 263 KB | 2.15 MB | ≁ 6.63 MB |

- light data texture； 128×6×16 ≈ 12 KB；screen bounds 固定 128×2×16 = 4 KB。
- texture path 宽度； 1080p FPTL 为 3960/7800/15480；128 已接近 16384 上限， 440p 128 会超过限制。
- buffer path 时 texture 宽度限制，但 128 的 FPTL 带宽/循环成本需要实测。
- 内存预算不是默认开启 128 的理由；M8.6 按实际 GPU 结果决定。
---

## 8. 风险与处理
1. **shared mask/shared list 扩容**； 128 bit 需要 4 个 `uint`，thread 0 扫描 128；shared list 128。先做 64，再开 128；每步对照 CPU。
2. **64 threads 写出 128 indices**：不能沿用 `if (thread < MAX)`；必须用 `for (i=thread; i<capacity; i+=64)`，否则 64..127 丢失。
3. **texture 宽度限制**：128 在 1440p/4K 超 `maxTextureSize`；必须 query caps，按 128→64→32 2 clamp 或走 buffer，不静默越界。
4. **cluster 32/cell 与总容量不一致 *：这是有意边界，需 debug/overflow 统计和文档；不能用“cluster 32”误判为 128 全丢。
5. **contact/fog/shadow 上限未扩容 *：contact mask 仍 32 bit，fog 仍 8，shadow requests 仍 64；不要声称 128 灯全部有 contact/shadow/fog。
6. **FPTL 与 cluster 视觉差异**：opaque FPTL 64/128、transparent cluster 32；验收必须在同路径对照，`--no-light-cluster` 单独记录。
7. **M7 0 diff 回归**：改 stride 时先锁 32 baseline；M8.0 必须先截 32 图，再改 producer/consumer。
8. **shader 变体/实 *：`HBG_LIGHT_LIST_STRIDE` 不能再是固定 33；所有 include 路径统一任 uniform 读，避免 texture/buffer 分支不一致。
9. **IDL 对齐**：`PassUniforms`/`ClusterUniforms` 新增 vec4 会影响 offset；更新 `test_uniform_layout.cpp`，双后端 gfx 验证。
10. **scope 膨胀**：M8 不顺手做 layered/16-bit/category/layer/cookie/shadow atlas；出现需求新建 M9。
---

## 9. 待确认/实现前检查
- [ ] `maxTextureSize` 在 D3D11/Vulkan 实际值；128 在 1080p 是否稳定小于上限。
- [ ] 128-slot AABB 用 64 threads 还是 128 threads；D3D11/Vulkan shared/atomic 成本、
- [ ] `uint[4]` mask 在 GLSL/HLSL/SPIR-V 的 shared memory 声明和 `atomicOr` 兼容性。
- [ ] FPTL/big-tile 的 `lightListParams2` 改 Pass/Cluster 两套 uniform 是否需要重复上传。
- [ ] texture path 128 的宽度是否在目标 backend 上稳定（1080p 15480）。
- [ ] 64 threads 写 128 index 的循环在 FPTL/big/cluster 三个 shader 中一致。
- [ ] `LightBuilder` 的 importance 截断是否需要在 diagnostics 中显示；是否保持现有排序稳定。
- [ ] 64 默认容量是否满足 1080p >60；如果否，是否只保留 64 opt-in。
- [ ] 128 cluster 32/cell 是否足够；layered 是否确定界 M9。
- [ ] shadow/contact/fog 上限是否需要在 UI 中提示，避免误解 128 全功能。
---

## 10. M8 明确不做

- HDRP 16-bit packed FPTL/big-tile list；M8 继续 32-bit row。
- Layered cluster offset、global atomic append、per-category offset buffer。
- Light category（Area/Env/Decal）、feature flags、per-material lighting variants。
- Light layers/rendering layer masks、GBuffer rendering-layer channel。
- Cookies/IES、area lights、multi-directional >1。
- GPU shadow request/atlas packing、cached shadow GPU 更新；M7 shadow 扩展仍是 opt-in。
- 256/512 lights、D3D12、DXR/RT、bindless/descriptor indexing、Hi-Z、meshlet/mesh shader。
- 改变 M7 默认的 GPU shadow culling/indirect/async 开关。
---

## 11. 文档与代码边界
本文件只做 M8 规划，不包含功能实现。进入实现后：
- 每个子任务完成后更新本文件状态、实际 `file:line`、测试/截图/性能：
- 更新 `docs/implementation-plan.md` 的 M8 状态和下一任务：
- 更新 `AGENTS.md` 的容量 list stride/cluster 32 边界：
- 临时场景/截图/日志继续放在 `C:\Users\chenfuling\AppData\Local\Temp\`，不提交到仓库；
- 未完成或阻塞的能力明确写“未实现/阻塞”，不把规划写成已完成。
M8 出口： 2/64/128 容量契约稳定；32 M7 基线 0 diff（64 双后端 0 diff 与 1080p >60；128 有可运行的 buffer 路径和真实性能记录；texture/CPU 回退、resize/4K、诊断、测试和文档齐全。

---

## 12. M8.2 实现记录（2026-11-13）

### 12.1 代码
- `light_screen_aabb_buffer.cs.sc` / `light_screen_aabb.cs.sc`：64 threads × 2 slots，覆盖 0..127；超出 effective capacity 的 slot 写零。
- `light_big_tile_buffer.cs.sc` / `light_big_tile.cs.sc`、`light_fptl_buffer.cs.sc` / `light_fptl.cs.sc`：`uint mask[4]`、`shared list[128]`，capacity/stride 从 `u_pass_lightListParams2` 读取；64 threads 循环写 `capacity` 个 index。
- `light_cluster_buffer.cs.sc` / `light_cluster.cs.sc`：big-tile 输入按 runtime stride 读取，候选 mask 扩到 128；输出保持 33 stride、32/cell，按升序取前 32。
- `shaders/common/light_data.sc`：FPTL/big-tile count/index 使用 `u_pass_lightListParams2.x` 运行时 stride；新增 `hbgLightListCapacity()`。
- `shaders/lighting/light_loop.sc`：light count/tile count/fallback 上限改为 capacity（128）。
- `ForwardRenderer::setSettings()`：移除 32 容量 gate；`ensureLightDataTexture()` 在 capacity 变化时重建 light-data texture；buffer 日志输出 capacity/stride。
- texture producer 同步支持 32/64/128；1280×720 / 1920×1080 的 128-light texture 宽度 10320/15480，未超 16384。

### 12.2 验证
- 构建：`cmake --build build/vs2022 --config Debug --parallel 8` 通过；shader 全量编译通过。
- `ctest --preset vs2022-debug --output-on-failure`：2/2 通过（207 cases / 20,170 assertions + `hbg_idl_check`）。
- `--gfx-test`：D3D11、Vulkan 通过。
- 32-light M7 基线：stress scene，D3D11/Vulkan buffer/texture 对 `hbg_m78\default_*.png`、`default_tex_*.png` 均 **0 diff**；`--no-gpu-light-list` CPU 与 GPU 也 **0 diff**。
- 64/128：自建 64/128 punctual 场景（32 个位置重复，保证同 tile 压力；单 tile/big-tile 最大 64/128）。
  - buffer/texture、cluster/no-cluster、D3D11/Vulkan、forward/deferred/transparent、GPU vs CPU 全部 **0 diff**。
  - 128-light forward buffer max lights tile=128、big tile=128；CPU cluster `overflow clusters=1440`，GPU 输出与 CPU **0 diff**。
  - 128-light indirect buffer 路径（D3D11）与 CPU **0 diff**。
  - `--render-scale=0.5` 128-light buffer 与 CPU **0 diff**。
  - 1920×1080 128-light texture 路径与 CPU **0 diff**；2560×1440 128-light texture 超宽时按既有行为回退 CPU，与 CPU **0 diff**。
  - directional + punctual 边界：1 个 directional + 127 punctual（capacity 128，max tile=127）与 1 + 63（capacity 64，max tile=63）GPU vs CPU **0 diff**。
- 临时产物：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m8_2\*`。

### 12.3 剩余
- M8.4：texture 路径 `maxTextureSize` clamp/诊断、contact/fog 既有 32 边界收尾、其余 debug/consumer 审计。
- M8.5：CLI/UI 的 requested/effective/clamped 诊断与 resize/4K 矩阵。
- M8.6：32/64/128 1080p 性能与默认容量决策。

### 13. M8.3 实现记录（2026-11-13）

#### 13.1 实现
- `light_cluster_buffer.cs.sc` / `light_cluster.cs.sc`：
  - big-tile 输入按 `u_pass_lightListParams2` 的 runtime stride 读取，候选上限为 effective capacity（32/64/128）。
  - mask 扩到 4×32-bit；输出 shared list 保持 32。
  - thread 0 升序扫描 mask，前 32 个写入 list；第 33 个起累加 `s_clusterOverflow`，不写越界。
  - count texel 打包为 `count | (overflow << 8)`，低 8 bit 仍是 count，高 8 bit 是溢出候选数（最多 96）。
- `shaders/common/light_data.sc`：
  - 新增 `hbgClusterLightListRawCount` / `hbgClusterLightListOverflow`；`hbgClusterLightListCount` 只取低 8 bit。
  - forward/deferred/transparent/fog 等消费者通过 helper 继续读取 32/cell count，M7 语义不变。
- `debug_light_cluster.fs.sc`：
  - `--debug-view=light-cluster` 的 overflow cell 显示为品红；绿色通道按 overflow 数量增强（1/8..1），便于区分 1 个和多个溢出。
- `LightListBuilder`：
  - CPU reference 同样按 cluster 记录 overflow count 并打包到 count texel；新增 `clusterOverflowCount()` 供测试/诊断。
  - `overflowClusters` 仍统计“有溢出的 cluster 数”。
- `tests/render/test_light_list.cpp`：
  - 新增 33 个同位置 light、capacity=64 的边界用例：count=32、overflow=1、index 升序 0..31；capacity=32 时不产生 overflow。
- HDRP 参考：`lightlistbuild-clustered.compute:453` 用 `min(iSum, MAX_NR_COARSE_ENTRIES)` 限制 cluster 写入、超限不越界；本实现保留同样的 32/cell 上限，并把超出数量额外写入 count 高 8 bit 作为诊断。

#### 13.2 验证
- shader 全量编译 + Debug 构建通过；`ctest` **208 cases / 20,209 assertions** + `hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test` 通过。
- 32-light M7 基线：D3D11/Vulkan buffer/texture 对 `default_*.png` / `default_tex_*.png` 仍为 **0 diff**。
- 33-light 同位置边界（capacity=64）：CPU `max lights cluster=32, overflow clusters=1440`；GPU/CPU buffer、texture、D3D11、Vulkan `--debug-view=light-cluster --debug-light-cluster-slice=2` 均 **0 diff**；overflow 像素颜色 `(255,32,115)` 对应 overflow=1，证明第 33 个 light 计入 overflow 且前 32 个升序输出。
- 64/128：normal forward/deferred/transparent、buffer/texture、cluster/no-cluster、D3D11/Vulkan GPU vs CPU 全部 **0 diff**；debug light-cluster slice 0/2 的 buffer/texture 与 CPU **0 diff**。
- 128-light max lights tile=128、big tile=128；`--no-light-cluster` 仍能读取完整 128；`--debug-light-cluster-slice=0` 与 CPU 同 slice **0 diff**。
- 临时产物：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m8_2\m83_*`、`m8_lights33_same.gltf`。

#### 13.3 剩余
- M8.4：texture `maxTextureSize` clamp/诊断，contact/fog 既有 32 边界收尾，其余 consumer 审计。
- M8.5：GPU overflow 数值 readback/CLI/UI 诊断（当前可经 debug view 观察，CPU reference 有 `clusterOverflowCount`）。
- M8.6：32/64/128 性能与默认容量决策。

### 14. M8.4 实现记录（2026-11-14）

#### 14.1 实现
- `LightListBuilder::textureFits()` / `textureCapacityFor()`：按 backend `maxTextureSize` 和 bgfx `uint16_t` pitch 的 16383 宽度上限，同时校验 FPTL、big-tile、cluster 三张 list 纹理；请求 128→64→32 逐档选择，0 表示没有可用档位。
- `ForwardRenderer`：新增 `m_lightListTextureCapacity` / `m_lightListEffectiveCapacity`；texture/CPU 路径使用 clamped capacity，buffer 路径保持 requested capacity；clamp 只在档位变化时告警一次，并输出 requested/effective/clamped 诊断。
- Consumers：`volumetric_fog.fs.sc` 的 `lightCount` 从硬编码 32 改为 `hbgLightListCapacity()`；`contact_shadows.cs.sc` 的 `lightCount` / `tileLightCount` 同样改为 capacity（contact mask 仍是 8 bit，最多 8 盏灯，不随 capacity 扩展）；`debug_light_data.fs.sc` 只显示 effective capacity 内的灯。
- 文档修正：contact shadow mask 实际为 8 bit（最多 8 盏灯，含方向光），旧 checklist 的“32-bit mask”描述不再适用；fog punctual 上限仍由 fog settings 决定（默认 8）。
- HDRP 参考：`LightLoop.hlsl` / `lightlistbuild-clustered.compute` 使用运行时 list count 和 `MAX_NR_COARSE_ENTRIES` 上限；本实现同样先取 effective capacity，再做 list/cluster 读取，不在 consumer 端写死 32。HDRP 没有 texture light-list 路径，`maxTextureSize` clamp 是本工程 texture 后端约束。

#### 14.2 验证
- shader 全量编译 + Debug 构建通过；`ctest` **209 cases / 20,225 assertions** + `hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test` 通过。
- 32-light M7 buffer/texture/CPU 基线 D3D11/Vulkan **0 diff**。
- 64/128 normal/fog/contact：buffer/texture、forward、D3D11/Vulkan GPU vs CPU 全部 **0 diff**。
- 1440p texture 128（D3D11 maxTextureSize=16384）自动 clamp 到 64，GPU/CPU **0 diff**；4K texture 128 在 D3D11/Vulkan 都因 16383 pitch 上限 clamp 到 64，GPU/CPU **0 diff**；4K texture 64/32 **0 diff**；4K buffer 128 保持 `capacity=128/128` 且无 fallback。
- 1440p texture 128 debug light-cluster slice 0 GPU/CPU **0 diff**；4K buffer 128 日志为 `path=buffer, capacity=128/128`。

#### 14.3 剩余
- M8.5：CLI/UI 的 requested/effective/clamped 完整展示、resize/RT scale/4K 矩阵（已覆盖主要路径）、GPU overflow 数值 readback。
- M8.6：32/64/128 1080p 性能与默认容量决策；contact shadow 在 128 下遍历更多候选，性能需实测。


### 15. M8.5 实现记录（2026-11-14）

#### 15.1 实现
- DebugUI：capacity 行显示 requested/effective/stride/truncated/clamped；新增 `Read GPU cluster stats` 按钮，显示 clusters/active/overflow clusters/overflow lights/max overflow/max lights。
- CLI：新增 `--cluster-stats-readback`；render test 自动补足 8 帧等待异步 readback；`render performance` 增加 `light_list_capacity_requested/effective/stride/truncated/clamped` 与 `cluster_stats_*` 字段。
- Buffer 路径：`bgfx::read(BufferRegion)` 直接读回 `lighting.lightClusterBuffer`；Texture 路径：创建 `R32U` + `BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK` staging texture，经诊断 view 4093 从 `lighting.clusterLightList` blit 后 `bgfx::read(TextureRegion)`；按 33-word stride 解析 count/overflow。
- `gfx::Buffer::native()` / `ResourceManager::native(BufferHandle)` 暴露 bgfx handle；readback 资源在 shutdown 释放，pending 时拒绝重复请求。
- clamp 告警改为仅在 texture/CPU 路径实际使用时输出；buffer 路径 4K 不再误报 texture clamp。
- HDRP 参考：`LightLoop.hlsl` / `LightLoopDef.hlsl` 的 runtime cluster count、`lightlistbuild-clustered.compute` 的 `MAX_NR_COARSE_ENTRIES` clamp；HDRP 不暴露 overflow 数值，本实现为诊断扩展。

#### 15.2 验证
- `ctest` **209 cases / 20,225 assertions** + IDL check 通过。
- 33 lights 同位置、capacity 64/128：CPU、D3D11/Vulkan buffer、D3D11/Vulkan texture 的 readback 均为 `clusters=7680 active=1440 overflow_clusters=1440 overflow_lights=1440 max_overflow=1 max_lights=32`（1280x720）。
- 1440p：`clusters=23040 active=4320 overflow_clusters=4320`；4K：`clusters=34560 active=6480 overflow_clusters=6480`；buffer/texture、D3D11/Vulkan 与 CPU reference 一致。
- 4K buffer 128 无 texture clamp 告警且 `effective=128/stride=129`；4K texture 128 告警 clamp 到 64（`effective=64/stride=65`），readback 仍与 CPU 一致。
- 32-light M7 baseline、gfx-test、`git diff --check` 通过。

#### 15.3 剩余
- M8.6：已完成，见 §16（默认容量切换为 64；128 保持 opt-in）。
- M8.7：M8 收尾、性能/内存/IDL/gfx/docs 全量验收。


---

## 16. M8.6 性能与默认决策（2026-11-14）

### 16.1 测试方法

- 构建：RelWithDebInfo；GPU：RTX 2060 SUPER；1920x1080；300 帧；`--no-vsync`。
- 场景：
  - 32-light：`m61_thirtytwo.gltf`（32 个 point light，32 个不同位置）；
  - 64-light：`m8_lights64_perf.gltf`（64 个 light node，32 个位置，每位置 2 个）；
  - 128-light：`m8_lights128.gltf`（128 个 light node，32 个位置，每位置 4 个）。
- 统一设置：`--pipeline-settings=config/pipeline.json --frame-settings=config/frame_settings.json --lit-shader-mode=forward --postprocess-profile=config/postprocess_quality.json`，Uffizi sky，shadows on，contact off（16.4 单独打开）。
- 每个 case 重复 7 次；wall fps 受桌面/DWM present 节流影响，结论以 GPU timer 和 CPU submit 为主，fps 只用于 60 门禁。表中为 min–max（avg）。

### 16.2 1080p 300 帧性能

| case | capacity | D3D11 fps | D3D11 GPU ms | Vulkan fps | Vulkan GPU ms |
|---|---:|---:|---:|---:|---:|
| 32-light | 32 | 67.7–75.9 (72.7) | 6.27–11.44 (8.53) | 88.2–104.2 (100.7) | 8.06–8.78 (8.35) |
| 32-light | 64 | 67.4–81.4 (73.2) | 6.40–10.16 (8.19) | 99.9–105.1 (102.0) | 8.10–10.01 (8.60) |
| 32-light | 128 | 66.8–74.2 (71.0) | 6.80–9.55 (8.16) | 100.1–109.8 (103.7) | 7.46–8.59 (8.15) |
| 64-light | 64 | 60.8–64.9 (63.1) | 12.33–20.51 (15.05) | 75.1–79.7 (77.0) | 10.94–11.91 (11.58) |
| 128-light | 128 | 40.0–43.8 (42.0) | 19.82–22.66 (21.32) | 48.2–51.3 (50.0) | 17.65–18.95 (18.53) |

CPU submit 平均：32-light/32 D3D11 8.94 ms / Vulkan 9.20 ms；64-light/64 15.29 / 12.58 ms；128-light/128 21.39 / 19.95 ms。所有 case light_list_truncated=0；shadow_drawn 分别为 130 / 134 / 138，未出现回退或空 list；fog 在性能矩阵关闭，contact 见 16.4。

结论：

- 32-light 场景下 capacity 32/64/128 的 GPU timer 在噪声范围内，没有“容量越大越慢”的稳定趋势；64 在双后端均 >60 fps。
- 64-light/64 的 D3D11 最低 60.8 fps、平均 63.1 fps；Vulkan 75.1–79.7 fps，满足 1080p >60 门禁，但 D3D11 余量较小。
- 128-light/128 双后端均 <60 fps（D3D11 40–44、Vulkan 48–51），不满足默认门禁；128 只保留 opt-in/experimental。
- 默认 deferred 路径复核（3 次）：64-light/64 D3D11 **63.4–65.2 fps**（GPU 12.79–14.52 ms）、Vulkan **75.1–84.0 fps**（GPU 10.61–11.98 ms），同样 >60。

### 16.3 1080p light-list 内存

| capacity | FPTL bytes | big-tile bytes | cluster bytes | bounds bytes | total buffers | light data |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 1,077,120 | 67,320 | 2,154,240 | 4,096 | 3,302,776 | 3,072 |
| 64 | 2,121,600 | 132,600 | 2,154,240 | 4,096 | 4,412,536 | 6,144 |
| 128 | 4,210,560 | 263,160 | 2,154,240 | 4,096 | 6,632,056 | 12,288 |

- runtime 日志已增加实际字节数：`light list buffers: capacity=... stride=... bounds=... B, big=... B, fptl=... B, cluster=... B, total=... B`。
- 64 相对 32 增加约 **1.11 MB**（1080p），远小于 texture/cluster 固定成本；1080p texture 宽度 7800，4K texture 宽度 15600，均低于 16383 pitch 上限。
- 4K texture/64 实测 `capacity=64/64`、`clamped=false`，FPTL 宽度 15600；默认 64 在 4K texture/CPU 路径不需要 clamp。

### 16.4 Contact shadows 在 128 capacity 的成本

| case | contact | D3D11 GPU ms | Vulkan GPU ms |
|---|---:|---:|---:|
| 32-light / 32 | off | 8.53 | 8.35 |
| 32-light / 32 | on | 10.40 | 11.31 |
| 128-light / 128 | off | 21.32 | 18.53 |
| 128-light / 128 | on | 25.95 | 23.77 |

- contact mask 仍只有 **8 bit**（`kMaxContactShadowLights=8`），但 shader 需要遍历 tile 的 effective capacity 才能找到有 mask 的 light；因此 128 capacity 下 contact 额外 **4.6–5.2 ms**，32 capacity 下约 **1.9–3.0 ms**。
- 128-light + contact 双后端 36–39 fps，明确低于 60；文档和 UI 不应把 128 宣传为“全功能 128 灯 contact”。
- fog 仍最多 8 个 punctual，shadow requests 仍最多 64；128 不等于 contact/fog/shadow 全部扩容。

### 16.5 默认容量决策

- **默认 64**：`config/pipeline.json`、`PipelineSettings::lightListCapacity`、`kLightListDefaultCapacity` 统一为 64；`--light-capacity=32` 保留安全回退，`128` 保持 opt-in/experimental。
- 依据：32-light 基线 **0 diff** 且无性能退化；64-light 双后端 >60 fps；4K texture 路径不 clamp；内存增加约 1.11 MB；HDRP 的 coarse cluster 上限为 128、fine cell 默认 8/上限 24，64 总容量处于合理区间。
- 不默认 128：128-light/128 双后端 <60 fps，1440p+ texture 路径会 clamp，contact 额外成本明显。
- 不为了容量牺牲 60 fps 门禁；如果后续场景在目标 GPU 上 64 明显退化，可继续用 32 作为默认回退并记录 64 opt-in。

### 16.6 验证

- 默认 64、32-light M7 基线：D3D11/Vulkan 对 `hbg_m78/default_*.png` **0 diff**。
- 默认 64、64-light 场景：D3D11/Vulkan 的 GPU buffer vs CPU reference **0 diff**，`light_list_truncated=0`。
- 4K texture/64：`capacity=64/64`、`clamped=false`。
- `ctest` 209 cases / 20,225 assertions + IDL check 通过。

---

## 17. M8.7 验收记录（2026-11-14）

### 17.1 方法

- Debug/RelWithDebInfo 双构建；RelWithDebInfo 用于截图/性能；GPU RTX 2060 SUPER；D3D11 + Vulkan。
- 核心视觉矩阵：1280×720、3 帧、`--no-vsync`、`config/pipeline.json` + `config/frame_settings.json`；64/128-light 场景分别覆盖 forward、deferred、fog、contact、debug `light-cluster` slice 2、transparent、transparent + `--no-light-cluster`。
- 每类场景三路径：GPU buffer（`--gpu-light-list-path=buffer`）、GPU texture（`--gpu-light-list-path=texture`）、CPU reference（`--no-gpu-light-list`）。
- M7 基线：`m5_shadow_stress.gltf`，1280×720，capacity=32，forward + contact，与 `hbg_m78/default_d3d.png`、`default_vk.png`、`default_tex_*.png` 对比。
- 边界场景：`m8_lights33_same.gltf`（33 个同位置灯）、`m8_lights127_dir.gltf`（128 punctual + 1 directional，共 129 候选）、`m8_lights64/128.gltf`。
- 读回统计：`--cluster-stats-readback`，buffer 直接 `bgfx::read`，texture 走 READ_BACK staging blit；CPU 路径输出同格式统计。
- HDRP 参考：`LightLoopDef.hlsl:232-262` 的 runtime count/index 读取、`lightlistbuild-clustered.compute:388,453,475-482` 的 `MAX_NR_COARSE_ENTRIES`/`iSpaceAvail` 上限、`GlobalLightLoopSettings.cs:207` 的 cluster cell 默认 8/上限 24。

### 17.2 视觉与数值结果

- **M7 32-light 基线**：D3D11/Vulkan × buffer/texture/CPU 共 10 组对比，全部 **0 diff**。
- **64/128 核心矩阵**：2 capacity × 7 场景 × 3 路径 × 2 后端 = 84 组 GPU/CPU 及 buffer/texture 对比，全部 **0 diff**，所有进程 exit=0。
- **1080p / 128**：`path=texture, capacity=128/128, stride=129, clamped=false`，FPTL 宽度 15480；buffer/texture/CPU 三路径在 D3D11/Vulkan 均 **0 diff**。
- **边界/读回**：
  - 33 个同位置灯，capacity 64/128：CPU、GPU buffer、GPU texture 的 `clusters=7680 active=1440 overflow_clusters=1440 overflow_lights=1440 max_overflow=1 max_lights=32` 完全一致；第 33 个灯只进 overflow，不越界。
  - 64-light/64：`overflow_lights=42240 max_overflow=32 max_lights=32`，CPU tile/big-tile 最大值均为 64；GPU buffer/texture 与 CPU 统计一致。
  - 128-light/128：`overflow_lights=130560 max_overflow=96 max_lights=32`，CPU tile/big-tile 最大值均为 128；GPU buffer/texture 与 CPU 统计一致。
  - 129 候选（128 punctual + 1 directional）：capacity 64 时 `truncated=65`、CPU tile 最大 63；capacity 128 时 `truncated=1`、CPU tile 最大 127；两种情况下 cluster 仍为 32/cell，overflow 统计 CPU/GPU 一致。
- **0.5/高分辨率**：
  - 1280×720 `--render-scale=0.5`、capacity 128：buffer/texture GPU 与 CPU **0 diff**。
  - 3840 宽度（4K 水平）/capacity 32/64：buffer/texture/CPU 全部 **0 diff**；texture/64 `capacity=64/64`、无 clamp。
  - 2560/3840 宽度、capacity 128 texture：按 16383 pitch 上限 clamp 到 64，texture GPU vs CPU（同 clamp）**0 diff**；buffer 保持 `capacity=128/128`。注意 buffer 128 与 CPU 64 的截图不同是预期行为，不是回归。
  - 桌面高度限制使 1440/4K 窗口 framebuffer 高度为 1141；FPTL/big-tile 宽度与 clamp 仅由宽度和 stride 决定，因此不影响本次容量结论。
- **Resize/资源重建**：
  - texture 路径 1280×720 → 2544×1141 → 1264×681：观察到 `light list texture capacity: 128 -> 64` 与 `64 -> 128`，双后端一致。
  - buffer 路径同一 resize：`light list buffers` 从 2,999,296 B（1280）重建为 9,324,064 B（2544×1141），再重建为 2,799,748 B（1264×681）；旧 buffer 先 destroy 再 create，无 `ERROR`/validation。
- **Debug/回退验证**：`--bgfx-debug` + texture/128 + readback 无 validation error；仅缺少 RenderDoc/Aftermath DLL 的既有警告。禁用 buffer（`--no-gpu-light-list-buffers`）时 128/1280 自动走 `path=texture`，D3D11/Vulkan 与显式 texture 路径 **0 diff**；`--no-gpu-light-list` 的 CPU reference 也一致。
- **Fog 边界诊断**：shader 仍以 `u_fog_params5.x` 将每采样 punctual light 限制为 8；`fog_lights` 诊断改为 `min(punctualCount, maxPunctualLights)`，capacity=128 时日志为 `fog_lights=8`，不再误报 128。
- 本轮自动矩阵共 130 组对比：**126 组 0 diff**；其余 4 组是 1440p/4K 的 buffer/128 vs CPU/64，属于 CPU texture-width clamp 的设计内差异，不是回归。

### 17.3 1080p 性能复核

- 方法：RelWithDebInfo、1920×1080、300 帧、`--no-vsync`、Uffizi、full post、forward、capacity=64/128；64 为 7 次，128 为 3 次。
- 64-light/64 forward：
  - D3D11：**58.9–66.5 fps**（avg 63.8、median 64.1），GPU timer **11.67–16.14 ms**（avg 13.31）。最低一次 CPU submit 38.2 ms，属于桌面/DWM 抖动；GPU timer 仍低于 16.67 ms。
  - Vulkan：**69.1–76.3 fps**（avg 73.2），GPU **11.26–11.85 ms**。
- 64-light/64 默认 deferred：
  - D3D11：**47.1–62.5 fps**（avg 57.9、median 61.7），GPU **12.14–18.27 ms**；7 次中 2 次受系统调度影响明显，deferred 路径在 D3D11 上接近 60 fps 门禁，不承诺稳定余量。
  - Vulkan：**73.0–75.2 fps**，GPU **12.41–13.17 ms**。
- 128-light/128 forward：
  - D3D11：**40.8–46.5 fps**（avg 43.0），GPU **20.54–20.59 ms**。
  - Vulkan：**50.5–51.1 fps**（avg 50.7），GPU **17.80–19.11 ms**。
- 结论：M8.6 的 forward 64-light >60 门禁仍成立；deferred D3D11 余量较小，保留 32 为安全回退，128 继续 experimental。

### 17.4 门禁

- `hbg_tests`：**209 cases / 20,225 assertions**，全部通过。
- `ctest --preset vs2022-debug`：`hbg_tests` + `hbg_idl_check` 100% 通过。
- `--gfx-test`：D3D11、Vulkan 均通过（draw=true, compute=true）。
- `git diff --check`：无 whitespace error（仅工作区 LF→CRLF 提示）。
- Debug/RelWithDebInfo 构建通过，shader 全量变体编译通过。

### 17.5 M8 结论与边界

- M8 出口达成：32/64/128 契约稳定；32-light M7 基线 0 diff；64/128 双后端三路径 0 diff；cluster 32/cell 边界、overflow、truncation、texture clamp、resize 资源重建均有数值/截图证据。
- 默认保持 **64**（HDRP coarse cluster 128 / fine cell 默认 8、上限 24；本项目固定 32/cell）；32 是安全回退；128 是 opt-in/experimental。
- 已知限制：128 texture 在 1440p+ 自动 clamp 到 64；buffer 128 没有同分辨率满容量 CPU 对照（CPU 路径受 texture width 限制）；contact 最多 8 盏、fog punctual 默认 8、shadow requests 最多 64；D3D11 deferred 64-light 性能接近 60 fps，需在目标场景复核。
- M9 可继续 layered cluster offset、16-bit packing、category/feature variants、light layers、cookies/IES、GPU shadow atlas；M8 不引入这些。
