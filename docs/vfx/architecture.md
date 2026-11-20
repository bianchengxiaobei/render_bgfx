# VFX Runtime 架构（V1）

> 关联：`README.md`（决策/里程碑）、`asset-format.md`（资产）、`backend-api.md`（API）、`source-alignment.md`（源码对齐）。
> 目标：在 hdrp-bgfx 上提供可独立复用的 C++20 GPU 粒子运行时；编辑器后置。

---

## 1. 模块与边界

```
vfx/                                    独立 C++20 库（无 bgfx/HDRP 头文件）
├─ include/vfx/                         公开 API
├─ src/core/                            资产、JSON 图、表达式、子图、校验
├─ src/compiler/                        layout、dispatch、codegen、shaderc
├─ src/runtime/                         spawner、时间、事件、排序、bounds
├─ include/vfx/device.h,command_sink.h,shading.h  Device / CommandSink / ShadingProvider 接口
└─ tools/vfxbake.cpp                    CLI：.vfx.json → .vfxb

src/render/vfx/                         hdrp-bgfx 适配层
├─ hbg_vfx_backend                      Device/Sink → gfx::ResourceManager/gfx::Encoder
├─ hbg_vfx_renderer                     Runtime 持有者；addSimulationPass/addOutputPass
└─ hbg_vfx_shading                      unlit/lit shader pack；复用 lighting/light_loop.sc

src/render/passes/vfx_pass.cpp          RenderGraph 集成点（或并入 forward_renderer.cpp）
shaders/vfx/                            vfx_platform.sc / vfx_common.sc / quad/lit 模板
config/uniforms/vfx.idl                 系统/属性 uniform 形状（IDL 生成）
```

**边界规则**

- `vfx/` 不包含 `bgfx::*`、`hbg::*`、`RenderGraph` 头文件。
- 适配层负责：scene color/depth、light data/shadow/IBL、RenderGraph pass、glTF sidecar、CLI。
- 库负责：资产解析、编译、GPU 资源生命周期（通过 `Device`）、每帧命令（通过 `CommandSink`）。
- Shader 最终产物是 bgfx `.sc` → dxbc/spirv；V1.0b.5 生成内建 unlit 模板，`--shader-pack` 目前仅追加 include 根，模板替换留到后续。

## 2. 端到端流水线

### 2.1 离线烘焙

```
.vfx.json
  → parse/validate
  → flatten subgraphs
  → resolve properties / custom HLSL
  → analyze attribute read/write
  → attribute layout（capacity 对齐 64）
  → per-system dispatch plan / event graph / sort plan
  → codegen: vfx_attributes.sc + spawn/init/update/output_update/fill_indirect
  → codegen: quad VS/FS（unlit 或 lit pack）
  → shaderc: dxbc(s_5_0) + spirv
  → .vfxb（metadata + LUT + shader blobs）
```

V1.0b.5 已落地最小后半段：
- `vfx/src/compiler/codegen.cpp` 从 `Graph` 生成 attribute layout、7 个 logical program（spawner/deadListInit/init/update/outputUpdate/fillIndirect + output VS/FS）；init/update 使用 local attribute 变量，最终统一 `Store_*`。
- `vfx/src/compiler/compiler.cpp` 调 shaderc（dxbc `s_5_0` + spirv），对同源 shader 做进程内缓存；`vfx/src/compiler/binary.cpp` 写 `.vfxb`。
- `vfx/tools/vfxbake.cpp` 提供单文件/目录 `--check`；`vfx/tests/assets/minimal_unlit.vfxb` 已验证 program 创建。
- V1.0b.5 未实现 subgraph flatten/custom HLSL/lit/sort/events；编译期显式拒绝并留到 V1.1–V1.4。

V1.0b.6 已落地 runtime：
- `vfx/src/runtime/runtime_asset.cpp` 独立读取 `.vfxb`（不链接 compiler），校验 contentHash、section、双后端和 buffer 集合。
- `vfx/src/runtime/runtime.cpp` 的 `EffectImpl` 创建每 system 的 7 个 buffer 与每 program 的 GPU program；`update` 累积常量 rate/Burst；`encodeSimulation` 按 spawner→init→update→outputUpdate→fillIndirect 发命令；`encodeOutput` 绑定 attribute/indirection/texture 并 indirect draw。
- output VS 新增 `u_vfx_world`；local system 用 effect transform，world system 用 identity。
- Curve/Gradient 默认 LUT 与 `setCurve/setGradient` 上传已接入；当前 unlit shader 仍用编译期分段函数。

