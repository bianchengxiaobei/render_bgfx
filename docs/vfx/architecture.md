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
- V1.0b.5 未实现 subgraph flatten/custom HLSL/lit/events；V1.2.2 已接入 KVP sort，V1.2.3 已接入 GPU event/prefix sum、V1.2.4 已接入 CPU event，V1.3.1 已接入 pure-function/block subgraph flatten，V1.3.2 已接入受限 custom HLSL，V1.3.3 已接入 JSON/HLSL 行列诊断、vfxbake asset-check CTest 与 Runtime reload；V1.3.4 已补子图/自定义 HLSL 双后端 fixture、D3D11/Vulkan golden 与校验测试；V1.4.1 已接入 lit output、共享 forward lighting binding 与 forward/deferred lit golden；V1.4.2 已接入 soft particles、scene depth stage 4 与 read-only depth attachment。

V1.0b.6 已落地 runtime：
- `vfx/src/runtime/runtime_asset.cpp` 独立读取 `.vfxb`（不链接 compiler），校验 contentHash、section、双后端和 buffer 集合。
- `vfx/src/runtime/runtime.cpp` 的 `EffectImpl` 创建每 system 的 9 个 buffer（含 renderKVP/sortScratch）、可选全局 `vfx.events` buffer 与每 program 的 GPU program；`update` 累积常量 rate/Burst；`encodeSimulation` 按 spawner→init→update→outputUpdate→(sort pre-pass/merge)→fillIndirect 发命令；有 GPU 事件时按 event reset→producer simulation→event prefix sum→consumer simulation 分阶段；`encodeOutput` 绑定 attribute/indirection/texture 并 indirect draw。
- output VS 新增 `u_vfx_world`；local system 用 effect transform，world system 用 identity。
- Curve/Gradient 默认 LUT 与 `setCurve/setGradient` 上传已接入；V1.1.4 起 compute shader 在 stage 9/10 双线性采样 LUT。

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

V1.0b.6 实际路径：`update` 只做 rate/Burst 累积与 bounds 刷新；`encodeSimulation` 在首次/reset 帧额外 dispatch deadListInit，随后 spawner/init/update/outputUpdate/sort/fillIndirect；V1.2.2 sort 在 outputUpdate 后覆盖 indirection，fillIndirect 同步 KVP counter；V1.2.3/4 在 event reset、CPU staging upload、producer 阶段、prefix sum、consumer 阶段之间插入全局 event buffer 调度；lit 尚未接入。`encodeOutput` 绑定 VS 的 attribute/indirection buffer 和 FS sampler，设置 `u_vfx_world`/global camera uniforms，再 `drawIndirect`。

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
| indirection | Storage/StorageVertex | `capacity` u32（draw slot → particle index） | 无排序顺序索引；V1.2.2 sort 覆盖为 sorted index | `Output.hlsl:4-60` |
| renderCount | Storage | 1 u32 | alive/draw 数、indirect args | `RenderingData.hlsl:4-61` |
| eventList | global Storage `vfx.events` | 每 link 5 counters + source index + 4×vec4 payload | GPU/CPU 事件追加/消费；V1 ≤64 KiB | `EventListData.hlsl:37-84`、`VFXPrefixSum.compute` |
| cpuEventStaging | Storage (COMPUTE_READ) | 与 event buffer 同布局的 CPU 上传镜像 | `sendEvent` → `vfx.cpu.events` | V1.2.4；`event_cpu_upload` 复制 |
| renderKVP | StorageVertex | `(1 + capacity) * 8`B：word0 key、word1 counter、随后 `{key,i}` | outputUpdate 注册，sort 输入 | `RenderingData.hlsl:4-61` |
| sortScratch | StorageVertex | `capacity * 8`B | bitonic pre-pass / merge ping-pong | `VFXDataParticle.cs:1185-1206,1489` |
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
| outputUpdate | 64 | `ceil(capacity/64)` | alive 才写 KVP 与顺序 indirection；sort 后覆盖 indirection |
| sort bitonic | 1024 | 1 | capacity <= 4096 单 dispatch；写 indirection |
| sort pre-pass | 1024 | `ceil(capacity/4096)` 次 | >4096 时每块 4096 KVP bitonic，写 sortScratch |
| sort merge | 64 | `ceil(capacity/64)` | 每 pass 合并 2×run，最终写 indirection |
| fillIndirect | 64 | 1 | 读 alive，写 args |
| event reset | 64 | 首次/reset | 清零全局 event buffer |
| event CPU upload | 64 | `ceil(CPU event words/64)` | 复制 CPU staging link 到全局 event buffer |
| event prefix sum | 128 | 1 | Sklansky 前缀和；每 link 写 frameCount/prefix 并清 elementCount |

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

### 3.6 碰撞

- update block 顺序执行；碰撞块可放在 `integrate` 前或后。实现同时检查当前穿透和本帧 swept 相交，命中后把位置放到碰撞点，避免固定 dt 下穿模。
- `collision_plane`：平面为 `dot(position, normal) = distance`；`normal` 在使用前归一化；支持当前在负侧和本帧从正侧穿过。
- `collision_sphere`：`center/radius` 球；当前在球内时投影到球面；外部用射线-球求交，`t` 限制在 `[0, dt]`。
- `kill`：命中后 `alive=false`，由 update 尾部回收；不修正位置/速度。
- `bounce`：法线速度按 `restitution` 反射，切向速度按 `1 - exp(-friction*dt)` 衰减；等效原 `CollisionBase` 的 Bounce/Friction 响应。
- `stick`：位置修正到碰撞点，速度清零。
- 默认 `mode=bounce`、`restitution=0.1`、`friction=0`；CPU reference 与 GPU 使用相同顺序和公式。
- 当前粒子半径为 0；原项目的 radiusMode、bounce speed threshold、lifetime loss、rough surface、碰撞事件和 sphere transform 尚未接入（D13）。

### 3.7 事件

- GPU 事件（V1.2.3）：producer 唯一 `emit` 与 consumer 唯一 `receive` 按名称连接；producer update 末尾写 source index + payload（最多 4 个 vec4 slot），无 `trigger_event` 时 alive→dead 自动发 1 个，显式 block 支持 count/condition。
- CPU 事件（V1.2.4）：没有同名 emit 的 receive 编译为 CPU link；`Effect::sendEvent` 按调用顺序入 per-effect pending queue，缺省属性使用 consumer 属性默认值，类型不兼容/未知事件名通过 error callback 拒绝。
- CPU runtime 将 pending 写入 `vfx.cpu.events` staging buffer（Storage + COMPUTE_READ），每帧 dispatch `event_cpu_upload`（task 13）复制 CPU link 范围到全局 `vfx.events`；GPU producer 随后追加；统一执行 128-thread Sklansky prefix，再跑 consumer init/update。
- event buffer 每 link 为 `elementCount/totalCount/prefix/dropped/frameCount` + source index + `capacity*16` payload words；CPU/GPU link 都参与同一 frame prefix，`frameCount`/`prefix` 驱动 consumer particleId 与 spawn 数；超出 capacity 写 `dropped`。
- 限制：同一 system 不能同时收发，receiver 不能有 CPU spawner；CPU receive 必须显式 attributes；V1 event buffer ≤64 KiB、最多 128 links；CPU `Diagnostics::eventCount` 仍为 link 数，精确计数读 buffer。
- 对齐参考：`Shaders/Temp/Data/EventListData.hlsl:8-84`、`VFXPrefixSum.compute:27-143`、`Editor/NewCompiler/Builder/GpuEventBuilder.cs`、`ParticleSystemBuilder.cs:565-580`。
### 3.8 排序

- KVP：`{ float key; uint index; }`；key 相等时始终按 index 升序（确定性 tie-break）。
- `distance` key = 相机空间/世界空间位置差的平方距离；`custom` key = scalar `sortKey` 属性；system `order` 控制 ascending/descending。
- 排序后写 indirection buffer；输出 VS 通过 `s_indirection[particleSlot]` 取粒子索引。
- 容量策略：`capacity <= 4096` 用一次 4096-element bitonic dispatch（覆盖 128/1024/2048/4096）；更大时每个 4096-KVP 块单独 dispatch bitonic pre-pass 写 `sortScratch`，再按 4096→8192→… merge，最终写 indirection（对齐 `Sort.compute` 的 pre-pass + merge 结构）。
- V1.2.2 所有 output 共享 system 排序与 indirection；不同 output 的 sort 配置必须一致。
- `src/render/vfx/hbg_vfx_backend.cpp` 将 renderKVP/sortScratch 也创建为 `StorageVertex`；避免大于 64KB 的 bgfx dynamic index buffer 在 Vulkan 上作为 compute storage 使用时触发 device lost。
- 对齐参考：`Shaders/Sort.compute:65-120,240-345`、`Shaders/Temp/Data/RenderingData.hlsl:4-61`。

## 4. 渲染集成（hdrp-bgfx）

### 4.1 Pass 插入位置

| 路径 | 仿真 pass | 输出 pass |
|---|---|---|
| Forward | `addSkyPass` 后、`addForwardPass(opaque)` 前（`forward_renderer.cpp:5384`） | `addForwardPass(opaque)` 与 `addForwardPass(transparent)` 之间（`forward_renderer.cpp:5396`） |
| Deferred | `addSkyPass`/decal 后、`addGBufferClearPass` 前（`forward_renderer.cpp:5462`） | `addDeferredLightingPass`/SSR 后、`addForwardPass(forwardOnly)` 后、`addForwardPass(transparent)` 前（`forward_renderer.cpp:5481`） |

