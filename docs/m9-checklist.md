# M9 HDRP Light Data Path 2.0 规划

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M9.0–M9.6 核心实现完成**（2026-11-14）；M9.7 部分完成。
> 前置基线：M8.7 已完成，默认 `lightListCapacity=64`，32 为安全回退，128 为 opt-in；`supportGpuLightList=true`、`supportGpuLightListBuffers=true`。
> 原则：先读 HDRP 与 vendored bgfx 源码再实现；M9 关闭时保持 M8.7 的 126 组 0 diff；不把 M9 写成 M10 的 area light/材质变体大爆炸。

---

## 0. 结论摘要

M9 不是继续加灯数，而是把 M8 的 **32-bit 固定 row + 固定 32/cell cluster** 换成 HDRP 风格的 **packed list + layered cluster + category/feature 数据契约**，并在其上补 light layers 与 cookies/IES。

M9 的核心目标：

1. **16-bit packed FPTL/big-tile**：count 放低 16 bit，两个 light index 放一个 dword，FPTL/big-tile 内存约减半。
2. **Layered cluster offset**：每 category/cluster 存 `offset+count`，全局 index list 按需 append；移除固定 `count + 32 indices` 和 32/cell 上限。
3. **Category/feature flags**：每盏灯带 category/feature flags，FPTL/big-tile/cluster 按 category 分层；M9 先只启用 Punctual，Area/Env/Decal 只保留枚举和空层，真正 area lighting 留 M10。
4. **Light layers**：per-light 与 per-object 16-bit mask，forward/deferred 都执行 `IsMatchingLightLayer`。
5. **Cookies/IES**：spot 2D / point octahedral cookie、spot IES；关闭时 0 diff。
6. **Shadow scalability（stretch）**：128 requests/4096 atlas/CPU packing；HDRP 没有 GPU atlas packing，M9 不伪造“GPU shadow atlas”。

M9 **不包含**：

- 256/512 lights、D3D12、bindless、mesh shader、Hi-Z。
- 完整 HDRP 29 个 material feature variants；M9 只落 feature flags 数据与 debug。
- Area light 的 LTC 光照/area shadow；M9 只预留 `Area` category，M10 再做。
- Env/Decal 进入 light loop；当前 reflection probe/decal 路径不重写。
- GPU shadow atlas packing；HDRP 仍是 CPU/Burst request + dynamic atlas。
- 8K 默认支持；128 在 8K 仍会被 texture pitch clamp。

### 0.1 与 M8 的边界

- M8 已解决 **总容量** 32/64/128 与 producer/consumer 动态 stride；M9 解决 **编码、分层、过滤**。
- M8 的固定 cluster row 仍保留 32/cell overflow 诊断；M9 删除该结构，128 同 cell 不再 overflow。
- M8 的 64 默认、128 opt-in、32 回退不变；M9 不因新编码改变默认。
- M9.1/M9.2 先保持“单 Punctual 层”也能 0 diff；M9.3 再把 category 维度打开，避免一次改完所有变量。

### 0.2 实现记录（2026-11-14）

- M9.1：FPTL/big-tile 已改为 packed 16-bit；dwordStride=(capacity+2)/2，cap32/64/128 = 17/33/65；4K/cap128 FPTL 宽度 15600，不再被 16383 pitch clamp。ctest 213 cases/28,918 assertions 通过。
- M9.2：cluster 使用 offset = clusterIndex * clusterCapacity 的确定性布局，未使用 GPU atomic；normal 128，8K/128 自动降为 64；offset_mismatch=0，128 同 cell 无 overflow。
- M9.3：4 个 category 层、texel6 layer/category/feature、per-tile feature flags 已接入；Area/Env/Decal 仍为空层。
- M9.4：forward 用 per-object uniform；deferred 复用 GBuffer0.a（低 8 bit）与 GBuffer3.a（高 8 bit）保存 object mask，避免第 5 个 MRT 与额外 sampler；D3D11 maxTextureSamplers=16，没有空闲 unit。默认 mask=0xFFFF 时开关 0 diff；交叉 mask 在 forward/deferred 均有差异。
- M9.5：cookie/IES 打包进 light data 扩展 texel（每灯 8×8 cookie = texel 8–71，8 个 IES 样本 = texel 72–79，kGpuLightTexels=80），避免独立 atlas sampler；spot 2D、point octahedral（源图按 equirect 解码）、spot LM-63 IES、clamp/repeat、缺失/解析失败 warning 已实现。
- M9.6：`kMaxShadowRequests` 64→128，默认仍 64；atlas 支持 1024/2048/4096（默认 2048）。extended 模式（`maxRequests>64` 或 `atlasSize>2048`）对 punctual request group 按面积降序做稳定 shelf packing，原 64/2048 路径保持逐灯顺序；shadow context 高度 128。默认 64/2048 的 M8.7 基线截图 **0 diff**；128 requests/4096 atlas 在 D3D11/Vulkan 运行通过，单测验证 viewport 不重叠、point 6 face 的 `shadowIndex` 连续。1080p deferred（post off，Debug）64/2048 vs 128/4096：avg 35.8 vs 39.2 ms。cached punctual atlas 仍与 `atlasSize` 同尺寸；4096 时 3 张 D24S8 约 192 MiB，未做独立 2048 cache。stress 场景 `C:\Users\CHENFU~1\AppData\Local\Temp\codemaker\m96_stress.gltf`（122 spot + 1 point，128 requests）。
- M9.7：完整容量/分辨率矩阵与 ctest/IDL 通过；4K/cap128 texture 路径 **不 clamp**（FPTL 15600×288，big-tile 3900×72），buffer/texture 截图 0 diff，light-list 总内存约 37.3 MB；Vulkan cookie/light-layer 开/关与 128/4096 shadow stress 通过；128/4096 stress 在 4K texture 路径（FPTL 15600×288）D3D11/Vulkan 不 clamp，D3D11 buffer/texture **0 diff**。缺失 cookie/畸形 IES fallback 与禁用路径 0 diff；cap32/64/128 × 1080p/1440p/4K × D3D11/Vulkan × forward/deferred 的 buffer/texture/CPU 三路径全部 0 diff；透明场景 light layers on/off 在 forward/deferred 均有差异。1080p 300 full post 实测 55.2 fps（D3D11 cap64/2048）与 29.2 fps（cap128），**未达 >60 fps 门禁**；M9.7 仅剩性能项。