V1.0b.7 已接入 hdrp-bgfx：
- `.hdrp.json` 的 `vfx` 数组映射为 `RenderScene::vfxInstances`（asset/position/rotation/scale/play/playRate/seed/properties）。
- `ForwardRenderer` 惰性创建 `HbgVfxBackend`/`vfx::Runtime`；simulation pass 在 sky 后、opaque 前；output pass 在 opaque 与 transparent 之间；deferred 使用相同插入点。
- `PipelineSettings::supportVFX` 默认 false；`--vfx/--vfx-test/--no-vfx` 控制；`FrameDiagnostics` 和 `render performance` 输出 VFX 计数。
- `vfx/tests/assets/vfx_scene.gltf` + `.hdrp.json` 提供可复现的 D3D11/Vulkan 示例。

对齐参考：
- 编译入口/资产描述：`com.unity.visualeffectgraph/Editor/Models/VFXGraph.cs:156-206,1491-1535`
- 新编译 Pass 结构：`Editor/NewCompiler/VfxGraphCompiler.cs:33-48`
- 模板入口：`Shaders/Temp/Templates/{Init,Update,Output,OutputUpdate,FillIndirect}.hlsl`
- 属性布局：`Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`、`Editor/NewCompiler/Passes/AttributeLayoutPass.cs:30-115`

### 2.2 运行时每帧

```
Runtime::update(dt)
  ├─ clamp dt（固定/最大步长）
  ├─ spawner 累积（常量速率/Burst）
  ├─ CPU 事件入队
  ├─ effect bounds / culling 标记
  └─ 属性值/纹理/曲线 LUT 版本号

Runtime::encodeSimulation(sink, frame)
  ├─ 每个 system（依赖顺序）：
  │   ├─ 上传 system uniforms（time/dt/seed/capacity/spawnCount/world 矩阵）
  │   ├─ spawner kernel（1 group；计算 spawn 数或写 spawn 请求）
  │   ├─ init kernel（ceil(spawnCount/64)）
  │   ├─ update kernel（ceil(capacity/64)）
  │   ├─ GPU event append / prefix sum（如启用）
  │   ├─ output update kernel（写 sort KVP / indirection）
  │   ├─ sort（bitonic + merge，如启用）
  │   └─ fill indirect（写 32B args）
  └─ 记录 diagnostics

Runtime::encodeOutput(sink, frame)
  ├─ 每个 output（按排序规则）
  │   ├─ 绑定 attribute / indirection / lighting（lit）
  │   ├─ 上传 per-draw uniforms
  │   ├─ setState(depth test on/write off, blend)
  │   └─ submitIndirect(args, start, 1)
  └─ 记录 draw 数
```

V1.0b.6 实际路径：`update` 只做 rate/Burst 累积与 bounds 刷新；`encodeSimulation` 在首次/reset 帧额外 dispatch deadListInit，随后 spawner/init/update/outputUpdate/fillIndirect；事件、sort、lit 尚未接入。`encodeOutput` 绑定 VS 的 attribute/indirection buffer 和 FS sampler，设置 `u_vfx_world`/global camera uniforms，再 `drawIndirect`。

对齐参考：
- spawn/init/update 周期：`Shaders/Temp/Templates/Init.hlsl:10-42`、`Update.hlsl:10-29`
- output update/排序注册：`OutputUpdate.hlsl:14-33`
- 每帧 64 线程组：`Editor/NewCompiler/VfxDispatch.cs:11-21`
- 粒子属性切片：`Editor/NewCompiler/Data/DescriptionWriter/ParticleSystemDataDescriptionWriter.cs:61-112`

## 3. 数据契约

### 3.1 Attribute layout