- 仿真 pass：`graph.addComputePass("VFX Simulation")`、`allowCulling(false)`；如需读 depth/normal，显式 `readTexture`。Deferred 的 contact shadow 在 GBuffer 之后，不是仿真依赖，因此仿真保持在 GBuffer 前。
- 输出 pass：`graph.addRasterPass("VFX Output")`；`setColorAttachment(color)`、`readTexture(depth)`、`setDepthAttachment(depth, 0, true)`（read-only depth）；lit 时显式 read light data/shadow/environment/cluster light list（同 forward pass）。HDRP 的 forward-only **opaque** 仍必须先于 VFX 输出，否则无深度写入的粒子会被其后的 opaque 覆盖；VFX 输出只保证在 transparent 前。
- V1 的 VFX 输出在 `Forward Transparent` 之前，暂不与透明物体交错排序（Deviation D8）；见 §4.5 的排序成本评估。

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
- V1.4.1 落地：`makeOutputFragmentSource` 生成 lit FS（`SurfaceData` + `hbgSampleDirectionalShadow` + `hbgEvaluateLightLoop`）；`ForwardLightingContext`/`bindForwardLighting()` 由 forward 与 `HbgVfxBackend::bindLighting` 共用；stage 5/6/7/8/9/10/11/14/15 绑定，12/13 仍给 attribute/indirection。
- V1.4.1 VFX lit 使用全局 light list fallback（`lightListParams.w=0`、cluster 关闭），避免与 GPU buffer 变体的 shader 变体绑定不一致；V1.4.3 已让 lit VFX 绑定 transparent cluster light list（见 §4.6）。

### 4.4 Stage / 绑定预算

**Compute（buffer stage 0..15，texture stage 0..15）**

> 约束：D3D11 的 buffer/texture 最终都落在 SRV/UAV 槽位；VFX 约定 **buffer stage 与 texture stage 不重叠**，避免同号冲突。下表 buffer 用 0–8，texture 用 9–15。

| Stage | Buffer | Texture |
|---|---|---|
| 0 | attributes / spawnCount / indirectArgs / sort source | — |
| 1 | deadList / renderCount / renderKVP / sort destination | — |
| 2 | indirection / spawnCount / renderCount / sort count | — |
| 3 | renderCount | — |
| 4–8 | reserved for bounds（V1.3+） | — |
| 9 | — | curve LUT |
| 10 | — | gradient LUT |

V1.2.2 实际 binding：`output_update` = attributes(0,RO) / renderKVP(1,RW raw) / indirection(2,RW raw) / renderCount(3,RW uint)；bitonic = source(0,RO) / indirection 或 sortScratch(1,WO/RW) / renderCount(2,RO)；merge = source(0,RO) / KVP 或 indirection(1,RW/WO) / renderCount(2,RO)；`fill_indirect` = indirectArgs(0,WO) / renderCount(1,RO) / renderKVP(2,RW raw)。
V1.2.3 实际 binding：producer `update` 的 event buffer 在 stage 2 RW；consumer `init` 的 event buffer 在 stage 2 RO；`event_reset`/`event_prefix_sum` 的 event buffer 在 stage 0 RW。V1.2.4 `event_cpu_upload` 为 staging(0,RO) / event buffer(1,RW)。event buffer 不属于 system.buffers，而是全局 `vfx.events`。
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

- V1.4.2 实现：FS 采样 scene depth（stage 4），用 `u_global_invViewProj`/`u_global_view` 将 raw depth 与粒子世界位置线性化到 eye depth，`fade = smoothstep(saturate((sceneZ - selfZ) / softDistance))`；对应 Unity `VFXGetSoftParticleFade`（`VFXCommonOutput.hlsl:108-159`）。
- `softParticles`/`softDistance` 来自 output；`softDistance` 默认 0.1、最小 0.001。开启时 FS 绑定 `s_vfxSceneDepth`（stage 4）并上传 `u_vfxSoftParams`；无 depth 时绑 1×1 白纹理降级为不淡出。
- 输出 pass 显式 `readTexture(depth)`；RenderGraph/Framebuffer 支持 read-only depth attachment（`BGFX_ATTACHMENT_READ_ONLY_DEPTH|STENCIL`），避免 D3D 同时采样/绑定时读回 0。
- V1 默认关闭，output 属性 `softParticles=true` 时启用；关闭路径保持原 unlit/lit shader 与 golden 不变。
- 已知限制：quad 为 camera-facing billboard，单粒子屏幕深度近似常量，当前表现为逐粒子距离淡出；完整屏幕空间边缘留待非 billboard 输出。

### 4.6 帧顺序与透明排序

- Forward：sky → VFX simulation → contact shadow/opaque → VFX output → transparent。
- Deferred：sky/decals → VFX simulation → GBuffer/contact shadow → deferred lighting/SSGI/SSR → forward-only opaque → VFX output → transparent。
- 默认行为仍是 VFX output 在 transparent 之前（D8）。V3.16 opt-in effect-level ordering 会把每个 effect 的 VFX pass 插入场景 transparent DrawList 的深度位置；V3.24 在同开 `--vfx-instance-batch>0` 时改为每个 active instance batch 一个深度位置（n=1 为逐 instance）。透明 DrawList 使用 `kTransparentSortingCriteria`，VFX 内部粒子继续使用各自的 KVP key（distance/camera depth/youngest/custom），V3.9 起 output 可独立翻转 draw 方向，V3.10 起 output 可独立选择 criterion。
- 并入 DrawList 的成本：VFX 是间接 6 顶点 draw，不是 mesh/submesh batch；要按粒子深度与透明物体统一排序，需要把 VFX 拆成逐粒子 draw、CPU 读回全量粒子深度，或在 DrawList 增加 indirect batch 及 effect-level 深度 key。V3.16 已实现 opt-in effect-level key 与 transparent 段拆分；默认仍保留 pass 级顺序，逐粒子 interleave 与完整 D8 仍待后续。
- 透明 lit：`prepareVfxClusterLighting` 在 texture light-list 路径直接复用 cluster texture；GPU light-list buffer 路径为 VFX 构建 CPU cluster-texture mirror 并上传到 `m_clusterTexture`，VFX pass 绑定该 texture 与 `u_cluster_*`。成本是 lit VFX 活跃时每帧一次 CPU light-list/cluster 构建；V1.4.4 再评估缓存/GPU buffer 变体。
- Unity 参考：`VFXSortingUtility.cs:9-56`、`NewCompiler/Builder/OutputUpdateTaskBuilder.cs:79-127`、`VFXAbstractParticleOutput.cs:110-120`；HDRP transparent sorting `HDRenderPipeline.cs:3465-3498`、`HDRenderPipeline.RenderGraph.cs:1899-1911`。

## 5. 属性、事件、子图、自定义 HLSL

### 5.1 Property API

- 类型：Float/Int/Bool/Vector2/3/4/Color/Texture/Curve/Gradient。
- 打包：`vec4 props[N]`（固定 N=16 或 32）；Color 占 1 个；Vector3 占 1 个（w=0）；Texture 用独立 sampler 槽位；Curve/Gradient 用 LUT 纹理。
- 每 system dispatch/draw 前由 adapter `setUniform` 上传；IDL 形状固定，避免运行时手写 uniform 名。
- 属性名与索引在 `.vfxb` 属性表中；运行时按名称查找。

### 5.2 子图

- V1.3.1 在 parse 阶段展开：表达式 output、init/update block 与 `input`/同名 property 引用替换为 args/default；`Graph::subgraphs` 清空，运行时不感知子图。
- 递归深度上限 16；定义级 DFS + 展开栈双重循环检测；错误带 `subgraphs[i]`/block/args JSON 路径。
- 对齐参考：Unity `VFXSubgraphBlock.cs` 的 `RecreateCopy`/`PatchInputExpressions`/`recursiveSubBlocks`，`VFXSubgraphOperator.cs` 的 input/output properties 与 `TransferExpressionToParameters`；V1 只做纯函数/block 子图，不做 context subgraph。

### 5.3 自定义 HLSL

- JSON block：`{"type":"custom_hlsl","code":"...","reads":["position"],"writes":["position"]}`；init/update 均可使用。
- `code` 是语句片段；读属性必须 `VFX_ATTR(name)` 且 `name` 在 `reads`，写属性必须 `VFX_SET_ATTR(name, value)` 且 `name` 在 `writes`；读改写同时声明。
- 注入宏：`VFX_ATTR(name)`、`VFX_SET_ATTR(name, value)`、`VFX_PROP(name)`、`VFX_TIME`、`VFX_DT`；`VFX_LIFETIME_RATIO` 需要 reads age/lifetime。
- `vfx/src/core/custom_hlsl.cpp` 在 parse/codegen 两处校验：禁用 `#` 预处理（include/define）、buffer/uniform/texture/sampler、直接 `a_*`/`attributes.Load_*`/`Store_*`/`s_vfx*`/`u_vfx*`、`return`/`discard`/barrier 等；错误带 JSON 路径与 HLSL 行/列。
- `vfx/src/compiler/codegen.cpp` 注入 `VFX_SET_ATTR` 宏，并把片段包在显式 `{ ... }` 作用域；`collectBlockUsage` 合并声明的 reads/writes。
- 最终由 shaderc 双后端编译把关；失败则资产烘焙失败。自定义 HLSL 仍限制在 `.sc` 兼容子集，不支持函数定义/include/平台 intrinsic/output stage 块。
- 对齐参考：Unity `CustomHLSL.cs` 的 `VFXAttributes` 拷贝/写回与 `BuildSource`/`BuildCustomCode`；V1 用宏化语句片段替代完整 HLSL function editor。

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
- `random` 固定 seed 生成确定性 code，不推进 seed；V1.0b.5 已生成 value noise，V1.0b.6 已上传 LUT 纹理，V1.1.4 起 `sample_curve/sample_gradient` 改为 LUT 双线性采样。
- `vfx/tools/expression_dump.cpp` 从 fixture 生成 `shaders/vfx/vfx_expressions.sc`；`vfx_expressions_test.cs.sc` 在 dxbc/spirv 下编译验证。

## 6. Curve / Gradient

- 属性控制点 → 编译期烘焙 LUT：
  - Curve：256×1 RGBA16F，仅 `.x` 有效；
  - Gradient：256×1 RGBA16F，RGBA 全通道。