---

## 1. 当前基线与限制

### 1.1 当前 FPTL/big-tile

- `src/render/lighting/gpu_light_data.h`：`kMaxGpuLights=128`、`kLightListStrideMax=129`、`kLightListDefaultCapacity=64`；`GpuLightListData` 每 tile 是 `count + capacity` 个 `uint32`。
- `src/render/lighting/light_list_builder.cpp:212-404`：CPU 每 tile 写 `count`，随后写升序 light index；FPTL 16×16，big-tile 64×64。
- `shaders/common/light_data.sc:78-98`：`hbgLightListCount()` / `hbgLightListIndex()` 按 `stride` 读 `uint32` row。
- 现状内存：1080p/cap64 FPTL `120×68×65×4 ≈ 2.12 MB`；4K/cap128 FPTL 宽度 `240×129=30960`，超过 texture 16383 pitch，只能 clamp 64。

### 1.2 当前 cluster

- `gpu_light_data.h`：`kLightClusterMaxLights=32`、`kLightClusterStride=33`。
- `light_list_builder.cpp:300-404`：每个 `(64×64 tile, 32 slices)` 固定 `count + 32 indices`；第 33 个起只累计 overflow。
- `shaders/common/light_data.sc:130-162`：`HBG_LIGHT_CLUSTER_STRIDE=33`、count 低 8 bit + overflow 高 8 bit。
- 结果：128 盏灯同 cell 时大量 overflow；M9.2 要改为 layered offset + 全局 index list。

### 1.3 当前 light data

- `GpuLightData` 是 6 texels；`texel3.w` 存 type/shadow flags，`texel5.y` 存 penumbraTint，`texel5.z/w` 未用满。
- 没有 light layer mask、category、feature flags、cookie scale/offset；M9 base 扩到 8 texels，M9.5 cookie/IES 再扩到 80 texels。
- 当前只有 directional + point/spot，没有 area；M9 的 category 先以 Punctual 为唯一 active 层。

---

## 2. HDRP 源码结论

### 2.1 FPTL/big-tile 的 16-bit packing

HDRP 把 tile 看成 dword 数组，**count 在第一个 dword 的低 16 bit**，light index 两两塞进一个 dword：

- `com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:129-150`：
  - `s_LightDwordPerFptlTile = (FPTLMaxLightCount + 1) / 2`；
  - big-tile 上限为 `clamp((FPTLMax+1)*8, 512, 1024)`；
  - cluster count/offset 的 bit 数由此生成。
- `ShaderConfig.cs.hlsl:25-32`（High 配置）：
  - `LIGHT_DWORD_PER_FPTL_TILE (32)`；
  - `MAX_NR_BIG_TILE_LIGHTS_PLUS_ONE (512)`；
  - `LIGHT_CLUSTER_PACKING_COUNT_BITS (6)` / `OFFSET_BITS (26)`。
- `LightLoopDef.hlsl:232-263`：
  - `GetCountAndStartTile()` 读 `g_vLightListTile[...] & 0xffff`；
  - `FetchIndex()` 用 `(lightOffset+1)>>1` 与 16-bit shift 取 index。
- `LightLoopDef.hlsl:326-342`：big-tile 同样 `count & 0xffff` + 16-bit index。
- `lightlistbuild.compute:409-422`：按 category 计算 `nrDWords = ((nrLightsFinalClamped + 1) + 1) >> 1`，写 `(uLow & 0xffff) | (uHigh << 16)`。
- `lightlistbuild-bigtile.compute:200-210`：big-tile 也按 halfword 写 count/index。

对本项目：FPTL 行宽从 `capacity+1` 个 uint32 改为 `dwordStride = (capacity+2)/2`；cap64 = 33 dwords，cap128 = 65 dwords。4K/cap128 的 FPTL texture 宽度 `240×65=15600`，**可以回到 16383 pitch 内**，不再必须 clamp 64。

### 2.2 Layered cluster offset

HDRP 的 cluster 不是每 cell 固定 row，而是：

- `ClusteredUtils.hlsl:106-111`：`GenerateLayeredOffsetBufferIndex(category, tile, cluster, ...)` 按 category × cluster × tile 计算 offset buffer 下标。
- `LightLoopDef.hlsl:286-300`：`UnpackClusterLayeredOffset()` 拆 offset/count；`GetCountAndStartCluster()` 从 offset buffer 读起点，再在 `g_vLightListCluster[start+i]` 取 32-bit index。
- `lightlistbuild-clustered.compute:76-78`：`PackClusterLayeredOffset(offset,count)`。
- `lightlistbuild-clustered.compute:440-453`：每个 cluster 计算 `iSpaceAvail = min(iSum, MAX_NR_COARSE_ENTRIES)`，通过全局 atomic 分配一段 index list。
- `lightlistbuild-clustered.compute:482-502`：按 category 写 `g_vLayeredLightList`，并把 `start+localOffs / count` 写入 `g_LayeredOffset`。
- `HDRenderPipeline.LightLoop.cs:654-656`：offset buffer 为 `LightCategory.Count * clusters * tiles`；index list 为 `NumLightIndicesPerClusteredTile() * clusterTiles`。
- `LightLoop.cs:871-874`：HDRP 的 per-cluster-tile index 预算是 `(FPTLMax+1) * (1 << (log2Clusters+1))`。

HDRP High 配置的 count 只有 6 bit（63），但 coarse 分配可到 128；本项目为避免 128 截断，采用 **8-bit count + 24-bit offset**（count 0–255，offset 0–16,777,215），足够 128 lights 与 4K 的 index buffer。

### 2.3 Category 与 feature flags