- 属性类型：`float/float2/float3/float4/uint/int/bool`；每个属性占 `componentCount` 个 4B word。
- 布局：每个 group 一个 bucket；bucket 内按属性顺序累加 offset；`stride = bucketSize` 个 word；buffer size = `bucketOffset + capacity * bucketSize` word。
- `capacity` 向上对齐到 64（`VfxDispatch.AlignToThreadGroups`），避免 dispatch padding 越界。
- 属性访问：`offset = bucketOffset + elementOffset + particleIndex * stride`（word 索引）；`byteAddress = offset << 2`。
- V1 单 group；`AttributeLayoutPass` 的读写亲和性分组留到 V1.1 之后（Deviation D3）。
- V1.0b.1/2 实现：`vfx::defaultAttributeSet()` 固定按 §3 内置表顺序 + 顶层 `attributes` JSON 顺序；`buildAttributeLayout()` 生成单 bucket、64 对齐、CPU word 读写与 `generateAttributeShader()`（`BUFFER_RAW_*`/`rawLoad`/`rawStore`）。后续编译器按 block 读写集合收窄属性集。生成器由 `vfx/tools/attributes_dump.cpp` 驱动，默认 fixture 输出 `shaders/vfx/vfx_attributes.sc`。
- V1.0b.3 实现：表达式 DAG/类型/折叠/codegen 在 `vfx/src/compiler/expression.cpp`，parser 通过 `validateGraphExpressions` 接入；测试表达式 shader 由 `vfx/tools/expression_dump.cpp` 生成 `shaders/vfx/vfx_expressions.sc`。block 字段目标类型统一在 V1.0b.5 的 kernel 编译入口处理。
- V1.0b.4 实现：公共接口在 `vfx/include/vfx/device.h`、`command_sink.h`、`shading.h`；Mock 在 `vfx/tests/support/mock_backend.h`；hdrp-bgfx 适配器在 `src/render/vfx/hbg_vfx_backend.{h,cpp}`（`hbg_vfx_adapter` target），实现 `Device`→`gfx::ResourceManager`、`CommandSink`→`gfx::Encoder`、`ShadingProvider` 占位。`StorageVertex` 的合成 layout 仅用于 bgfx buffer 创建，raw attribute 仍按 word offset 读取。

对齐参考：
- `Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`
- `Editor/GraphCommon/Data/DescriptionWriter/AttributeDataDescriptionWriter.cs:6-43`
- `Shaders/Temp/Data/AttributeBuffer.hlsl:8-29`
- `Shaders/Temp/Data/ByteAddressBuffer.hlsl:9-120`

### 3.2 Buffer 清单（每 system / effect）

| Buffer | 类型 | 大小/布局 | 用途 | 参考 |
|---|---|---|---|---|
| attribute | StorageVertex (RAW) | `capacity * stride` words × instanceCount | 粒子属性 | `VFXDataParticle.cs:148-170` |
| deadList | Storage | `2 + capacity` uints（counter/copy/free list） | 分配/回收 | `VFXDataParticle.cs:898-900`、`DeadListData.hlsl:6-31` |
| eventList | Storage | counters + index buffer + attribute source | GPU/CPU 事件 | `EventListData.hlsl:8-84` |
| renderKVP | Storage | `capacity + 1`（counter + KVP×capacity） | sort/indirection | `RenderingData.hlsl:4-61` |
| sortScratch | Storage | 2 × KVP 容量 | bitonic/merge | `VFXDataParticle.cs:1185-1206,1489` |
| indirectArgs | Indirect | N × 32B | draw args | `src/gfx/buffer.cpp:132-138` |
| bounds | Storage | 6 floats | GPU AABB（V1 可选） | `VFXDataParticle.cs:729-743` |

- V1 每个 effect 一份 system buffer；实例切片用 uniform `baseIndex`，不依赖 `setStorageBuffer` 的 256B offset。
- `indirectArgs` 每条 32B：`{numVertices, numInstances, startVertex, startInstance, 0,0,0,0}`；D3D11/Vulkan 只读前 4 个 uint。
- V1 quad：**非索引 6 顶点/粒子**（两个三角形），`numVertices = 6 * alive`，`numInstances = 1`（Deviation D1/D2）；`alive = capacity - deadList.counter`。4 顶点只能组成一个三角形，不能再按文档旧假设使用 4。bgfx D3D11 的间接 draw 需要非空 vertex stream，因此绑定一个 6 顶点 dummy vertex buffer（V0 finding）。