- 每条 property 的 2048B LUT 写入 `.vfxb` 的 LUTS section（16B 对齐，确定性字节），不依赖引擎曲线资产。
- V1.1.4 的 codegen 在 init/update compute shader 中声明 `s_vfxCurveLut`(stage 9)/`s_vfxGradientLut`(stage 10)；`makeLutBindings` 把实际 property 的 binding 追加到 kernel（`codegen.cpp:415`）。
- 运行时把同类型 property 按 JSON 顺序打包为一张 `256 × N` 纹理：row 为类型内序号；`setCurve`/`setGradient` 重传整张类型纹理（`runtime.cpp:931-997`）。
- `VFX_SAMPLE_CURVE/GRADIENT(name, t)` 与 over-life block 使用同一个 LUT 函数：`u = (saturate(t) * 255 + 0.5) / 256`，`v = (row + 0.5) / N`，`texture2DLod(..., 0)` 由 sampler 做双线性/clamp 采样。
- over-life 的 `t = saturate(age / max(lifetime, 1e-6))`；`color_over_life` 写 RGB，`alpha_over_life` 写 alpha，`size_over_life` 写 size。
- CPU reference 使用控制点精确线性求值（`cpu_reference.cpp:526-605`）；`curve_1024` 使用线性曲线/渐变，GPU half LUT + 双线性与 CPU 在 1e-3 内一致。D3D11/Vulkan CTest 均通过。
- 对齐参考：Unity `ColorOverLife.cs:93`、`AttributeFromCurve.cs:332-358`、`SampleCurve.cs:7-27`、`SampleGradient.cs:7-27`、`VFXCommon.hlsl:536-558`；本实现只做求值语义，不做编辑器曲线。
- 偏差：当前 block 只做 OverLife 覆盖（无 composition、BySpeed/Random/Custom）；LUT 固定 256 宽，不支持 wrap/metadata（D14/D15）。

## 7. 测试与验证

| 层级 | 内容 | 工具 |
|---|---|---|
| 单测 | attribute layout、JSON/子图/custom HLSL/lit material 校验、表达式类型/折叠、子图展开、dispatch、indirect args、KVP 排序计划、forward lighting stage 表 | `vfx/tests` + doctest |
| Mock backend | dispatch 顺序/数量、绑定 stage、uniform 值、indirect 参数 | 无 GPU |
| CPU reference | 小粒子数逐帧位置/age/alive/color 对比 | 读回 + epsilon |
| GPU events | V1.2.3 `vfx_event`、`vfx_events_count`、`vfx_events_multi`、`vfx_events_overflow`；D3D11/Vulkan event reference 校验 frameCount/prefix/dropped、receiver attrs 与 particleId | `--vfx-event-reference` |
| CPU events | V1.2.4 `vfx_cpu_event`；D3D11/Vulkan sendEvent 顺序/默认属性/类型拒绝、64 上限/dropped、consumer attrs | `--vfx-cpu-event-reference` |
| GPU golden | 基础 unlit additive；V1.1.5 bursts、forces、collision、curves；V1.2.2 sort on/off；V1.2.5 events；V1.3.4 subgraph/custom HLSL；V1.4.1 lit alpha forward/deferred；V1.4.2 soft on/off；V1.4.3 order forward/deferred 与 VFX off | D3D11/Vulkan 截图 + PSNR/0-diff；V1.1.5 feature 使用每后端 golden；V1.2.5 sort/events 另有同场景两次渲染确定性比较；V1.3.4 subgraph/custom HLSL 双后端逐字节一致；V1.4.1 lit 允许 4 px/1 LSB（记录 PSNR）；V1.4.2 soft 同后端 0 diff，on/off 差异单独记录；V1.4.5 全矩阵双后端通过 |
| 编辑器文档 | `vfx/editor.h` 加载/typed 编辑/结构 authoring/undo/Curve-Gradient/非法回滚/sidecar round-trip/compile error focus/default binary path；`.vfxb`/既有 runtime API 语义不变（V2.4 新增 `advanceOneFrame`） | `hbg_vfx_tests`（80/80, 38,056 assertions；13 editor cases / 299 assertions） |
| 编辑器 UI | ImGui 节点画布 + Inspector + add/remove/undo/文本输入/Curve-Gradient + Save/Compile + error focus + Playback/Runtime 面板；加载 `minimal_unlit.vfx.json`，3 帧退出 | `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` CTest |
| 编辑器编译/hot reload | Save/Compile → `compileGraphFile` → 同目录 `.vfxb`；匹配 scene VFX instance → `Runtime::reloadEffect`；ParseError 点击定位 context/block/property；`Preview` 编译未保存图到临时 `.vfxb` | `hbg_vfx_editor_compile` headless CTest（canonical + preview，校验 graph/canonical SHA256 不变）；临时 scene + `--vfx` + Ctrl+S 本地日志 `compiled ...; hot reloaded` |
| 编辑器 playback | Play/Pause、Step（固定 1/60s）、Replay、Time scale、固定 seed、bounds 数值、`FrameDiagnostics` 运行时指标 | `hbg_vfx_runtime_tests`（19/19, 494）；`vfx_editor_v24_final.png` 双后端 smoke 截图 |
| 空 effect | `empty_visual.vfxb` 零 spawn、零 alive；场景含 opaque/transparent/point light | 与 `--no-vfx` 同帧截图逐字节比较，D3D11/Vulkan **0 diff** |
| 资源 | effect init/destroy、resize、buffer 复用、shutdown 泄漏；`--resize-test` 覆盖 full/half 尺寸切换 | `ResourceManager::hadLeaksAtShutdown`、16 次 create/destroy、4 次 backend/runtime session |
| 性能 | `avg_frame_ms/fps`、`vfx_particles`、`vfx_draws`、`vfx_dispatches`、`vfx_sort_passes`、`vfx_events`、`vfx_ms`、`vfx_update_ms`、`vfx_sim_ms`、`vfx_output_ms`；sort reference 的 `steady_ms_per_frame/readback_ms/dispatches/sort_passes` | `render performance` / `vfx reference performance` 日志 |

验收口径：
- VFX off / empty scene：与基线 **0 diff**。
- 双后端：D3D11 vs Vulkan 截图对比（允许驱动差异时记录 PSNR，不伪装 0-diff）。
- 确定性：固定 seed/dt/frame，CPU reference 与 GPU readback 在 epsilon 内。
- 资源：`ctest` 后无泄漏；`idl_gen --check` 通过。

## 8. 性能测量（V1 不设门槛）

- 场景：10k / 100k / 1M quad；unlit/lit；sort on/off；events on/off。V1.4.4 使用 unlit additive + distance sort 的 10k/100k，1M 关闭 sort（V1 sort capacity 上限 262144）。
- 记录：CPU `vfx_update_ms`/`vfx_sim_ms`/`vfx_output_ms`/`vfx_ms`、GPU sim+draw+sort 合计（`gpu_frame_ms`）、draw/dispatch/sort pass 数。
- 先优化明显瓶颈（sort、binding、过度 dispatch），不引入 async compute / GPU instancing。
- V1.1.5 已记录 1920×1080×300 帧 Debug 基线（`v1-checklist.md`）：bursts 230.2/163.2 fps、forces 265.6/175.5 fps、collision 235.0/188.7 fps、curves（8 实例）224.5/140.3 fps（D3D11/Vulkan）；V1.4.4 记录 640×360×60 帧 Debug 基线：10k/100k/1M D3D11 为 162.0/69.8/107.2 fps，Vulkan 为 53.7/52.9/49.2 fps；100k sort 30 passes 是 D3D11 主要瓶颈，1M 无 sort。仅作回归参考，不设门槛。

## 9. 关键风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 16 compute binding 不够 | 一个 kernel 无法同时绑定 attributes/deadList/event/sort/indirect | V0 spike 验证；拆 pass；合并小 buffer；uniform 化 |
| V1 event buffer >64 KiB | Storage buffer 在 Vulkan 大容量 compute 写入可能 device lost | V1.2.3 编译期限制 event buffer ≤64 KiB；V1.3 拆分/分块 |
| `setStorageBuffer` 256B offset | 实例切片不能用 sub-range | baseIndex uniform；整 buffer 绑定 |
| 32B indirect stride | args 写错导致 0 draw/越界 | V0 spike；单元测试固定 8×uint |
| shaderc 对生成代码不兼容 | 无法烘焙 | V0 spike 覆盖 raw buffer/atomics/shared/uniform array；`--dump-shaders` 留档 |
| Lit stage 冲突 | 光照资源覆盖 VFX 纹理 | V0 已验证：VFX lit 关闭 point/area sampler 后，texture 5/6/7/9/10/11/14/15 + buffer 12/13 不冲突；开启 point/area sampler 前必须重排 stage |
| 无 async compute | 仿真串行 | 先测量；sort/事件分帧或降频留 V2 |
| 自定义 HLSL 可移植性 | 单后端编译失败 | 受限子集 + 双后端编译门禁 |
| 与透明物体排序 | 视觉穿插 | V1.4.3 已评估：并入 DrawList 需间接 batch/逐粒子深度键，成本高于收益；保留 pass 级顺序并登记 D8 |

## 10. 与 Unity 的偏差汇总

见 `source-alignment.md` §5。实现时如新增偏差，必须在该表登记并说明关闭条件。

## 11. 任务映射

- `v0-checklist.md`：V0.1–V0.6。
- `v1-checklist.md`：V1.0b–V1.4。
- `v2-editor-outline.md`：编辑器边界与 ImGui/布局 sidecar 决策。
- `v2-checklist.md`：V2.0–V2.5 任务、参考与验收。

## 12. V2 编辑器