- `LightLoop.cs:66-83`：`LightCategory = { Punctual, Area, Env, Decal, Count }`。
- `LightLoop.cs:76-87`：`LightFeatureFlags`：`Punctual=1<<12`、`Area=1<<13`、`Directional=1<<14`、`Env=1<<15`、`Sky=1<<16`、`SSRefraction=1<<17`、`SSReflection=1<<18`。
- `LightLoop.cs:120-122`：`s_LightFeatureMaskFlags=0xFFF000`，opaque/transparent 各自去掉一个 bit。
- `LightLoop.cs:215-234`：category 的 bit mask。
- `lightlistbuild.compute:345-386`：每个 tile 用 `ldsCategoryListCount` 统计 category，`InterlockedOr(ldsFeatureFlags, ...)` 汇总 feature flags。
- `lightlistbuild.compute:409-422`：每个 category 单独限制并写 FPTL row；`offs` 按 `nrTilesX*nrTilesY` 前进。
- `LightLoopDef.hlsl:226-246`：FPTL 的 tile offset 先按 category 分层。
- `HDRenderPipeline.LightLoop.cs:621-657`：`LightCategory.Count * s_LightDwordPerFptlTile * nrTiles` 的 buffer 规模。

M9 只落 **category 维度、per-tile feature flags、per-category list 布局**；不实现 29 个 material variants。Area/Env/Decal 的层先保留为空，避免 M10 再改 stride。

### 2.4 Light layers

- `CommonLighting.hlsl:550-553`：`IsMatchingLightLayer(lightLayers, renderingLayers)` 就是 `(lightLayers & renderingLayers) != 0`。
- `LightLoop.hlsl:305`、`:649`：punctual 和 directional 在累加前检查 light layer。
- `LightLoop.hlsl:322,591`：env/probe 也按 layer 过滤；M9 先不改 env 路径。
- `Lit.hlsl:883-884`：deferred 从 `_RenderingLayersTexture` 解 16-bit mask；关闭时用 `RENDERING_LAYERS_MASK`。
- `ShaderVariablesFunctions.hlsl:8-15`：mesh mask 只取 16 bit；`UnpackMeshRenderingLayerMask()` 从两个 0–255 通道恢复。
- `BuiltinUtilities.hlsl:59`：forward 从 `unity_RenderingLayer` 取 mask。
- `HDRenderPipeline.Prepass.cs:295-302`：只有 DecalLayers/LightLayers/RenderingLayerMaskBuffer 才创建 rendering layers buffer。
- `HDRenderPipeline.Prepass.cs:776-782`：GBuffer 把 rendering layers 作为额外 MRT attachment。
- `HDAdditionalLightData.cs:3350-3357`：`SetLightLayer` 设置 per-light mask。

M9 需要 per-object mask（`RenderObject`）与 per-light mask（`Light`）；forward 用 per-object uniform，deferred 复用 GBuffer0.a/GBuffer3.a 存 16-bit（避免额外 MRT 与 sampler）。`supportLightLayers` 默认仍为 false，开启后才有行为。

### 2.5 Cookies/IES

- `LightCookieManager.cs:47-62`：HDRP 用 `PowerOfTwoTextureAtlas`，默认 `cookieAtlasSize=2048`、`cookieFormat=R11G11B10`、`cookieAtlasLastValidMip=0`。
- `LightCookieManager.cs:242-272`：spot/directional 2D cookie 走 `Fetch2DCookie`，返回 atlas scale/offset。
- `LightCookieManager.cs:338-366`：point cookie 走 cube/octahedral atlas 预留。
- `CookieSampling.hlsl:29-55`：
  - `SampleCookie2D(coord, scaleOffset, lod)` 做 padding remap + atlas scale/offset；
  - `SamplePointCookie(lightToSample, scaleOffset)` 用 octahedral 编码到 2D。
- `LightDefinition.cs:70-72`、`:165-174`：`cookieMode`（None/Clamp/Repeat）与 `cookieScaleOffset` 是 LightData 的一部分。
- `LightEvaluation.hlsl:380-435`：point 用 `SamplePointCookie`，spot/area 用 `SampleCookie2D`，并处理 IES cutoff。
- `GlobalLightLoopSettings.cs:180-226`：cookie atlas 默认 2048、格式、last mip。

M9 cookie 先覆盖 spot 2D + point octahedral；IES 先覆盖 spot 2D LM-63；directional cookie 与 point IES 可留 M10。

### 2.6 Shadow request/atlas

- HDRP 默认 `maxShadowRequests=128`、`shadowAtlasResolution=4096`（`HDShadowManager.cs:403-407`）。
- `HDShadowManager.cs:1414-1452`：punctual/area/directional 各自 dynamic/cached atlas，逐 request 渲染。
- `HDShadowCullingUtils.cs:28-132`：Burst job 计算 shadow culling infos、分桶和 split；不是“GPU 生成 atlas”。

因此 M9 的 shadow 项只能叫 **request/atlas scalability**：提高 request 上限、扩大 atlas、CPU/Burst 分桶；**不把 GPU atlas packing 写成 HDRP 对齐项**。如果要做 GPU atlas，另立 M10 研究项。

### 2.7 与 M9 的映射

| 能力 | HDRP | M9 本项目 | 说明 |
|---|---|---|---|
| FPTL 编码 | count 低 16 + 2 index/dword（`LightLoopDef.hlsl:232-263`） | 同样 packed，动态 `dwordStride` | cap64 33 dword，cap128 65 dword |
| big-tile 编码 | count 低 16 + 2 index/dword（`LightLoopDef.hlsl:326-342`） | 同样 packed | 4K/128 宽度 3900 |
| cluster | layered offset + global index（`LightLoopDef.hlsl:286-300`） | offset/count + global index（确定性 offset） | 8-bit count/24-bit offset |
| cluster 上限 | 128 coarse/cluster | capacity 32/64/128，128 不再固定 32/cell | 不再 magenta overflow |
| category | 4 类（`LightLoop.cs:66-83`） | 4 类布局，M9 active=Punctual | Area/Env/Decal 空层 |
| feature flags | 29 variants + tile flags | tile flags 数据 + debug，不扩 29 variants | 变体留 M10 |
| light layers | 16-bit mask + GBuffer layer texture | 16-bit mask；deferred 用 GBuffer alpha 通道 | forward uniform；deferred 4RT |
| cookies | 2D atlas + octahedral point | light-data texel 打包（8×8 spot/octa） | IES spot first；无独立 atlas sampler |
| shadow | 128 requests/4096/CPU Burst | stretch：128/4096/CPU pack | 不做 GPU atlas |