### 3.3 Kernel 与 dispatch

| Kernel | 线程组 | dispatch | 说明 |
|---|---|---|---|
| spawner | 64 | 1 | 常量速率/Burst 产生 spawnCount；也处理 CPU 事件 |
| init | 64 | `ceil(spawnCount/64)` | `NewParticle` 从 dead list 分配；写属性 |
| update | 64 | `ceil(capacity/64)` | 读 alive、处理块、写回、死亡回收 |
| outputUpdate | 64 | `ceil(capacity/64)` | 写 sort key/indirection；无排序时顺序写 |
| sort | 64 | 按元素数 | bitonic 128/1024/2048/4096 + merge |
| fillIndirect | 64 | 1 | 读 alive，写 args |
| prefixSum | 128/1024 | 按事件数 | Sklansky prefix sum（V1.2） |

- `ThreadsPerGroup = 64`：`Editor/NewCompiler/VfxDispatch.cs:11`。
- 事件/排序的原始实现：`Shaders/VFXPrefixSum.compute:27-143`、`Shaders/Sort.compute:1-120`、`Shaders/VFXFillIndirectArgs.compute:31-120`。
- 初始化 dead list：`Shaders/VFXCopyBuffer.compute:63-70`（`size - index - 1`，counter = size）。

### 3.4 Spawner 状态

- 常量速率：`pending += rate * dt`（double 累加）；取整 spawn；余量保留到下一帧。
- Burst：单次在 `time` 后触发一次；周期首次在 `time`，之后每 `period` 触发，`repeat` 为总次数上限（0=无限）；多个 burst 同帧合并。
- 周期边界：`1 + floor((time - burst.time) / period + 1e-6)`；帧跨过多个周期时同帧补齐（受 dt clamp 限制），避免 float 边界漏触发。
- `maxDeltaTime`：单帧 dt 上限（防止后台恢复时爆发）；V1 默认 0.1s；负值/NaN 视为 0。
- CPU reference：`vfx::reference::SpawnerState`/`updateSpawner` 与 runtime 使用相同时间/累加/边界语义；固定 dt 序列测试。
- 随机种子：systemSeed 来自 effect seed + system index；每次 reset 恢复。
- 对齐参考：`Runtime/CustomSpawners/LoopAndDelay.cs`、`SpawnOverDistance.cs`（行为参考）；`VFXSpawnerBurst.cs`/`VFXSpawnerConstantRate.cs` 的 Count/Delay/Rate 语义。

### 3.5 力场

- update block 顺序执行，`integrate` 由图显式放置；CPU reference 与 GPU 使用相同顺序。
- `gravity`：`velocity += gravity * dt`。
- `drag`：`velocity *= exp(-dragEffective * dt)`；`useSize` 时 `dragEffective = drag * size^2 * scale.x * scale.y`（对齐 `Drag.cs`）。
- `force`：`absolute` 时 `velocity += force*dt`；`relative` 时 `velocity += (force - velocity) * min(1, drag*dt)`（`ForceHelper.ApplyForceString`）。
- `attractor`：`delta = position - center`；`velocity += normalize(delta) * strength/(|delta|^2+falloff) * dt`，距离平方下限 `1e-4`。
- `turbulence`：`field = position + scroll*time + 0.5`；Value curl noise（`frequency/octaves/roughness/lacunarity`），默认 `relative`、`drag=1`；`intensity` 缩放到力值。field 在当前 system 空间，`FieldTransform` 暂不支持。
- CPU/GPU 的 turbulence cell hash 使用整数 Wang hash；`VFXNoise.hlsl` 的大浮点 hash 在 D3D 乘加重排下会超过 1e-3，整数 hash 保证确定性。
- 质量在 V1.1.2 固定为 1.0；当前没有 mass 属性/Set Mass 块。原项目 Drag/Force/Turbulence 的 `/mass` 因此等价于不除。