- 边界：编辑器只读写 `.vfx.json`；`.vfxb` binary、既有 runtime API 语义、shader pack stage 表不变；V2.4 仅新增 additive `Effect::advanceOneFrame()`。
- 文档模型：`vfx/include/vfx/editor.h` + `vfx/src/editor/document.cpp`；使用 `ordered_json` 保留字段顺序，每次 typed 修改后 `parseGraphJson()` 重新验证，失败恢复旧 JSON。
- 布局 sidecar：`<asset>.editor.json`，记录 `systems/<i>/{spawner,init,update}|outputs/<j>` 的位置、折叠、pan/zoom 和 `graphHash`；不写入 `.vfx.json`。缺失/损坏 sidecar 只影响布局，不阻塞图。
- UI：`src/app/vfx_editor_ui.{h,cpp}` 复用 DebugUI 的 ImGui context，自绘 context 卡与连线；不新增 render graph pass、sampler 或 runtime 资源。`--vfx-editor=<json> --scene=<gltf>` 打开；`HBG_BUILD_VFX=OFF` 使用 stub。
- 当前能力：Spawn/Init/Update/Output 图、属性列表、system add/remove/rename + capacity/space/sort/bounds/rate、block add/remove/move/enable + 参数编辑、output add/remove + lit/blend/texture/soft/metallic/roughness、property add/remove + 数值/Curve/Gradient/texture、undo/redo、文本输入、Save/Reset Layout、ParseError 面板。
- 编译：`Save`（默认 `On Save`）按需写 graph + layout，再调用 `vfx::compileGraphFile()`；也可手动 `Compile`。编译选项来自 CLI/env/CMake 默认，binary 写同目录 `.vfxb`（临时文件替换）。`ParseError` 可在 Compile 面板点击，映射到 context/block/property 并居中画布。
- hot reload：`ForwardRenderer::reloadVfxAsset()` 按路径匹配 scene VFX instance，成功写 binary 后调用 `Runtime::reloadEffect()` 并重放 instance properties；无匹配 instance 时编译成功但显示 skipped。
- playback：`ForwardRenderer::toggleVfxPaused/stepVfx/restartVfx/setVfxPlayRate/vfxPlaybackStatus` 控制匹配实例；`Effect::advanceOneFrame()` 在暂停时执行固定 1/60s 仿真一步，hot reload 保留 playback 状态。
- preview：`VfxEditorUI::preview()` 将未保存 `Document::serialize()` 编译到临时目录 `.vfxb`，经 `ForwardRenderer::previewVfxAsset()` 替换 scene 实例或创建 editor-only effect；不写 graph/layout/canonical binary，失败保留旧 effect。`--vfx-editor-preview` 为 headless smoke。
- 诊断：V2.4 Editor Inspector 的 Runtime 面板消费 `FrameDiagnostics` 的 effects/systems/particles/draws/dispatches/sorts/events 与 update/sim/output/total ms。
- 未做：copy/paste、subgraph invocation 插入、表达式文本编辑、保存确认/自动恢复（V2.2b）、bounds 3D 线框（V2.4 当前为数值）。同步编译/预览会阻塞 UI，raw shaderc 日志尚无可点击的 JSON/HLSL 位置。
- 参考：Unity `VFXUI` 布局/sanitize（`Editor/Models/VFXUI.cs:33-132`）、GraphValidation（`Editor/GraphView/Views/Controller/VFXGraphValidation.cs:25-46`）、Save/Compile（`Editor/GraphView/Views/VFXView.cs:1977-2004`）、节点创建（`Editor/GraphView/Views/VFXView.cs:575-611`）。

## 13. V3.0 粒子 motion vectors

- 边界：只增加 renderer/runtime 的 additive path，不改变 `.vfx.json`、`.vfxb` v6、shader pack stage 表与既有 `encodeOutput` 语义。`Runtime::encodeMotionVectors()` 和 `HbgVfxBackend::resolveProgram("vfx.motionVectors")` 是新 API；`FrameSettings::MotionVectors` 仍由现有 pipeline 门控。
- 数据流：VFX Simulation 后，VFX Output 写 scene color；独立 `VFX Motion Vectors` raster pass 写 `resources.motionVector`，绑定 scene depth 做 `LessEqual` 深度测试，复用每个 system 的 indirect args。顶点程序按 billboard 公式重建当前世界位置，上一帧位置读 `oldPosition`；attribute stride 通过 `u_vfxMotionParams.w` 逐 system 传入；current/previous clip 分别用 `u_global_nonJitteredViewProj`/`u_global_prevViewProj`，片元输出 `(currentNDC - previousNDC) * 0.5`。
- 兼容性：`--vfx`/scene VFX 实例与 `FrameSettings::MotionVectors` 同时启用时才提交 pass；program 不可用时 runtime 静默跳过。Debug view `motion-vectors` 下仍执行 VFX simulation/output。
- 限制：上一帧 billboard offset 仍复用当前 size/angle，尺寸/旋转突变时 MV 是近似值；没有 flipbook、逐顶点 strip/mesh、per-particle previous element matrix；透明 output 用纹理 alpha cutoff 0.001，opaque 不裁。V3.2 增加 per-output `generateMotionVector` gate，V3.3 增加 effect transform 历史。
- 参考：`Shaders/VFXParticleCommon.template:199-220`；`Shaders/VFXCommonOutput.hlsl:311-323`；HDRP `Runtime/VFXGraph/Shaders/VFXCommon.hlsl:113-122`。

## 14. V3.1 粒子阴影

- 边界：新增 per-output `castShadows`（JSON 可选），复用 `.vfxb` v6 OUTPUTS 记录 offset 21 的 reserved byte；旧资产为 0，不投影。VFX shadow 使用固定 `vfx.shadow` program，不改变 output VS/FS 或 shader pack stage 表。
- 数据流：`ForwardRenderer` 在 shadow atlas clear/GPU cull 后、`addShadowPass` 前记录 VFX simulation，确保 shadow pass 读到本轮 indirect/attributes；每个 `ShadowRequest` 在 mesh shadow 后调用 `Runtime::encodeShadow(viewProj)`，runtime 对 `castShadows` output 绑定 attributes/indirection/texture，用 camera billboard + `u_vfx_world` + shadow VP 写 depth。存在 VFX shadow output 时 `ShadowManager::setCacheSuppressed(true)`，避免复用上一帧粒子 shadow。
- 透明 output：按 `s_vfxMain` alpha < 0.001 discard；opaque 不裁。state 为 depth write、LessEqual、无 back-face cull。
- 限制：没有 normal bias、alpha-to-coverage、flipbook/逐顶点 shadow、per-output shadow caster shader；punctual/area 光源共用 shader/matrix，尚未逐光源 feature golden。
- 参考：`VFXAbstractParticleOutput.cs:132,149,600`；`ParticlePlanarPrimitives/PassShadowCaster.template`；`PassDepthOrMV.template:91-106`；本地 shadow atlas/request/bias 在 `src/render/shadows/shadow_manager.cpp` 与 `src/render/passes/forward_renderer.cpp`。

## 15. V3.2 per-output motion-vector gate

- 边界：`OutputDesc::generateMotionVector` 默认 `true`，保持 V3.0 行为；JSON 显式 false 时 `Runtime::encodeMotionVectors` 跳过该 output。`vfx.motionVectors` program、VS/FS 与 scene MV buffer 不变。
- 二进制兼容：`.vfxb` v6 OUTPUTS 32B 记录不扩；byte 22 表示“禁用 MV”（0/旧资产 = 启用，1 = 跳过），byte 21 仍是 `castShadows`。旧资产无需重烘焙。
- 编辑器：`Document::setOutputGenerateMotionVector` 修改 raw JSON；Inspector 增加 `Generate motion vectors`，节点摘要显示 `motion on/off`；graphHash 包含该字段。
- 验证：`hbg_vfx_motion_gate_{d3d11,vulkan}` 使用 `shadow_visual`（shadow on + motion off），VFX on/off 的 `--debug-view=motion-vectors` 图要求 0 diff；既有 `hbg_vfx_motion_vectors` 覆盖 gate on；CTest 137。
- 限制：Unity 缺省 false，本地缺省 true 以兼容 V3.0；未实现 flipbook/逐顶点 MV 或 soft-particle 边缘裁剪。

## 16. V3.3 effect transform motion history

- 边界：只改 runtime motion history 和固定 `vfx.motionVectors` VS；`.vfx.json`、`.vfxb` v6、attribute layout、`encodeOutput`/`encodeShadow` 不改。world-space system 仍用 identity。
- 数据流：`EffectImpl` 保存 `m_transform`/`m_previousTransform`。每帧第一次 `setTransform` 提交历史：首帧 previous=当前 world，之后 previous=上一帧 current；`update()` 清除本帧提交标记。`encodeMotionVectors` 懒查找 `u_vfx_prevWorld`，local system 上传 previous transform，world system 上传 identity；VS 用 `u_vfx_prevWorld` 变换 `oldPosition + billboardOffset`，再用 `u_global_prevViewProj`。
- 首帧/重置：构造时 previous=desc.transform；`reset()` previous=当前并失效历史；首帧/重置帧不会产生虚假 MV。
- 验证：`hbg_vfx_runtime_tests` 用 `curve_visual.vfxb`（local）捕获 `u_vfx_world`/`u_vfx_prevWorld`，验证 1/1 → 2/1 → 3/2，以及 reset 后 4/4；`hbg_vfx_motion_vectors`/`motion_gate` 双后端继续通过。
- 限制：上一帧 billboard offset 仍用当前 size/angle，尺寸/旋转突变时近似；没有 Unity `elementToVFXBufferPrevious` 的 per-particle 矩阵；单帧多次 `setTransform` 只保留第一次历史。

## 17. V3.4 automatic bounds

- Boundary: only `.vfx.json` `bounds.mode:"automatic"` and the SYSTEMS reserved byte 60 change; `.vfxb` v6 record size/version and attribute layout stay unchanged. Manual assets keep byte 0 and existing behavior.
- Data flow: compiler adds `bounds_init` (write `0xffffffff`/`0` sentinels) and `bounds` (64 threads, alive + position/size/scale radius, FloatFlip atomic min/max) after Update for automatic systems. The runtime reads the buffer asynchronously through `Device` (Hbg: `bgfx::read` + sentinel polling), decodes six words into a local AABB, and `refreshBounds()` applies the effect transform for local systems.
- Latency: a new readback is requested only after the prior copy is ready; CPU-visible bounds trail simulation by roughly two to three frames. Until the first valid readback, `AssetSystem::bounds` remains the fallback; `EffectDesc::hasBoundsOverride` still wins.
- Editor: system Inspector has `Bounds mode` (Manual/Automatic); automatic hides center/extents and shows the GPU readback note plus `Bounds padding` (V3.5). `setSystemBounds()` forces manual mode.
- Verification: `auto_bounds.vfx.json`/`.vfxb` is the 30th checked-in asset. Runtime mock readback + local transform, compiler kernels/binary mode, and D3D11/Vulkan `hbg_vfx_reference_auto_bounds` (64 particles, 10 frames) pass; CTest 137.
- Limits: V3.4 used a conservative 3D scale radius; V3.13 specializes it to a flat-billboard 2D radius. V3.20 adds camera-aware tight extents but still no tight OBB using Unity axis/angle/pivot; two extra dispatches per automatic system. Unity forces `CullNone` for automatic bounds; local automatic effects still simulate while off-screen. V3.5 adds padding, V3.6 adds manual/recorded culling flags.