---

## 3. M9 任务拆分

| 任务 | 产出 | 状态 |
|---|---|---|
| **M9.0 契约/基线** | 常量、布局、HDRP 对照、M8.7 基线截图、CLI/JSON 方案 | `[x]` 完成（2026-11-14） |
| **M9.1 16-bit packed FPTL/big-tile** | count+16-bit index、动态 dwordStride、CPU/GPU/texture 解码 | `[x]` 完成（2026-11-14） |
| **M9.2 Layered cluster** | offset/count buffer、global index、128/cell、无固定 overflow | `[x]` 完成；采用确定性 per-cluster offset，无 atomic（2026-11-14） |
| **M9.3 Category/feature flags** | 4 类布局、per-category list、tile feature flags；Area/Env/Decal 空层 | `[x]` 完成（2026-11-14） |
| **M9.4 Light layers** | per-light/per-object mask、forward uniform、deferred GBuffer alpha 通道 | `[x]` 完成；deferred 复用 GBuffer0.a/GBuffer3.a，未分配第 5 个 MRT（2026-11-14） |
| **M9.5 Cookies/IES** | spot/point cookie、spot IES、fallback | `[x]` 完成；cookie 数据打包进 light data 扩展 texel，未使用独立 atlas sampler（2026-11-14） |
| **M9.6 Shadow scalability（stretch）** | 128 requests、4096 atlas、CPU packing | `[x]` 完成；默认仍 64/2048，extended 模式做 size-sorted shelf packing，context 128（2026-11-14） |
| **M9.7 M9 验收** | 双后端矩阵、性能、4K/内存、测试/IDL/docs | `[~]` 部分完成：ctest/IDL、fallback、cap32/64/128 × 1080p/1440p/4K × 双后端 × 三路径矩阵、透明 light layers 与 128/4096 stress 通过；1080p 300 full post 55.2/29.2 fps **未达 >60 门禁** |

依赖：M9.0 → M9.1/M9.2（可并行，但都先锁编码契约）→ M9.3 → M9.4/M9.5（可并行）→ M9.6（opt-in）→ M9.7。

### 3.1 M9.0 契约与基线

**产出**