### 3.6 事件

- CPU 事件：`Effect::sendEvent(name, attributes)` → 事件缓冲 → 下游 init 读取；V1 支持 1 个事件属性集。
- GPU 事件：输出/更新 kernel 写 `eventList`（atomic append + 总数）；下一个 system 的 init 读 `elementCount`/`totalCount`/`indexBuffer`。
- 跨 system 需要 prefix sum（`VFXPrefixSum.compute` 语义），保证多 system 事件计数连续。
- 排序：事件缓冲顺序即 spawn 顺序；属性通过 source index 读取。
- 对齐参考：`Shaders/Temp/Data/EventListData.hlsl:8-84`。

### 3.7 排序

- KVP：`{ float key; uint index; }`；key 相等时按 index 升序（确定性）。
- 排序方向：输出配置 `ascending/descending`；distance 排序用相机距离。
- 排序后写 indirection buffer；输出 VS 通过 `s_indirection[particleSlot]` 取粒子索引。
- 容量策略：`capacity <= 4096` 用单 dispatch bitonic；更大用 bitonic pre-pass + merge（对齐 `Sort.compute`）。
- 对齐参考：`Shaders/Sort.compute:65-120`、`Shaders/Temp/Data/RenderingData.hlsl:4-61`。

## 4. 渲染集成（hdrp-bgfx）

### 4.1 Pass 插入位置

| 路径 | 仿真 pass | 输出 pass |
|---|---|---|
| Forward | `addSkyPass` 后、`addForwardPass(opaque)` 前（`forward_renderer.cpp:4670-4682`） | `addForwardPass(opaque)` 与 `addForwardPass(transparent)` 之间 |
| Deferred | `addContactShadowPass` 后、`addGBufferClearPass` 前（`forward_renderer.cpp:4731-4756`） | `addDeferredLightingPass`/SSR 后、`addForwardPass(opaque/forwardOnly)` 后、`addForwardPass(transparent)` 前 |

- 仿真 pass：`graph.addComputePass("VFX Simulation")`、`allowCulling(false)`；如需读 depth/normal，显式 `readTexture`。
- 输出 pass：`graph.addRasterPass("VFX Output")`；`setColorAttachment(color)`、`readTexture(depth)`、`setDepthAttachment(depth)`；lit 时显式 read light data/shadow/environment（同 forward pass）。
- V1 的 VFX 输出在 `Forward Transparent` 之前，暂不与透明物体交错排序（Deviation D8）。

### 4.2 Quad 输出

- VS 输入：`vertexID`（每粒子 6 个顶点）、`instanceID`（V1 = 0）。
- `particleSlot = vertexID / 6`；`corner = vertexID % 6`；UV/offset 查 6 项表：`(0,0),(1,0),(1,1),(0,0),(1,1),(0,1)`，`offset = uv - 0.5`。4 顶点只能组成一个三角形，V0 已改为 6 顶点。
- 有排序：`particleIndex = indirection[particleSlot]`；无排序：`particleIndex = particleSlot`。
- 输出必须绑定非空 vertex stream（bgfx D3D11 的 indirect draw 在 `streamMask==0` 时不提交）；V1 用静态 6 顶点 dummy VB。
- 属性 → 位置：`GetElementToVFXMatrix(axis, angle, pivot, scale*size, position)`，再 VFX→world→clip；V1 可用简化：
  - billboard：相机右/上向量 × size × offset + position；
  - 平面：粒子 axis/angle；V1 先 billboard，axis/angle 在 V1.1 加。
- FS：`color = texture(mainTex, uv) * colorAttr; alpha = tex.a * alphaAttr`。
- 深度：`depth test on`、`depth write off`；alpha/additive blend；可选软粒子。
- 对齐参考：`Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`、`Shaders/Temp/Templates/Output.hlsl:4-60`。

### 4.3 Lit 输出