## 18. V3.5 automatic bounds padding

- Boundary: JSON `bounds.padding` is optional; `.vfxb` v6 SYSTEMS and OUTPUTS layout are unchanged. A new optional `BOUNDS` section (id 11) stores `u32 count + count * 3*f32`; it is omitted when every system's padding is zero, so existing asset bytes are unaffected.
- Semantics: only `BoundsMode::Automatic` applies padding, matching Unity `VFXBasicInitialize` where Manual maps `bounds` and Automatic/Recorded map `boundsPadding`. `EffectImpl::refreshBounds()` expands the readback (or fallback) AABB before local-to-world transform. Negative values are accepted; extreme values may invert the AABB.
- Editor: automatic systems expose `Bounds padding`; `Document::setSystemBoundsPadding()` persists it, and `setSystemBounds()` preserves existing padding when switching back to manual.
- Verification: `hbg_vfx_tests` JSON/editor round-trip; `hbg_vfx_runtime_tests` mock readback + padding + local transform; `hbg_vfx_compiler_tests` compiled/parsed padding and optional BOUNDS section; V3.5 logs showed padded bounds (`z` approx `±0.173` → `±0.923`), and V3.13 narrows the radius to `z` approx `±0.1414` → `±0.8914`; CTest 137.
- Limits: padding is a scalar AABB expansion; no tight OBB. V3.13 tightens the per-particle flat-billboard radius, and V3.20 tightens its AABB extents from camera basis/angle/scale.

## 19. V3.6 culling flags

- Boundary: asset-level JSON `culling` (`none`/`simulation`/`visible`) maps to `CullSimulation`/`CullBoundsUpdate`. A new optional `CULLING` section (id 12, `u32 flags`) is written only for non-zero flags; existing assets and `.vfxb` v6 layout are unchanged. Automatic bounds reject culling and the editor clears it when switching to automatic.
- Visibility: `ForwardRenderer::updateVfx()` tests each effect's world `Effect::bounds()` against camera frustum planes and calls `Effect::setCulled()`. `CullSimulation` suppresses `update()` time/spawn accumulation and `encodeSimulation()` dispatches; culled effects also skip output and motion-vector draws. `Effect::isCulled()` reports the camera result.
- Default: flags default to `none`; all 30 existing assets keep simulating, and output/MV draws are skipped only when the effect is frustum-culled. Shadow requests remain independent and are not merged with camera culling.
- Verification: runtime mock test covers simulation suppression/restoration and output suppression; parser/compiler/editor tests cover flags, automatic rejection, editor round-trip and `CULLING` BINARY round-trip. Reference auto-bounds/motion tests stay green on D3D11/Vulkan; CTest 137.
- Limits: culling is asset-level, not per-output; shadow path is independent.

## 20. V3.7 output sort priority

- Boundary: output JSON accepts an optional int32 `sortPriority`; `.vfxb` v6 reuses the previously reserved OUTPUTS offset 28. No section or version change. Non-zero values enter graphHash; zero is omitted and preserves old asset bytes.
- Runtime: `EffectImpl` builds a per-effect `m_outputOrder` from all output programs and stable-sorts by ascending priority. `encodeOutput`, `encodeMotionVectors`, and `encodeShadow` iterate this list, so priority orders outputs across systems and applies consistently to all fixed VFX passes. Equal priorities retain compiler/declaration order.
- Editor: `Document::setOutputSortPriority()` persists the field and removes it for 0; Output Inspector exposes `Sort priority`, and non-zero values appear as `order N` in the node summary.
- Limits: per-asset output ordering, not a global VFX/transparent DrawList merge. Particle-internal sorting remains the system sort path. Priorities do not affect simulation, events, bounds, or culling.
- Verification: runtime mock test reverses Emitter/Receiver output order from patched `vfx_event.vfxb` priorities; parser/compiler/editor tests cover int32 parsing, BINARY offset 28 round-trip, and editor persistence. D3D11/Vulkan force/order/off goldens, all 30 assets, and CTest 137 remain green.

## 21. V3.8 camera depth and youngest sort criteria

- Boundary: JSON `sort.mode` accepts `cameraDepth` and `youngest`; `.vfxb` v6 reuses the existing SYSTEMS/OUTPUTS `sortMode` byte with new enum values 3/4. No record, section, or version change; old 0/1/2 values and assets are unchanged.
- Semantics: `cameraDepth` writes `dot(worldPosition - cameraPosition, cameraForward)`; `youngest` writes `age`. The existing ascending/descending KVP comparator and deterministic particle-index tie-break apply unchanged, matching Unity `VFX_DEPTH_SORT_KEY` / `VFX_YOUNGEST_SORT_KEY`.
- Runtime: `output_update` binds `u_vfx_world`, `u_global_cameraPos`, and `u_global_invView`; `EffectImpl::setUniforms()` uploads the inverse view for per-camera depth sorting. Local systems transform particle positions to world before computing the key.
- Editor: the System Inspector Sort combo exposes `Camera depth` and `Youngest in front`; `Document::setSystemSort()` writes the same mode to every output.
- Limits: all outputs share one system KVP/indirection; V3.10 adds per-output criteria via serial re-sort, not concurrent lists. VFX output is still not merged into the scene transparent DrawList.
- Verification: `hbg_vfx_tests` 95/95 (38,249 assertions) and `hbg_vfx_compiler_tests` 27/27 (559 assertions, 168.51s) cover both modes and BINARY round-trip; GPU `--vfx-reference` with 1024 particles passes on D3D11 and Vulkan for both modes; `hbg_vfx_asset_check` leaves all 30 assets byte-identical; existing distance/custom goldens remain green.

## 22. V3.9 per-output sort direction

- Boundary: JSON output `sortOrder` (`ascending`/`descending`) is an optional override of the system sort order. Omitted = inherit. `.vfxb` v6 uses the existing OUTPUTS offset 11 byte, so record size and version are unchanged; old assets remain byte-identical.
- Semantics: the system sort pass still produces one KVP/indirection list. Each output compares its effective order with `system.sort.order`; different order means drawing the shared list backwards. Color output VS reverses `drawParticle` using `RenderCount`; MV/shadow pass set reverse flags in existing `u_vfxMotionParams.z` / `u_vfxShadowParams.w`.
- Runtime/editor: `RenderCount` is promoted to `StorageVertex`; `EffectImpl::m_outputOrder` continues to control output submission order. `Document::setOutputSortOrder()` persists/removes the override; System sort changes preserve explicit output overrides.
- Limits: V3.9 only covered direction; V3.10 adds per-output criteria via serial re-sort, but still no concurrent independent sorted lists. Reverse also reverses equal-key particle-index ties.
- Verification: `hbg_vfx_tests` 96/96 (38,262 assertions), compiler 28/28 (575 assertions), runtime 25/25 (576 assertions); temporary reverse asset bakes and `--vfx-reference` passes on D3D11/Vulkan; 30 assets byte-identical; editor compile/smoke and order/force/motion/shadow goldens pass; CTest 137.

## 23. V3.10 per-output sort criteria

- Boundary: JSON output `sort` may select a criterion different from `system.sort.mode`. `.vfxb` v6 keeps one KVP/indirection and unchanged buffers/sections; compiler emits extra `SortReset` (task 16) + output-update/sort kernels only for distinct extra criteria. Existing assets are byte-identical.
- Compiler: extra pipelines are tagged through KERNELS reserved byte 3; OUTPUTS offset 10 stores the effective output criterion. When extras exist, base and extra output-update shaders use `u_vfxSortWorld`/`u_vfxSortCameraPos`/`u_vfxSortInvView` so sort compute and color/MV raster passes do not set the same uniforms in one bgfx encoder.
- Runtime: `EffectImpl::ensureOutputSort()` walks `m_outputOrder`; when the next output criterion differs, it dispatches reset + output update + sort + fillIndirect into the shared buffers, then draws. Consecutive outputs with the same criterion reuse the list. Color/MV/shadow all call it before their draw; shadow uses the last simulation camera frame.
- Editor: Output Inspector exposes `Sort criterion`; `Document::setOutputSortMode()` writes `output.sort`; changing the system criterion updates outputs that still follow the old system criterion and preserves explicit overrides.
- Limits: V3.12 stores an independent sorted indirection per extra criterion while sharing KVP/RenderCount; different criteria are still sorted serially, and there is no concurrent GPU sort. Only sorted outputs are supported (no per-output unsorted mode). Assets containing task 16 are rejected by older runtimes.
- Verification: `hbg_vfx_tests` 98/98 (38,286 assertions), compiler 29/29 (599 assertions), runtime 26/26 (589 assertions); `sort_outputs` is the 31st asset and bakes dxbc+spirv; D3D11/Vulkan `hbg_vfx_sort_outputs_{d3d11,vulkan}` smoke passes; `vfxbake --check` passes for all 31 assets; CTest 139.

## 24. V3.11 output draw instancing

- Boundary: each output uses one indirect draw with up to 2048 particles per draw instance, matching Unity's quad `k_QuadParticlesPerDrawInstance` and `instanceID * 2048` output indexing. The project keeps its existing 6-vertex quad.
- Compiler: `fill_indirect` writes `numVertices = 6 * min(count, 2048)` and `numInstances = ceil(count / 2048)`. Output and shared MV/shadow vertex shaders compute `linearParticle = vertexID / 6 + gl_InstanceID * 2048`; they always bind stage 14 `RenderCount` and collapse out-of-range particles in the last partial instance to a degenerate vertex.
- Runtime: no draw API change; bindings come from the generated vertex program, and all color/MV/shadow passes consume the same indirect args. `.vfxb` section/record layout remains v6; shader and fillIndirect changes require rebaking all checked-in assets.
- Limits: output draw chunking only; multiple scene instances still own separate simulations and buffers. No 4-vertex indexed quad or instance split/property split yet. The tail chunk may submit up to 2047 invalid particles that the VS discards. Unity only splits indexed quads above 16384; this v1 always splits at 2048, which changes draw-instance count but not pixels.
- Verification: compiler 30/30 (614 assertions), runtime 26/26 (592 assertions); `reference_sorted_8192` passes on D3D11/Vulkan with indirect `[12288, 4]` and `reference_sorted_262144` passes with 128 instances; a temporary 2560-particle partial asset passes with `[12288, 2]`; sort/MV/shadow smokes pass; old HEAD 8192 asset (1 instance mapping) and current 4-instance asset render byte-identical 640×360 screenshots on D3D11/Vulkan; asset check 31 assets 292.82s; editor roundtrip 31 assets 553.02s; CTest 139.