- `kLightCategoryCount=4`、`LightCategory`、`LightFeatureFlags`、`kRenderingLayersMask=0xFFFF`。
- `packedDwordStride(capacity)`、FPTL/big-tile 布局函数、cluster offset/count 布局函数。
- `GpuLightData` 8 base texels（texel6 = layer/category/feature；texel7 = cookie mode/type/IES），M9.5 再追加 8×8 cookie + 8 IES = 80 texels。
- sidecar 字段草案：`lightLayerMask`、`cookie`、`cookieMode`、`ies`；object `lightLayerMask`。
- M8.7 基线：32/64/128、1080/4K、forward/deferred、buffer/texture/CPU；存 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m9\baseline`。
- CLI/JSON 草案：`--light-layers`、`--no-light-layers`、`--cookies`、`--no-cookies`、`--ies`。

**验收**

- 不改行为；M8.7 126 组仍 0 diff。
- 单元测试覆盖布局公式、半字 roundtrip、offset/count 位宽。
- HDRP 参考行号写入本文档；M9.0 不写渲染代码。

### 3.2 M9.1 16-bit packed FPTL/big-tile

**产出**

- FPTL dword 布局：
  - `dword0.low16 = count`；
  - `dword0.high16 = index0`；
  - `dword k.low16 = index(2k-1)`，`dword k.high16 = index(2k)`；
  - `dwordStride = (capacity+2)/2`。
- big-tile 同样布局；每 category 独立 row（M9.3 打开 category 维度，M9.1 先单层）。
- CPU `LightListBuilder` 写 packed；提供 `tilePackedDword`/`tileLightCount`/`tileLightIndex` 解码 API。
- GPU producer `light_big_tile_buffer.cs.sc`、`light_fptl_buffer.cs.sc` 写 packed。
- consumer `light_data.sc` 的 `hbgLightListCount`/`hbgLightListIndex` 读 packed；texture/buffer 两分支一致。
- readback/debug 解码 packed；texture width 改为 `ceil(W/16)*dwordStride`。
- `textureCapacityFor()` 按 packed 重量重新计算：4K/cap128 应返回 128；8K 仍 clamp。

**验收**

- 32/64/128 × 1080/1440/4K × buffer/texture/CPU × forward/deferred：**0 diff**。
- 33/64/128 灯边界：count/index 解码与 M8.7 完全一致；128 灯 index 不截断。
- 4K/cap128 texture 宽度 15600，不 clamp；big-tile 宽度 3900。
- 内存记录：1080/cap64 FPTL `≈1.08 MB`（单层）/`≈4.31 MB`（4 类）；4K/cap128 `≈33.7 MB`（4 类）。
- 所有已存在 consumer（fog/contact/debug/transparent）走 packed 解码；无 32 硬编码。

**风险**

- 16-bit index 上限 65535；当前 `kMaxGpuLights=128` 安全，但要在 static_assert 中锁死。
- 最后一个 index 的 high half 可能是 0；consumer 必须按 count 截断，不能把 0 当合法 index 读取。
- texture 宽度/高度变化会触发 resize 重建；保留 M8.7 的 capacity 日志与 buffer 重建日志。

### 3.3 M9.2 Layered cluster

**产出**

- `GpuLightClusterData` 从固定 `count + indices` 改为：
  - `offsets`：`categoryCount * clusterTileCount * kClusterSliceCount` 个 uint32，低 24 bit offset、高 8 bit count；
  - `indices`：`clusterTileCount * kClusterSliceCount * capacity` 个 uint32 的全局 index list；
  - **实现选择**：不分配 atomic buffer；`offset = clusterIndex * clusterCapacity`，`clusterCapacity` normal=128、8K/128=64。
- `light_cluster_buffer.cs.sc`：
  - 每 workgroup 对应一个 `(64×64 tile, 32 slices)`；先统计每 slice 的交集 light；
  - 每个 cluster 使用确定性 offset，不依赖全局 append；
  - 按 coarse list 升序写 index，保证同一 cluster 内顺序稳定；
  - 写 offset/count；count 超过 255 或 allocation 用尽时记录 truncated，不写越界。
- consumer：
  - `light_loop.sc` 从 offset buffer 取 count/start，再读 global index；
  - fog/contact/debug 同步；
  - texture 路径增加 cluster offsets/indices 的 USAMPLER2D；buffer 路径用 StructuredBuffer；绑定 stage 8/9，避开现有 t5/t6/t7。
- CPU reference：
  - 先算每 cluster 的 count，再 prefix-sum 得到确定性 offset；
  - indices 按 light index 升序；
  - 暴露 `clusterOffset()`/`clusterLightCount()`/`clusterLightIndex()`；不再有 `clusterOverflowCount()` 的固定 32 语义。
- debug：`--debug-view=light-cluster` 按 offset/count 显示；`truncated` 单独诊断，不再用品红 overflow 表示 33+。

**内存**

- 1080p：cluster tiles `30×17=510`；
  - offsets（4 类）：`510×32×4×4 = 261,120 B`；
  - indices cap64：`510×32×64×4 = 4,177,920 B`；
  - indices cap128：`8,355,840 B`。
- 4K：cluster tiles `60×34=2040`；
  - indices cap64：`16,711,680 B`；
  - indices cap128：`33,423,360 B`。
- 4K/cap128 为 opt-in，必须记录峰值；若不可接受，4K 下建议 cap64。

**验收**

- 33/64/128 灯、同 cell 全重叠：cap128 不再 overflow，`max_lights=128`；129 候选 cap128 只截断 1。
- CPU/GPU 的 count 与 index 集合一致；offset 数值确定，且必须满足：
  - 每个 offset/count 在 buffer 内；
  - 同一 cluster 的 index 升序；
  - 不同 cluster 的区间不重叠。
- cluster debug slice `-2/0/5/15/31`、forward/deferred/transparent/fog/contact：0 diff。
- texture 与 buffer 路径一致；buffer 不可用时 texture 回退 0 diff。
- resize 1280→2544→1264：offset/index 重建、无越界、无 validation error。

**风险**

- 不使用 GPU atomic；offset 由 clusterIndex 计算，CPU/GPU 可直接比较。
- 4K/cap128 index buffer 33 MB；需要 diagnostics 内存字段与 4K cap64 回退。
- texture 路径需要两次 readback（offset/index）；测试不要把 FPTL 的 `s_lightList` 与 cluster 绑定混用。

### 3.4 M9.3 Category/feature flags

**产出**

- `LightCategory { Punctual, Area, Env, Decal, Count }`、`LightFeatureFlags`（Punctual/Area/Directional/Env/Sky/SSR/SSGI）。
- `GpuLightData` texel6：`lightLayers`、`category`、`featureFlags`、unused。
- FPTL/big-tile/cluster 增加 category 维度；texture 高度 = `tileCountY * kLightCategoryCount`，base row 按 category 偏移。
- `tileFeatureFlags` R32_UINT texture：每 tile OR 所有 light feature flags + base flags；M9 只用于 debug/选择，不扩 29 variants。
- CPU `LightListBuilder` 按 category 分桶；M9 active=Punctual，Area/Env/Decal 为空层。
- light builder 写 `category=Punctual`、`featureFlags=Punctual`；directional 仍走独立 primary directional 路径。

**验收**

- M8.7 所有场景 0 diff；category 维度不改变 Punctual 的升序与 lighting。
- 单元测试：4 类布局、空层 count=0、feature flags OR、tile flags。
- 4K/cap128 texture 高度 `135*4=540`，宽度 15600；确认 texture/pitch 限制。
- Debug 显示每 tile category count / feature flags；不引入 29 variants。

**边界**

- Area/Env/Decal 只保留枚举/空层；M10 再做 area LTC、area shadow、env/decal light loop。
- 不改变 reflection probe/decal 现有独立路径。

### 3.5 M9.4 Light layers

**产出**

- `RenderObject.lightLayerMask`（uint16，默认 0xFFFF）；`Light.lightLayerMask`（uint16，默认 0xFFFF）。
- sidecar：
  - object：`"lightLayerMask": 65535`；
  - light：`"lightLayerMask": 65535`；
  - 缺省不改变现有场景。
- forward：per-object uniform `u_objectLightLayerMask`；directional/punctual 累加前 `IsMatchingLightLayer`；transparent 同样。
- deferred：**实际实现未使用第 5 个 GBuffer**；复用 GBuffer0.a（低 8-bit）和 GBuffer3.a（高 8-bit）存 object mask，`hbgDecodeGBufferSurface()` 读回。原因：D3D11 PS 只有 16 个 texture unit，现有 GBuffer/深度/light data/shadow/environment/probe 已占满。`supportLightLayers` 关闭时 GBuffer shader 使用普通 4-output 变体。
- 不增加 MRT attachment，因此无需提高 `maxFBAttachments`；forward/deferred 均可开启。
- FrameSettings `LightLayers` 与 pipeline `supportLightLayers` 接通；默认 false。

**验收**

- `supportLightLayers=false`：M8.7 126 组 0 diff。
- 两盏 point + 两个 object，mask 交叉：只匹配的灯影响对应 object；directional 同理；forward/deferred 单后端一致。
- 全 0xFFFF 默认：开启前后 0 diff（无 layer 限制）。
- 透明 object：uniform mask 正确；无 GBuffer 路径。
- 第 5 个 MRT 不可用不再是阻塞项；deferred alpha 通道路径不依赖额外 MRT。
- Debug：可选显示 mask 条纹；至少日志输出 layer mask。

**风险**

- D3D11 texture unit 上限 16；M9.4 复用现有 GBuffer alpha 通道，不新增 sampler/MRT。
- GBuffer0.a/GBuffer3.a 在 deferred 路径不再保存 surface alpha/emissive alpha；deferred lighting 不使用这两个 alpha。
- 不能把 light layer 和 decal layer 共用同一通道；M9 只做 light layer。

### 3.6 M9.5 Cookies/IES

**产出**

- sidecar `lights[].cookie`（spot 2D / point octahedral）、`cookieMode`（none/clamp/repeat）、`ies`（LM-63）。
- **实现偏离规划**：不创建 2048 atlas 和独立 sampler。D3D11 只有 16 个 texture unit，已全部占用；cookie 数据打包进 light data 的扩展 texel：
  - `kGpuLightTexels=80`；texel0–7 保持原语义；
  - texel8–71：每灯 8×8 cookie，单通道亮度；
  - texel72–79：每灯 8 个 IES 样本；
  - texel7：x=cookieMode、y=cookieType、z=hasIes。
- spot：按 spot forward/right/up 投影到 cookie UV；point：octahedral 编码方向；源图按 equirect 解码后重采样到 8×8。
- IES：LM-63 TILT=NONE 解析，按 0–90° 采样 8 点，spot 乘到 attenuation；point IES 留 M10。
- 无 cookie/解析失败：warning，cookieMode 回 None（等价白纹理）。
- **未实现**：`--cookies/--no-cookies`、`--ies` CLI 与 pipeline support flag；当前由 sidecar 是否提供 cookie/ies 决定。

**验收**

- cookie off：无 sidecar cookie 时 0 diff。
- spot 2x2 checker cookie：D3D11 视觉差异通过；clamp/repeat 代码路径存在，但当前 spot 投影 UV 在锥内，视觉上通常无差异。
- point octahedral cookie：8×8 采样可见差异；未做 CPU reference 数值对照。
- 缺失/无效：warning，cookieMode=None，无越界。
- 无 mip/padding；8×8 采样为块状，HDR/高分辨率留 M10。
- 性能：未记录 cookie 增量；场景可运行。

**风险**

- 8×8 亮度采样精度有限；spot 边缘/point 方向会出现块状。
- 未做 HDR cookie、mip、padding；高分辨率需求留 M10。
- sidecar 资源路径解析与纹理上传；缺失纹理必须 fallback。
- IES 解析器容错（角度/单位/归一化）。

### 3.7 M9.6 Shadow scalability（stretch，已实现 opt-in）

**定位**

HDRP 没有 GPU shadow atlas packing；它是 Burst CPU culling + dynamic atlas。M9 不把 GPU atlas 作为目标，只做 request/atlas 规模：

- `kMaxShadowRequests` 64→128（完成；`kDefaultShadowRequests=64` 保持默认）；
- atlas 2048→4096（完成；1024/2048/4096 可配置，默认 2048）；
- per-request resolution、shelf/bucket packing、CPU request 排序（完成；extended 模式按 group 面积降序稳定排序后 shelf pack，失败回退原布局）；
- GPU shadow culling/indirect 保持 M7 的 opt-in，不改默认（完成）；
- cached shadow atlas 不扩，除非时间允许（**未做**；当前 cached 与 `atlasSize` 同尺寸）。

**验收**

- 64 request 基线 0 diff（完成：m8_lights128 4K D3D11 默认路径旧/新 **0 diff**）；
- 128 request/4096 atlas 场景不崩、无越界；per-request viewport 正确（完成：单测 128 requests 两两不重叠，D3D11/Vulkan stress 运行通过）；
- 1080p 性能记录；默认仍 64/2048，128/4096 opt-in（完成：Debug deferred/post-off 64/2048 avg 35.8 ms，128/4096 avg 39.2 ms）；
- 不做 GPU atlas、不做 async shadow；文档明确（完成）。

### 3.8 M9.7 M9 验收

**矩阵**

- M9 features off：M8.7 32/64/128、forward/deferred、buffer/texture/CPU 全部 0 diff。
- M9.1：packed list 解码、4K/128 不 clamp、0 diff。
- M9.2：128 同 cell 不 overflow、slice debug、0 diff。
- M9.3：category/feature flags、空层、0 diff。
- M9.4：light layers 开/关、forward/deferred、透明；`m8_transparent128` 全透明场景 light layers on/off 在两路径均产生全屏差异，forward/deferred 的 layers-on 截图 0 diff。
- M9.5：spot/point cookie、IES、缺失/解析失败 fallback、off 0 diff。
- M9.6：64/128 requests、4096 atlas、CPU sorted shelf packing；64 baseline 0 diff，128/4096 运行通过。

**M9.7 追加记录（2026-11-14）**

- fallback：缺失 cookie 文件与畸形 IES 均打印 warning、禁用 cookie/IES，截图与无 cookie/IES 路径 **0 diff**。
- 1080p 300 帧 full post（RelWithDebInfo，`config/postprocess_quality.json`）：
  - D3D11 forward cap64/2048：avg 18.1 ms / 55.2 fps；Vulkan 20.6 ms / 48.7 fps（CPU-bound）。
  - D3D11 forward cap128/2048（128 灯场景）：avg 34.3 ms / 29.2 fps。
  - **未达到 cap64 默认 >60 fps 门禁**；M8.6 同场景曾为 64.9 fps（Debug），需后续 profile M9 增量。
- 4K/cap128：light-list 总内存约 37.3 MB；4096 shadow atlas 3×D24S8 约 192 MiB；建议 4K 默认 cap64，cap128/4096 保持 opt-in。
- 完整容量/分辨率矩阵已补：cap32/64/128 × 1080p/1440p/4K × D3D11/Vulkan × forward/deferred × buffer/texture/CPU，共 36 组、每组 buffer/texture/CPU **全部 0 diff**（cap32 用 `m8_lights64_perf` 截断，64/128 用对应场景）。
- 透明 light layers 已补：`m8_transparent128`（全 BLEND + floor layer1/redWall layer2）在 D3D11/Vulkan forward/deferred 下 layers on/off 均全屏差异；各后端 forward-on 与 deferred-on 0 diff。
- M9.7 矩阵已全部通过；仅剩 1080p 300 帧 full post 的 >60 fps 性能门禁未满足，M9.7 维持 `[~]`。

**测试/门禁**

- `hbg_tests` 新增：
  - pack/unpack halfword roundtrip；
  - `dwordStride` 与每 capacity；
  - cluster offset/count 边界；
  - 128 同 cell；
  - category 分桶/feature flags；
  - light layer match；
  - cookie 缺失 fallback、IES parser。
- `ctest --preset vs2022-debug --output-on-failure`；
- `hbg_idl_check`；
- D3D11/Vulkan `--gfx-test`；
- `git diff --check`。

**性能/内存**

- 1080p 300 帧 full post：
  - cap64 默认 >60 fps（M8.7 门禁保持）；
  - cap128 记录，不承诺默认；
  - light layers/cookies 各自记录增量。
- 4K：
  - cap64 可运行；
  - cap128 texture 不 clamp（FPTL 宽度 15600），记录 cluster index 33.4 MB 峰值；
  - 若不可接受，4K 默认 cap64，128 保持 opt-in。
- resize/资源释放：light data/list/cluster offset/index 全部重建与销毁。

**文档**

- 更新 `README.md`、`AGENTS.md`、`docs/implementation-plan.md`。
- 新增 M9 验收 section；记录 HDRP 参考 `file:line`。
- 明确 M9.6 是否实现；未实现要写“未实现”，不能写成已完成。

---

## 4. 数据契约草案

### 4.1 常量

```text
kMaxGpuLights              = 128
kLightCategoryCount        = 4       // Punctual, Area, Env, Decal
kRenderingLayersMask       = 0xFFFF
kClusterSliceCount         = 32
kClusterCountBits          = 8       // M9 choice; HDRP High uses 6
kClusterOffsetBits         = 24
kClusterOffsetMask         = (1<<24)-1
kClusterCountMask          = 0xFF
```

### 4.2 GpuLightData texel 扩展

| texel | 现有 | M9 |
|---|---|---|
| 0 | position.xyz, range | 不变 |
| 1 | color.rgb, intensity | 不变 |
| 2 | direction.xyz, cosOuter | 不变 |
| 3 | shadowIndex, cosInner, contactMask, type/shadow flags | 不变 |
| 4 | shadowTint.rgb, shadowDimmer | 不变 |
| 5 | shadowFadeDistance, penumbraTint, flags2, unused | 不变 |
| 6 | 不存在 | `lightLayers`, `category`, `featureFlags`, unused |
| 7 | 不存在 | `cookieMode`, `cookieType`, `hasIes`, unused |
| 8–71 | 不存在 | 8×8 cookie 亮度样本 |
| 72–79 | 不存在 | 8 个 IES 样本 |

`kGpuLightTexels=80`；lightData texture 高度仍是 capacity，宽度 80；128 lights 时约 164 KB，不是瓶颈。

### 4.3 FPTL/big-tile 布局

```text
dwordStride(capacity) = (capacity + 2) / 2