- 复用 `shaders/lighting/light_loop.sc:339` 的 `hbgEvaluateLightLoop(SurfaceData, viewWS, worldPosition, directionalShadowVisibility)`。
- `SurfaceData` 填充（参考 `shaders/material/surface_data_types.sc:6-37`、`forward.fs.sc:10-36`）：
  - `baseColor = colorAttr.rgb * tex.rgb`
  - `alpha = colorAttr.a * tex.a`
  - `metallic/roughness` 来自 output 属性
  - `normalWS = normalize(cameraForward)`（billboard）或粒子 axis；`tangentWS` = billboard right
  - `ambientOcclusion = materialAmbientOcclusion = 1`
  - `contactShadowMask/Fade` 由 `hbgSampleContactShadow` 填充
  - `lightLayerMask = 65535`
- `shadowVisibility = hbgSampleDirectionalShadow(worldPos, normalWS)`。
- Adapter 在上传/绑定上与 forward pass 保持一致：`PassUniforms`、`GlobalUniforms`、`ObjectUniforms`、`ClusterUniforms`、`EnvironmentUniforms`、`ProbeUniforms`。
- 透明 lit 使用 cluster light list（与 forward transparent 一致）。

### 4.4 Stage / 绑定预算

**Compute（buffer stage 0..15，texture stage 0..15）**

> 约束：D3D11 的 buffer/texture 最终都落在 SRV/UAV 槽位；VFX 约定 **buffer stage 与 texture stage 不重叠**，避免同号冲突。下表 buffer 用 0–8，texture 用 9–15。

| Stage | Buffer | Texture |
|---|---|---|
| 0 | attributes (RAW RW) | — |
| 1 | deadList | — |
| 2 | event counters/indices | — |
| 3 | event attributes/index | — |
| 4 | renderKVP / indirection | — |
| 5 | sort scratch A | — |
| 6 | sort scratch B | — |
| 7 | indirect args | — |
| 8 | bounds | — |
| 9 | — | curve LUT |
| 10 | — | gradient LUT |
| 11 | — | noise LUT（可选） |
| 12 | — | scene depth（可选） |
| 13 | — | property texture（可选） |
| 14–15 | reserved | reserved |

**Raster（VFX output）**

> 同样不重叠。V0 实测：VFX lit FS 由 `light_loop.sc` 带入 texture 5/6/7/9/10/11/14/15；attribute/indirection buffer 用 12/13。**前提是 VFX lit 变体不定义 `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`**；一旦开启，point/area shadow sampler 占用 12/13，会与 VFX buffer 冲突，必须重新分配或改用 VFX 专用 light-loop 变体。

| Stage | Buffer (VS/FS) | Texture |
|---|---|---|
| 0 | — | main texture |
| 1 | — | mask/second texture |
| 2 | — | gradient LUT |
| 3 | — | curve LUT |
| 4 | — | scene depth（soft particles） |
| 5 | — | light data（lit，同 forward） |
| 6 | — | directional shadow（lit，同 forward） |
| 7 | — | light list（lit，同 forward） |
| 8 | shadow context（FS RO，lit） | — |
| 9 | — | punctual shadow（lit，同 forward） |
| 10 | — | environment（lit） |
| 11 | — | contact shadow（lit，可选） |
| 12 | attributes (VS RO) | —（point shadow 关闭时） |
| 13 | indirection (VS RO) | —（point shadow 关闭时） |
| 14 | — | probe maps（lit；V1 可绑 1×1 空数组） |
| 15 | — | cookie atlas（lit，同 forward） |

- 上限依据：`build/vs2022/_deps/bgfx-src/src/config.h:498-500`（16 samplers）、`src/bgfx_p.h:211`（16 compute bindings）。
- `setStorageBuffer` 的 offset 必须 256B 对齐（`src/gfx/encoder.cpp:241-252`）；VFX 用 baseIndex uniform 切片，不传非对齐 offset。
- 前向 stage 参考：`src/render/passes/forward_renderer.cpp:6270-6310`。
- V1 lit 固定绑定 stage 14 的 1×1 probe 数组与 stage 15 cookie atlas（`light_loop.sc` 无条件声明），但不启用真实 probe/area 数据；若后续要启用 point/area shadow sampler（12/13），必须先重新分配 VFX buffer stage，不能直接开 define。

### 4.5 软粒子