## 25. V3.12 per-output sorted indirection lists

- Boundary: V3.10/V3.11 reused one indirection for every output criterion. V3.12 gives each extra criterion its own sorted indirection buffer while keeping the existing KVP, RenderCount, indirect args, compiler output, and binary format.
- Runtime: `EffectImpl` allocates `vfx.<system>.sorted.<mode>` (`StorageVertex`, capacity × 4) for each unique extra criterion. V3.12 `outputIndirection()` selected the base indirection or the matching extra list; V3.14 replaces it with `outputBuffers()` and adds the criterion-specific KVP/RenderCount/indirect set.
- Sort flow: extra pipelines reset/recount and write their criterion state; V3.12 kept the shared KVP/RenderCount, while V3.14 isolates them. `m_sortGeneration` advances on simulation; an extra list is sorted once on first use per generation. Outputs with the same criterion share the list, and color/MV/shadow reuse it without re-sorting.
- Fixture: `sort_outputs` is distance → youngest → distance, so the third draw proves the simulation's base list remains valid after the youngest extra sort. Runtime tests observe stage 13, stage 14, and draw args base → youngest → base and zero extra MV sorts.
- Limits: lists are per criterion, not per output; paused camera changes reuse the previous distance/cameraDepth list; different criteria are sorted serially. V3.14 adds an independent KVP/RenderCount/indirect set per extra criterion; no concurrent GPU sort yet.
- Verification: runtime 26/26 (628 assertions after V3.14); `hbg_vfx_tests` 98/98 (38,286), compiler 30/30 (616), adapter 8/8 (328); sort/MV/shadow D3D11/Vulkan smokes pass; asset check 31 assets 472.10s; editor roundtrip 31 assets 636.04s; CTest 139.

## 26. V3.13 tight billboard automatic bounds

- Boundary: only the automatic-bounds per-particle radius changes; `.vfxb` v6 sections/records, bounds mode, readback path, and API stay unchanged.
- Codegen: `makeBoundsSource()` uses `0.5 * abs(size) * length(scale.xy)` instead of `length(scale)`. The quad output offsets only use `scale.x`/`scale.y`, so `scale.z` must not inflate the bounds sphere. This follows Unity `VFXBoundsUtils.hlsl:52-59` `ComputeBoundingRadius2DFromMatrix()` (`0.5*sqrt(xAxisSqrLength + yAxisSqrLength)`).
- Assets: `auto_bounds` sets `scale=[1,1,4]` in its init so the GPU readback exercises a non-unit z scale; only `auto_bounds.vfxb` is rebaked, and the other 30 checked-in assets remain byte-identical under `vfxbake --check`.
- Verification: compiler 30/30 (616 assertions) checks the generated source contains `length(scale.xy)` and not `length(scale)`; runtime 26/26 (605); `hbg_vfx_tests` 98/98 (38,286); D3D11/Vulkan `hbg_vfx_reference_auto_bounds` pass; D3D11 readback reports `min=(-1.3821943,-0.6414214,-0.8914214)` / `max=(1.3291512,0.6414214,0.8914214)` with `scale.z=4`, confirming z is excluded from the `0.1414` billboard radius; CTest 139.
- Limits: flat billboard only; mesh/3D outputs, Unity axis/angle/pivot tight extents, and OBB remain deferred. Bounds are still a conservative AABB readback, and padding remains axis-aligned.

## 27. V3.14 per-output sort state

- Boundary: V3.12 shared the base KVP, RenderCount, and indirect args across every criterion. V3.14 gives each extra criterion its own KVP, RenderCount, and indirect args; `.vfxb` v6, compiler output, shaders, and assets stay unchanged.
- Runtime: `SortOutputRuntime` now owns `indirection`, `kvp`, `renderCount`, and `indirect`. `SortBufferSet` selects the base set or the criterion's set for compute and vertex bindings.
- Sort flow: `ensureOutputSort` dispatches the extra reset/output-update/sort kernels with the criterion's KVP/count/indirection and dispatches `fillIndirect` with the criterion's args/count/KVP. Color, MV, and shadow bind stage 13/14 and draw from the same set. The base list and args are no longer overwritten by an extra sort.
- Verification: runtime 26/26 (628 assertions) observes stage 13, stage 14, and draw-indirect handles in base → youngest → base order; extra sort leaves the base state intact and MV does not re-sort. `hbg_vfx_tests` 98/98 (38,286), compiler 30/30 (616), adapter 8/8 (328); 12 D3D11/Vulkan sort/reference/shadow/motion smokes pass; CTest 139.
- Limits: state is per unique criterion, not per output; extra criteria still sort serially on first use; scratch stays shared; no concurrent GPU sort or per-output KVP. New runtime buffers are not serialized into `.vfxb`.

## 28. V3.15 effect simulation instancing v1

- Boundary: compatible scene instances of one asset share one GPU simulation and its buffers; each instance keeps its own transform, bounds, culling state, and output/MV/shadow draw. The feature is opt-in through `PipelineSettings::supportVFXInstancing` and `--vfx-instancing`.
- Reference: Unity `VFXInstancing.hlsl:1-115` defines current/active/batch indices and `VFXInitInstancing()`; `VFXDataParticle.cs:918-920,1505-1509` allocates per-system instancing indirection/prefix-sum state; `OutputPass.cs:300-312` marks systems with `SystemUsesInstancedRendering` and instance split descriptions.
- Runtime: `EffectImpl` owns `InstanceRuntime` entries with current/previous world, local bounds, and cull state. Simulation runs once for the group; `encodeOutput`, `encodeMotionVectors`, and `encodeShadow` loop visible instances and set the matching uniforms. MV uses each instance's previous world; shadow ignores camera culling.
- Grouping: `ForwardRenderer::ensureVfx` groups consecutive enabled scene instances by asset path, seed, playRate, playOnAwake, and identical property lists when the created effect reports `supportsEffectInstancing()`. The group keeps the first instance as the primary `Effect` descriptor.
- Eligibility: only all-local-space systems whose output sort modes are `None` or `YoungestInFront`; `distance`, `cameraDepth`, and `custom` remain one-effect-per-instance because their sort keys depend on the instance transform.
- Verification: runtime 29/29 (671 assertions) covers multi-instance transforms/bounds/draws, per-instance MV history, and world-space rejection. Both D3D11 and Vulkan `hbg_vfx_instancing` tests observe effects 2 → 1 with two instances and byte-identical screenshots. 32 feature goldens, 12 sort/reference/shadow/motion smokes, and dual-backend auto-bounds pass; CTest 141.
- Limits: v1 shares simulation/buffers; V3.17 extends the all-visible case to one instanced draw per output. There is no instance split, property/texture split, prefix-sum batch, active/batch indirection, or GPU culling compaction. Grouping is fixed at scene initialization and hot reload does not re-evaluate it. Effect clocks must match; incompatible instances fall back to separate effects.

## 29. V3.16 VFX transparent effect-level ordering v1

- Boundary: opt-in `PipelineSettings::supportVFXTransparentSort` / `--vfx-transparent-sort` places each VFX effect as one transparent batch at its effect-level depth instead of always drawing all VFX before the scene transparent DrawList. Disabled behavior remains V1 D8 (VFX output before transparent).
- Reference: HDRP `HDRenderPipeline.cs:3460-3499` uses `SortingCriteria.CommonTransparent | SortingCriteria.RendererPriority` for transparent RendererLists; `HDRenderPipeline.RenderGraph.cs:955-965,1899-1911` creates and draws the forward transparent list. Unity's VFX internal particle order remains `VFXSortingUtility.cs:9-56` / `VFXParticleCommon.template:337-365`.
- Runtime: `Runtime::beginOutputFrame()` resets output diagnostics/timing; `Runtime::encodeOutputEffect()` encodes one effect. `ForwardRenderer` builds an effect key from `Effect::bounds()` center, transparent queue 3000, and `kTransparentSortingCriteria`, then inserts per-effect VFX passes among sliced transparent items.
- Renderer: `DrawList::buildRange()` makes transient transparent sublists kept in `m_vfxTransparentSegments`; `addTransparentFramePasses()` is shared by forward and deferred. The sort path is skipped for pre-refraction, transparent backface, empty transparent lists, or disabled settings. GPU-driven transparent falls back to CPU segmented draws when this opt-in is active.
- Ordering: far VFX effect → farther transparent items → nearer VFX effect → nearer transparent items. Each VFX effect still sorts its outputs/particles internally; transparent scene items retain their sorted order within each segment.
- Verification: new `vfx_transparent_order` fixture has a transparent panel at z=1.4 and `curve_unlit` effects at z=0.5 and z=2.0. D3D11/Vulkan `hbg_vfx_transparent_sort_*` verify the off/on order difference and golden; off/on D3D11 differs by 4,680 pixels (max delta 81), while the two backend on goldens match. Existing 32 goldens, 16 references, 11 shadow/motion/sort smokes, runtime 29/29 (671), `hbg_vfx_tests` 98/98 (38,286), adapter 8/8 (328), compiler 30/30 (616), `hbg_tests` 309/33,443 pass; CTest 143.
- Limits: effect-level key only; particles do not interleave with transparent meshes or other effects. V3.24 refines this to active-instance-batch keys when `vfxInstanceBatchSize > 0`; independent per-particle insertion is still absent. The key is an approximate bounds-center depth and uses the default transparent queue; pre-refraction/backface/GPU-driven paths are not merged. Segmenting can increase pass count.

## 30. V3.17 effect GPU instancing v2: instanced draw v1

