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
- VFX output 始终在 transparent 之前，因此不与场景透明物体交错（D8）。透明 DrawList 使用 `kTransparentSortingCriteria`（sorting layer、render queue、renderer priority、sorting order、back-to-front、state），VFX 内部粒子继续使用各自的 KVP key（distance/camera depth/youngest/custom）。
- 并入 DrawList 的成本：VFX 是间接 6 顶点 draw，不是 mesh/submesh batch；要按粒子深度与透明物体统一排序，需要把 VFX 拆成逐粒子 draw、CPU 读回全量粒子深度，或在 DrawList 增加 indirect batch 及 effect-level 深度 key。V1 保留 pass 级顺序并记录 D8；后续优先评估 effect-level key，而不是逐粒子 CPU 排序。
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