- FS 采样 scene depth（stage 4），线性化后与粒子深度比较，`fade = saturate((sceneDepth - particleDepth) / softDistance)`。
- 需要输出 pass `readTexture(depth)`；depth attachment 保持只读测试。
- V1 默认关闭，output 属性 `softParticles=true` 时启用。

## 5. 属性、事件、子图、自定义 HLSL

### 5.1 Property API

- 类型：Float/Int/Bool/Vector2/3/4/Color/Texture/Curve/Gradient。
- 打包：`vec4 props[N]`（固定 N=16 或 32）；Color 占 1 个；Vector3 占 1 个（w=0）；Texture 用独立 sampler 槽位；Curve/Gradient 用 LUT 纹理。
- 每 system dispatch/draw 前由 adapter `setUniform` 上传；IDL 形状固定，避免运行时手写 uniform 名。
- 属性名与索引在 `.vfxb` 属性表中；运行时按名称查找。

### 5.2 子图

- 编译期展开：子图输入/输出映射到父图属性/表达式；递归深度上限（如 16）；循环检测。
- 展开后做一次 attribute 分析/布局；运行时不感知子图。
- 对齐参考：Unity `VFXSubgraph*`（按需复核）；V1 只做纯函数子图，不做上下文子图。

### 5.3 自定义 HLSL

- JSON block：`{"type":"custom_hlsl","code":"...","reads":["position"],"writes":["velocity"]}`。
- 编译期校验：
  - 禁止 `#include`、`#define`、`buffer`/`uniform` 声明、`discard`（fragment 除外）；
  - 禁止未声明的属性/属性名；
  - 括号/语句块平衡；
  - 注入宏：`VFX_ATTR(name)`、`VFX_SET_ATTR(name, value)`、`VFX_PROP(name)`、`VFX_TIME`、`VFX_DT`。
- 最终由 shaderc 双后端编译把关；失败则资产烘焙失败并报 JSON 路径。
- 自定义 HLSL 不能跨 backend 引入平台特定语法；文档要求只用 `.sc` 兼容子集。

### 5.4 Expression compiler

V1.0b.3 在 `vfx/include/vfx/expression.h` / `vfx/src/compiler/expression.cpp` 实现独立的表达式 DAG：

- `ExprType`：`float/float2/float3/float4/uint/int/bool`；`ExpressionNode` 保存 op、类型、常量值、inputs、HLSL 字符串。
- `ExpressionContext` 提供 property/attribute 名称解析；`compileExpression(Value, context, path, expected?)` 递归编译并返回 `CompiledExpression{root, errors}`。
- 支持最小集及 asset format 中的非纹理算子：`attr/property/time/dt/lifetime_ratio/add/sub/mul/div/min/max/clamp/lerp/remap/sin/cos/abs/saturate/random/noise/select/swizzle/sample_curve/sample_gradient`。
- 类型提升：标量→float 向量广播；`int/uint↔float`、`int↔uint` cast；禁止向量截断/向量→标量隐式转换。
- 常量折叠覆盖算术、clamp/lerp/remap、unary math、swizzle、常量 select；非有限浮点结果不折叠，整数除零按 Unity 语义为 0。
- JSON 内联树无引用环；仍执行 DAG DFS 环检测和 64 层深度限制。
- Parser 在 systems/subgraphs 解析后调用 `validateGraphExpressions`，表达式内部引用/类型错误直接带 JSON 路径返回。
- V1.0b.5 codegen 已输出 `VFX_ATTR/VFX_PROP/VFX_TIME/VFX_DT/VFX_LIFETIME_RATIO/VFX_RAND*/VFX_NOISE/VFX_SAMPLE_*` 宏及属性 local/store；V1.0b.6 runtime 绑定 `u_vfx_params`/`u_vfx_ids`/`u_vfxProp_*` 与 texture sampler。
- `random` 固定 seed 生成确定性 code，不推进 seed；V1.0b.5 已生成 value noise 与曲线/梯度分段函数，V1.0b.6 已上传 LUT 纹理，但 shader 采样 LUT 与 seed 推进留到 V1.1。
- `vfx/tools/expression_dump.cpp` 从 fixture 生成 `shaders/vfx/vfx_expressions.sc`；`vfx_expressions_test.cs.sc` 在 dxbc/spirv 下编译验证。