dword0: count(low16) | index0(high16)
dword1: index1(low16) | index2(high16)
dword2: index3(low16) | index4(high16)
...
```

- count 是当前 tile/category 的 index 数；consumer 必须 `i < count`。
- 最后一个 index 的 high half 可能为 0；不得把 0 当有效 index 无条件读。
- texture 路径：`width = tileCountX*dwordStride`，`height = tileCountY*categoryCount`。
- buffer 路径：row base `((category*tileCountY + tileY)*tileCountX + tileX)*dwordStride`。

### 4.4 Cluster 布局

```text
offsetBufferIndex(category, tileX, tileY, slice) =
    ((category*sliceCount + slice)*tileCountY + tileY)*tileCountX + tileX

offsetWord = (offset & 0x00FFFFFF) | (min(count,255) << 24)
indexBuffer[offset + i] = lightIndex
```

- `indices` 为全局 append list；每 cluster 的 index 升序。
- `offset` 为 24-bit；4K/cap128 的 index 总数约 8.36M < 16.7M，安全。
- 若 offset/count 超出，记录 `clusterTruncated`，不写越界；M9 不再使用固定 32/cell 的 overflow 打包。

### 4.5 Light layer GBuffer

```text
lightLayerMask = 16-bit; default 0xFFFF
match = (lightMask & objectMask) != 0
deferred GBuffer0.a = low8 / 255
deferred GBuffer3.a = high8 / 255
```

- `supportLightLayers=false`：GBuffer shader 使用普通 4-output 变体，mask 用 0xFFFF。
- forward：per-object uniform；deferred：GBuffer0.a/GBuffer3.a；透明：uniform。
- 不增加 MRT 或 sampler；D3D11 texture unit 16 的限制不影响。

### 4.6 Cookie/IES

```text
texel7.x = cookieMode: 0=none, 1=clamp, 2=repeat
texel7.y = cookieType: 0=none, 1=spot2D, 2=pointOctahedral
texel7.z = hasIes
texel8..71 = 8x8 cookie samples (r channel)
texel72..79 = 8 IES samples (r channel)
```

- spot：按 forward/right/up 投影到 cookie UV；point：octahedral 编码方向。
- point 源图按 equirect 解码后重采样到 8×8 octahedral 网格；不是立方体贴图。
- IES：LM-63 TILT=NONE，0–90° 取 8 点；spot 乘到 attenuation；point IES 未实现。
- 缺失/解析失败：warning，cookieMode=None（等价白纹理）；无越界。

---

## 5. 验收矩阵草案

### 5.1 M9.1

| 维度 | 值 |
|---|---|
| capacity | 32/64/128 |
| 分辨率 | 1280×720、1920×1080、2560×1440、3840×2160 |
| 路径 | buffer/texture/CPU |
| 模式 | forward/deferred |
| 灯数 | 8/16/32/33/64/128 |
| 后端 | D3D11/Vulkan |

判定：解码 count/index 与 M8.7 CPU 一致；截图 0 diff；4K/128 texture 不 clamp；内存记录。

### 5.2 M9.2

| 维度 | 值 |
|---|---|
| 场景 | 33/64/128 同位置灯、129 候选、cluster stress |
| 路径 | buffer/texture/CPU |
| debug | slice -2/0/5/15/31 |
| 模式 | forward/deferred/transparent/fog/contact/no-cluster |
| 后端 | D3D11/Vulkan |

判定：cap128 同 cell 无 overflow；count/index 集合一致；offset 区间合法；截图 0 diff。

### 5.3 M9.3/M9.4/M9.5

| 功能 | 判定 |
|---|---|
| category | 4 类布局、空层、tile flags；Punctual 0 diff |
| light layers | off 0 diff；on 只匹配灯影响；forward/deferred；透明 |
| cookies | off 0 diff；spot/point atlas；clamp/repeat；fallback |
| IES | spot profile；无 profile 白；边界容错 |

### 5.4 M9.6

| 维度 | 值 |
|---|---|
| requests | 64/128 |
| atlas | 2048/4096 |
| 场景 | 多 spot/point shadow stress |
| 判定 | 0 diff（64 baseline）；无越界；性能记录 |

---

## 6. 风险与决策

### 6.1 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 4K/cap128 cluster index 33.4 MB | 显存/带宽 | 4K 默认 cap64；128 opt-in；记录峰值 |
| 4096 shadow atlas | 3 张 D24S8 约 192 MiB | cached 未独立缩小；仅 opt-in，默认 2048 |
| 4 类 FPTL 内存 | 1080/cap128 ≈8.5 MB texture | 只 4 类；Area/Env/Decal 空层；必要时 activeCategoryCount |
| atomic offset 非确定 | 不能比较 raw offset | 每 cluster 排序；比较 count/index 集合；图像 0 diff |
| 5th MRT 不支持 | 原计划需要额外 MRT | 实际使用 GBuffer alpha 通道，不依赖第 5 个 MRT |
| 16-bit index | 未来 >65535 灯受限 | static_assert 128；256 lights 另立 M10 |
| cookie atlas 渗色/格式 | 视觉误差 | padding/mip clamp；RGBA8 限制写文档 |
| HDRP 无 GPU atlas | 需求误解 | M9.6 只做 request/atlas scalability；GPU atlas 另立 |
| 29 feature variants | 编译爆炸 | M9 只 flags；variants M10 |

### 6.2 待确认决策

1. **M9.3 area light**：本文建议 M9 只落 category 空层，Area LTC/area shadow 留 M10；若必须 M9 出 area，需要新增 M9.3b，且会拉长验收。
2. **M9.6 shadow**：已作为 opt-in 实现；默认仍 64/2048，不改变 M8.7 行为。
3. **Cookie 格式**：M9 实际把 8×8 亮度样本打包进 light data，未使用 2048 atlas；HDR/高分辨率 cookie 留 M10。
4. **Category 数**：建议固定 4 类以对齐 HDRP；若内存压力大，改为 active=1 + 预留 enum。
5. **Light layer 存储**：实际采用 GBuffer0.a/GBuffer3.a，避免第 5 个 MRT；forward 保留 uniform。
6. **Cluster 内存**：实际采用确定性 per-cluster offset（`clusterIndex * clusterCapacity`），无 atomic；4K/128 保持 opt-in。

---

## 7. M9 出口标准

M9 完成必须满足：

- M9 features off：M8.7 的 32/64/128 视觉矩阵仍 **0 diff**。
- M9.1：packed FPTL/big-tile 双后端/双路径 0 diff；4K/cap128 texture 不 clamp。
- M9.2：128 同 cell 不再 32 overflow；CPU/GPU count/index 一致；debug slice 0 diff。
- M9.3：category/feature 数据契约完成；Punctual 0 diff；Area/Env/Decal 空层明确。
- M9.4：light layers off 0 diff；on 行为正确；forward/deferred/透明覆盖。
- M9.5：cookies/IES off 0 diff；spot/point atlas、fallback、clamp/repeat 覆盖。
- M9.6：64 baseline 0 diff，128/4096 可运行；不做 GPU atlas。
- `hbg_tests`/`ctest`/IDL/gfx-test/git diff 通过。
- 1080p 300 帧 cap64 默认 >60 fps；cap128 记录；4K/cap128 记录内存与是否建议 cap64。
- 文档更新，未实现项明确写“未实现”，不把规划写成完成。

---

## 8. 实现顺序建议

1. **M9.0**：冻结布局/常量/sidecar/验收基线；只写测试与文档。
2. **M9.1**：先 packed FPTL/big-tile，保持 cluster 不变；最容易独立验证。
3. **M9.2**：layered cluster；先单 category，后接 9.3；用 33/64/128 同 cell 验。
4. **M9.3**：category 维度 + feature flags；Area/Env/Decal 空层。
5. **M9.4**：light layers；forward uniform + deferred GBuffer alpha 通道。
6. **M9.5**：spot cookie → point cookie → spot IES；每步保持 off 0 diff。
7. **M9.6**：shadow scalability（opt-in，已实现）。
8. **M9.7**：全矩阵、性能、4K/内存、文档收口。

---

## 9. HDRP 参考索引

- 16-bit FPTL/big-tile：
  - `ShaderConfig.cs:129-150`、`ShaderConfig.cs.hlsl:25-32`
  - `LightLoopDef.hlsl:232-263`、`:326-342`
  - `lightlistbuild.compute:409-422`
  - `lightlistbuild-bigtile.compute:200-210`
- Layered cluster：
  - `ClusteredUtils.hlsl:106-111`
  - `LightLoopDef.hlsl:286-300`
  - `lightlistbuild-clustered.compute:76-78`、`:440-453`、`:482-502`
  - `HDRenderPipeline.LightLoop.cs:654-656`
  - `LightLoop.cs:871-874`
- Category/feature：
  - `LightLoop.cs:66-87`、`:120-122`、`:215-234`
  - `lightlistbuild.compute:345-386`、`:409-422`
  - `HDRenderPipeline.LightLoop.cs:621-657`
- Light layers：
  - `CommonLighting.hlsl:550-553`
  - `LightLoop.hlsl:305`、`:649`
  - `Lit.hlsl:883-884`
  - `BuiltinUtilities.hlsl:59`
  - `ShaderVariablesFunctions.hlsl:8-15`
  - `HDRenderPipeline.Prepass.cs:295-302`、`:776-782`
  - `HDAdditionalLightData.cs:3350-3357`
- Cookies/IES：
  - `LightCookieManager.cs:47-62`、`:242-272`、`:338-366`
  - `CookieSampling.hlsl:29-55`
  - `LightDefinition.cs:70-72`、`:142-180`
  - `LightEvaluation.hlsl:380-435`
  - `GlobalLightLoopSettings.cs:180-226`
- Shadow：
  - `HDShadowManager.cs:403-407`、`:1414-1452`
  - `HDShadowCullingUtils.cs:28-132`