- Boundary: when the V3.15 group has more than one visible instance and a valid per-effect transform buffer, each output is submitted as one indirect draw whose `numInstances` is `particleChunks * effectInstances`. If any instance is culled, or the group has one instance, runtime falls back to the V3.15 per-instance draw loop. No new pipeline setting or CLI flag.
- Reference: Unity `VFXInstancing.hlsl:1-115` handles fixed/variable-size current/active/batch indices; `VFXOutputUpdate.template:147-152` and `ParticlePlanarPrimitives/PassVS.template:113-142` construct and apply the per-instance element-to-VFX matrix; `OutputPass.cs:300-312` marks `SystemUsesInstancedRendering` and emits split descriptions. hdrp-bgfx replaces Unity's per-instance property/index buffers with one effect-level current/previous transform buffer.
- Data: `EffectImpl` creates `vfx.instance.transforms` only for >1 instances. Each entry is 32 floats: current `Mat4` followed by previous `Mat4`. `u_vfxInstanceParams.x` stores the effect instance count; `fill_indirect` multiplies the particle chunk count by it. Generated output VS and the external MV/shadow VS split `gl_InstanceID` into effect instance and particle chunk, then compute world position from the raw matrix columns (matching the CPU `Mat4` layout). N=1 and fallback paths retain `u_vfx_world`/`u_vfx_prevWorld`.
- Descriptor handling: data buffers are bound at stage 15; for N=1 the effect attributes buffer is bound as a valid descriptor fallback because the shader still declares the storage binding. This keeps Vulkan descriptor validation stable while the branch remains inactive.
- Verification: runtime 30/30 (699 assertions) covers batched color/MV and shadow draws, stage-15 binding, transform buffer contents, and cull fallback. The `vfx_instancing` fixture now rotates/scales the second instance; D3D11/Vulkan off/on color and motion-vector screenshots are 0 diff, and observed VFX draw count drops 4 → 2. Compiler 30/30 (622), `hbg_vfx_tests` 98/98 (38,286), adapter 8/8 (328), `hbg_tests` 315/33,505, all 31 assets pass `vfxbake --check`; selected 62 VFX CTest cases pass, CTest total 144.
- Limits: V3.17 batching is all-or-nothing per effect and never compacts culled instances on GPU; V3.18 adds active instance indirection for partial culling. It supports only the V3.15 local-space/none-or-youngest eligibility set; no property/texture split, prefix-sum variable-size batch, batch indirection, or instanced output draw with per-instance sort keys. Groups and instance clocks are fixed at scene initialization. Indirect args are generated during simulation, so cull changes while paused do not regenerate them.

## 31. V3.18 effect GPU instancing v3: active instance indirection

- Boundary: V3.15/V3.17 still define the instanced effect group. V3.18 keeps the same opt-in and fixed-size draw submission, but adds a CPU-built active instance list so a group with more than one visible instance can render with one indirect draw even when some instances were frustum-culled. Zero visible instances skip color/MV; one visible instance keeps the V3.15 per-instance fallback. Shadow ignores camera culling and always draws the full instance set.
- Reference: Unity `VFXInstancing.hlsl:37-54` derives the current instance from the draw/dispatch index for fixed-size batches, and `:69-80` maps current -> active with `instancingIndirectAndActiveIndirect[instancingActiveIndirectOffset + instanceCurrentIndex]`; `VFXCodeGenerator.cs:962-988` emits active/batch indirection defines and `VFXDataParticle.cs:1505` allocates the indirection buffer. Unity's output path supplies `_InstanceActiveIndex`/`_InstanceIndex` through per-instance properties; hdrp-bgfx stores the equivalent effect-level mapping in the stage-15 buffer.
- Data: `vfx.instance.transforms` is `N + ceil(N/32)` 32-float slots. The first N slots keep current/previous `Mat4`; trailing slots hold active instance indices. `u_vfxInstanceParams` is `{totalInstances, activeInstances, activeBase, activeIndirection}`. `fill_indirect` still uses `totalInstances`, so partial batches submit the same indirect args and shader slots beyond `activeInstances` degenerate without reading attributes.
- Shader: generated output VS and `vfx_motion_vectors.vs.sc` resolve `instanceActive` and `effectInstance` before loading attributes; active instances read the stage-15 active list, while full batches and fallback paths keep the direct draw-instance index. MV uses the same active transform for current and previous positions.
- Runtime: `refreshDrawInstancing()` rebuilds `m_activeInstances` from per-instance cull flags and selects output batch/active-indirection state; `uploadInstanceActiveList()` writes only the active range at `activeBase` after cull changes. `encodeShadow()` sets the active flag to zero and draws all N instances.
- Verification: runtime 31/31 (725 assertions) covers a three-instance partial cull (one draw, active list `[0,2]`, total/active/base/flag uniforms) and shadow cull still batching. The new `vfx_instancing_cull` fixture has an off-screen third instance; D3D11/Vulkan off/on color and motion-vector screenshots are 0 diff, off `vfx_draws=4`, on `vfx_draws=2`. Compiler 30/30 (626), 90 VFX CTest cases, 31 assets, and CTest total 146.
- Limits: the active list is CPU-owned and rebuilt from cull flags; V3.18 still submits total-sized args and inactive slots run the vertex shader as degenerate output. V3.19 adds active-count indirect args; neither version has GPU cull compaction. There is no batch indirection when the batch is smaller than active, instance split, property/texture split, or prefix-sum variable-size batch. Shadow does not consult the active list, and the transform buffer carries one extra slot. Eligibility remains local-space with output sort `none`/`youngest`.

## 32. V3.19 effect GPU instancing v4: active-count indirect args

- Boundary: V3.15/V3.17 still define the instanced group and V3.18 defines the active list. V3.19 narrows color/MV submission to the active instance count while preserving full-batch and single-visible fallback behavior. Shadow always keeps total-sized args. No new pipeline setting or CLI flag.
- Reference: Unity `VFXInstancing.hlsl:10-12` exposes current/active/batch sizes; `:29-31` describes the active→batch and split current→active indirection sections, and `:81-92` maps an active index into the batch when the batch is smaller than active. `VFXCodeGenerator.cs:984-988` enables active/batch indirection for non-output tasks and `VFXDataParticle.cs:1505` allocates the buffer. With no instance split, batch order equals active order, so hdrp-bgfx only needs the V3.18 active list as batch→current mapping plus an active-sized args buffer.
- Data: `prepareActiveIndirect()` lazily creates an `.active.indirect` buffer per system and per extra sort list. When `0 < active < total`, it reuses the generated `fill_indirect` kernel with `u_vfxInstanceParams.x = active` and a `SortBufferSet` whose indirect target is the active buffer. `activeArgsSerial` avoids more than one fill per list until cull/instance state changes.
- Submission: color/MV choose active args when `m_activeArgsRequired` is set; full batches use the simulation-generated total args, and shadow never uses active args. The output/MV shader source is unchanged: partial batches still read the active list for batch→current, while one visible instance keeps the `u_vfx_world` fallback but now receives `particleChunks * 1` instead of `particleChunks * total`.
- Verification: runtime 31/31 (743 assertions) covers partial fill count 2, single-visible fill count 1, full-batch original args with no fill dispatch, and shadow original args. `vfx_instancing_cull` (third instance off-screen) and `vfx_instancing_cull_gap` (middle instance off-screen, active `[0,2]`) both match ungrouped color and MV screenshots on D3D11/Vulkan, with `vfx_draws=4` off and `2` on. Compiler 30/30 (626), `hbg_vfx_tests` 98/98 (38,286), adapter 8/8 (328), `hbg_tests` 315/33,525, 31 assets, 90 VFX CTest cases, CTest total 146.
- Limits: active args are one fixed-size batch; there is no split/batch-smaller-than-active active→batch indirection, property/texture split, or prefix-sum variable-size batch. The active list remains CPU-owned, buffers are allocated on first partial/visible-one use, and there is no GPU cull compaction. Shadow still ignores active state.
## 33. V3.20 tight billboard automatic bounds v2: camera-aware AABB

- Boundary: V3.13 keeps the automatic-bounds pipeline, padding, readback, and host AABB culling. V3.20 only replaces the per-particle 2D radius with oriented extents for the camera-facing billboard. No new asset, CLI, pipeline setting, buffer, or readback format.
- Reference: Unity `Shaders/VFXBoundsUtils.hlsl:73-85` computes tight extents from the element matrix columns; `:52-56` provides the 2D bounding radius fallback. hdrp-bgfx's billboard uses `u_global_invView[0/1]` as camera right/up plus `angle`; V3.20 applies the same matrix-extents idea in local billboard axes.
- Shader: `makeBoundsSource()` reads `angle` (0 when absent) and, when `u_vfxBoundsParams.x > 0.5`, builds rotated `billboardRight/Up` and computes `extents = 0.5*|size|*(|right|*|scale.x| + |up|*|scale.y|)`. The V3.13 `radius = 0.5*|size|*length(scale.xy)` path remains the fallback.
- Runtime: automatic-bounds systems upload `u_vfxBoundsParams={1,0,0,0}` and `FrameContext::cameraRight/Up` during simulation; non-automatic systems do not receive the extra uniforms. Only `auto_bounds.vfxb` was rebaked because the bounds shader is embedded per automatic system.
- Verification: runtime 31/31 (751 assertions) checks the camera uniforms on automatic simulation plus existing readback/padding; compiler 30/30 (631) checks the generated bounds source and fallback. New `run_vfx_auto_bounds.cmake` asserts D3D11/Vulkan reference bounds valid and tight y/z, with min `(...,-0.6,-0.75)` and max `(...,0.6,0.75)`. 90 VFX CTest, 98/98 VFX tests, 8/8 adapter, 318/33,587 hbg tests, 31 assets, CTest 146.
- Limits: still AABB, not an oriented box; no Unity axis/angle/pivot OBB, mesh/3D extents, or OBB padding. Camera changes while paused do not dispatch new bounds; readback remains asynchronous. Multi-instance groups share local bounds, but the extents use the same local billboard axes as the output shader.

## 34. V3.21 cull-aware visible instance bounds