## 6. Curve / Gradient

- 属性控制点 → 编译期烘焙 LUT：
  - Curve：256×1 RGBA16F（或 R32F 四通道打包）；
  - Gradient：256×1 RGBA8/RGBA16F。
- LUT 数据写入 `.vfxb`（确定性字节），运行时上传为纹理；不依赖引擎曲线资产。
- kernel 采样：`t = saturate(age / lifetime)`；`texture2DLod(...,0)`。
- 对齐参考：Unity `AttributeFromCurve`/`ColorOverLife`（按需复核），V1 只做求值语义，不做编辑器曲线。

## 7. 测试与验证

| 层级 | 内容 | 工具 |
|---|---|---|
| 单测 | attribute layout、JSON 校验、表达式类型/折叠、子图展开、dispatch、indirect args、KVP 排序计划 | `vfx/tests` + doctest |
| Mock backend | dispatch 顺序/数量、绑定 stage、uniform 值、indirect 参数 | 无 GPU |
| CPU reference | 小粒子数逐帧位置/age/alive/color 对比 | 读回 + epsilon |
| GPU golden | unlit/lit、alpha/additive、soft particles、bursts、collision、events、sort、subgraph、custom HLSL | D3D11/Vulkan 截图 + PSNR/0-diff |
| 资源 | effect init/destroy、resize、buffer 复用、shutdown 泄漏 | `ResourceManager::hadLeaksAtShutdown` |
| 性能 | `vfx_particles`、`vfx_sim_ms`、`vfx_draw_ms`、`vfx_sort_ms`、`vfx_events`、`vfx_draws` | `render performance` 日志 |

验收口径：
- VFX off / empty scene：与基线 **0 diff**。
- 双后端：D3D11 vs Vulkan 截图对比（允许驱动差异时记录 PSNR，不伪装 0-diff）。
- 确定性：固定 seed/dt/frame，CPU reference 与 GPU readback 在 epsilon 内。
- 资源：`ctest` 后无泄漏；`idl_gen --check` 通过。

## 8. 性能测量（V1 不设门槛）

- 场景：10k / 100k / 1M quad；unlit/lit；sort on/off；events on/off。
- 记录：CPU update/encode 时间、GPU sim + draw（`gpu_frame_ms`）、draw/dispatch 数、sort 时间、内存占用。
- 先优化明显瓶颈（sort、binding、过度 dispatch），不引入 async compute / GPU instancing。

## 9. 关键风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 16 compute binding 不够 | 一个 kernel 无法同时绑定 attributes/deadList/event/sort/indirect | V0 spike 验证；拆 pass；合并小 buffer；uniform 化 |
| `setStorageBuffer` 256B offset | 实例切片不能用 sub-range | baseIndex uniform；整 buffer 绑定 |
| 32B indirect stride | args 写错导致 0 draw/越界 | V0 spike；单元测试固定 8×uint |
| shaderc 对生成代码不兼容 | 无法烘焙 | V0 spike 覆盖 raw buffer/atomics/shared/uniform array；`--dump-shaders` 留档 |
| Lit stage 冲突 | 光照资源覆盖 VFX 纹理 | V0 已验证：VFX lit 关闭 point/area sampler 后，texture 5/6/7/9/10/11/14/15 + buffer 12/13 不冲突；开启 point/area sampler 前必须重排 stage |
| 无 async compute | 仿真串行 | 先测量；sort/事件分帧或降频留 V2 |
| 自定义 HLSL 可移植性 | 单后端编译失败 | 受限子集 + 双后端编译门禁 |
| 与透明物体排序 | 视觉穿插 | V1 文档化；V1.4 评估并入透明 DrawList |

## 10. 与 Unity 的偏差汇总

见 `source-alignment.md` §5。实现时如新增偏差，必须在该表登记并说明关闭条件。

## 11. 任务映射

- `v0-checklist.md`：V0.1–V0.6。
- `v1-checklist.md`：V1.0b–V1.4。
- `v2-editor-outline.md`：编辑器边界（占位）。