- Boundary: V3.16 keeps effect-level transparent ordering and V3.15/V3.18 keep grouped simulation/active-list rendering. V3.21 only changes the bounds used as that effect-level sort key. No shader, asset, buffer, CLI, or pipeline-setting change.
- Reference: Unity `Shaders/VFXBoundsUtils.hlsl:150-168` writes per-instance bounds with `instanceIndex * 6`, and `Editor/Data/VFXDataParticle.cs:928-938` marks automatic bounds on the system. V3.21 follows the same per-instance bounds semantics on the CPU: only instances that pass culling contribute to the visible aggregate.
- API: `Effect::visibleBounds()` returns the union of valid `instanceBounds(i)` for `!culled` instances, or an invalid bounds when none remain. `Effect::bounds()` remains the all-instance union for compatibility and diagnostics.
- Runtime: `refreshVisibleBounds()` is called from `refreshBounds()` and from both cull setters. It caches `m_visibleBounds`; it does not add a buffer or synchronization point. `refreshBounds()` still updates per-instance transformed bounds and automatic-bounds readback exactly as before.
- Renderer: `ForwardRenderer::addTransparentFramePasses()` reads `visibleBounds()` for the transparent sort center and falls back to the primary instance position when the visible bounds are invalid. The default VFX pass path and V3.16 segment building are unchanged.
- Verification: runtime 31/31 (761 assertions) checks all-visible bounds, one-culled bounds `[2,4]`, all-culled invalid, and restored bounds. Existing transparent-sort CTests and the 90-case VFX suite remain unchanged. 98/98 VFX tests, 8/8 adapter, 318/33,587 hbg tests, 31 assets, CTest 147.
- Limits: still one effect-level key per grouped effect, not per-instance/per-particle interleaving with scene transparent meshes. Shadow keeps all instances, and there is no GPU culling compaction. Cull changes while paused are consumed on the next `updateVfx`.

## 35. V3.22 fixed-size active instance split

- Boundary: V3.18 owns the active list and V3.19 owns active-sized args. V3.22 adds opt-in fixed-size batch slicing of that active list for color/MV; it does not add instance/property/texture splits, variable-size prefix sums, or GPU culling compaction.
- Reference: Unity `Shaders/VFXInstancing.hlsl:10-12` exposes current/active/batch counts, `:29-31` describes active→batch and current→active indirection, and `:81-92` maps an active index into a smaller batch. `Editor/Compiler/VFXCodeGenerator.cs:984-988` enables the indirection and `Editor/Data/VFXDataParticle.cs:1505` allocates the buffer. Because hdrp-bgfx has no instance split, batch order equals active order, so the active→batch mapping is `activeBase + batchOffset` instead of a separate buffer.
- Configuration: `EffectDesc::instanceBatchLimit` (0 = no split) is propagated from `PipelineSettings::vfxInstanceBatchSize` / `--vfx-instance-batch=<n>` and JSON key `vfxInstanceBatchSize`. Values are clamped to 0–65535.
- Runtime: `refreshDrawInstancing()` computes `m_outputBatchSize = min(limit, active)` and `m_outputBatchCount = ceil(active / m_outputBatchSize)`. `BatchIndirectRuntime` keeps one `.active.indirect[.N]` per batch; `prepareActiveIndirect()` fills each with the existing `fill_indirect` using that batch's local instance count. Extra buffers are retained when the batch count shrinks and released with the effect.
- Submission: color/MV loop over batches; each batch sets `u_vfxInstanceParams = {totalInstances, localInstances, activeBase + batchOffset, 1}` and uses its own indirect args. The shader source is unchanged: the active list still maps batch-local index to the current instance. `u_vfx_world` remains the primary-instance fallback for the one-instance path.
- Shadow: V3.22 itself keeps one total-sized shadow batch; V3.23 extends the same limit to shadow total-instance batches without reading the active list.
- Verification: runtime 32/32 (792 assertions) checks 3 active instances with limit 2 yielding args `{3,2,96,1}` / `{3,1,98,1}`, and a culled middle instance reducing the active list to `[0,2]` while preserving offset mapping. The instancing fixture with limit 1 emits 4 draws (2 outputs × 2 batches) and remains 0 diff to the ungrouped color/MV screenshots on D3D11/Vulkan; both cull fixtures also remain 0 diff with limit 1, including the non-contiguous active list `[0,2]`. VFX tests 98/98, adapter 8/8, compiler 30/30 (507), hbg tests 323/33,687, CTest 148. No asset/shader change; asset check and the full 90-case suite were not rerun.
- Limits: fixed-size contiguous batches only; no property/texture split, prefix-sum variable-size batch, separate active→batch buffer, or GPU cull compaction. Shrunk batch counts retain unused args buffers until effect destruction. `instanceBatchLimit` is creation-time only and has no runtime setter.

## 36. V3.23 fixed-size shadow instance split

- Boundary: V3.22 slices color/MV active instances. V3.23 applies the same `instanceBatchLimit` to shadow, but batches the total instance list because shadow must keep drawing camera-culled instances. No active-list read, camera-cull change, asset, or new setting.
- Reference: Unity `Shaders/VFXInstancing.hlsl:5,10-17` exposes current/active/batch counts and `instancingCurrentOffset`; `:37-54` adds that offset when resolving a fixed-size current instance; `:94-103` feeds the same indices into `VFXInitInstancing()`. `Editor/Compiler/VFXCodeGenerator.cs:185-186` emits the instance indices at task entry. hdrp-bgfx shadow has no active indirection, so only the current-instance offset applies.
- Configuration: reuses `EffectDesc::instanceBatchLimit` / `PipelineSettings::vfxInstanceBatchSize` / `--vfx-instance-batch=<n>`; 0 keeps one total-sized shadow draw.
- Runtime: `refreshDrawInstancing()` computes `m_shadowBatchSize` and `m_shadowBatchCount` from total instance count, independently of the active batch. `SystemRuntime::shadowIndirect` holds `.shadow.indirect[.N]`; `prepareShadowIndirect()` lazily creates and fills each total batch with `fill_indirect`, caching by `m_shadowArgsSerial`. Extra buffers are retained and released with the effect.
- Submission: `encodeShadow()` loops total batches, sets `u_vfxInstanceParams = {totalInstances, localInstances, batchOffset, 0}`, and uses the matching args. `vfx_shadow.vs.sc` adds `u_vfxInstanceParams.z` to the resolved instance index; the color/MV shader path is unchanged.
- Verification: runtime 33/33 (814 assertions) checks 3 instances with limit 2 producing two shadow draws and params `{3,2,0,0}` / `{3,1,2,0}`, cull invariance, args buffers, and zero live buffers after destroy. The shadow shader compiles for DXBC and SPIR-V. A manual D3D11/Vulkan split smoke on three local shadow instances with limit 1 produced `vfx_shadow_draws=4` vs `12` and byte-identical directional shadow atlases; the shared manifest needed temporary color-pyramid placeholders because of an unrelated parallel edit. At that time VFX was 98/98, adapter 8/8, compiler 30/30 (507), CTest 148, and `hbg_tests` 323/33,687; V3.24 later ran with the restored manifest/config.
- Limits: shadow batches are total-instance contiguous slices; camera culling never removes shadow instances. No GPU cull compaction, shadow active list, property/texture split, prefix-sum batch, or independent active→batch buffer. `instanceBatchLimit` remains creation-time only.

## 37. V3.24 VFX transparent per-batch insertion

- Boundary: V3.16 inserts one VFX pass per effect at the effect-level `visibleBounds()` key. V3.24 only refines that key when `supportVFXTransparentSort` and a non-zero `vfxInstanceBatchSize` are both active: each V3.22 active instance batch becomes its own transparent submission, and each submission draws only that batch through its existing `.active.indirect[.N]` args. `0` keeps the V3.16 path unchanged; no new CLI/JSON/asset field and no shader change.
- Reference: HDRP `Runtime/RenderPipeline/HDRenderPipeline.cs:3460-3475` builds transparent RendererLists with `SortingCriteria.CommonTransparent | RendererPriority`; `HDRenderPipeline.RenderGraph.cs:1872,1902` sends all transparent forward categories through the same sorted list. Unity `Shaders/VFXInstancing.hlsl:5,10-17,37-54` supplies the fixed-size current offset used by V3.22/V3.23. hdrp-bgfx applies the same transparent key to the active batch instead of the whole effect.
- API: `Effect::isInstanceCulled(index)`, `outputBatchCount()`, and `outputBatchSize()` expose the cull/batch state the renderer already owns; `Runtime::encodeOutputEffectBatch(sink, frame, effect, batchIndex)` encodes one prepared batch. `encodeOutput()` now calls a shared `encodeOutputDraws()` so full-effect and single-batch submission use identical output/soft/lit/buffer binding code.
- Renderer: `addTransparentFramePasses()` walks active instances in order (skipping `isInstanceCulled`), groups by `outputBatchSize()`, unions each group's `instanceBounds()`, and creates a submission with that union-center key. Each submission becomes a `VFX Output N Batch B` pass at its sorted transparent cursor; segment insertion is unchanged. The log reports `effects=<unique> batches=<submissions> segments=<segments>`.
- Unchanged paths: VFX internal particle KVP/indirection sorting, simulation, motion-vector submission, shadows, pre-refraction, transparent backface, and GPU-driven transparent fallback. MV still runs after all transparent segments; shadow still draws every instance.
- Verification: runtime 34/34 (843 assertions) covers a 3-instance limit-1 effect: `outputBatchCount()==3`, batch 1 uses `vfx.CurveVisual.active.indirect.1` with `{3,1,97,1}`, culling the middle instance yields 2 batches over active `[0,2]`, and destroy leaves zero live buffers. New `hbg_vfx_transparent_batch_sort_d3d11/vulkan`: 3 grouped `curve_unlit` instances at z=0.5/1.0/2.0 plus a transparent panel at z=1.4; effect-level logs `batches=1`, `vfx_draws=2`, batch mode logs `batches=3`, `vfx_draws=6`, and the two orders differ by 4,680 px / max delta 81 on both backends. Per-backend goldens pass; cross-backend is max 1 px/delta 1. Existing transparent-sort, instancing/cull, and shadow suites pass; VFX CTest 106/106 after rerunning transient/missing-target failures, runtime 34/34 (843), VFX tests 98/98, compiler 30/30 (507), adapter 8/8, `hbg_tests` 325/33,719, CTest 150.
- Limits: interleaving is per batch, not per particle; with batch size >1 transparent objects cannot split a batch. Keys use the batch union bounds center, not per-particle depth. Pre-refraction/backface/GPU-driven transparent paths remain effect-level. Splitting increases pass/draw count and CPU submission; the default `--vfx-instance-batch=0` avoids it. Shadow and MV are never split by this feature.
