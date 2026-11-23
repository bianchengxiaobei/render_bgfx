# VFX 源码对齐清单

> 目的：每个实现任务都能追溯到原项目的行为/数据布局依据。
> 版本：当前 checkout（2026-11-20）。行号可能随上游变化，实现时用 `rg` 重新确认。
> 路径：
> - Unity 包：`E:\Learn\Graphics\Packages\com.unity.visualeffectgraph`
> - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition`
> - hdrp-bgfx：`E:\Learn\hdrp-bgfx`

---

## 1. 调度与输出

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| 线程组大小 | `Editor/NewCompiler/VfxDispatch.cs:11-21` | `ThreadsPerGroup = 64`；capacity 向上对齐到整组 | `vfx/src/compiler/codegen.cpp`（`groupCount`）；所有 kernel `NUM_THREADS(64,1,1)` |
| quad 每实例粒子数 | `Editor/NewCompiler/Passes/VfxTemplateCodeGenerationPass.cs:12-15,554` | 每 draw instance 2048 粒子；`linearThreadIndex = (id >> 2) + instanceID * 2048` | `vfx/src/compiler/codegen.cpp`（output VS/FS + fillIndirect）；V1 偏差见 §5 |
| quad 顶点展开 | `Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58` | `index = id/4`；UV 由 `id & 3`；offset = uv - 0.5 | `shaders/vfx/vfx_quad.vs.sc` |
| old fill indirect（quads） | `Shaders/VFXFillIndirectArgs.compute:58-70` | 索引绘制：`numIndices = count*6`；超过 16384 按 2048 分实例 | `vfx/src/compiler/codegen.cpp`（`fill_indirect` kernel；V1 非索引 6 顶点，见 §5） |
| output update 注册排序 | `Shaders/Temp/Templates/OutputUpdate.hlsl:14-33` | alive 才注册 `SortingKVP(key,index)`；V1.2.2 写 distance/custom key、KVP 与顺序 indirection | `vfx/src/compiler/codegen.cpp`（`makeOutputUpdateSource`） |
| output 顶点取粒子 | `Shaders/Temp/Templates/Output.hlsl:4-60`、`Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58` | 通过 `RenderingDataBinding.GetRenderingIndex` 取 indirection | `vfx/src/compiler/codegen.cpp`（`makeOutputVertexSource`）+ `s_vfxIndirection` |
| 渲染计数/indirect | `Editor/NewCompiler/Data/RenderingData.cs:7-26,79-90`；`Editor/NewCompiler/Data/DescriptionWriter/RenderingDataDescriptionWriter.cs:54-96` | `RenderingData` 含 KVP counter/keys；`DrawIndirectionData` 含 counter + index buffer | `.vfxb` BUFFERS（9 条）+ 全局 `vfx.events`；runtime buffers |

## 2. 粒子数据、属性、生命周期

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| attribute bucket 布局 | `Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103` | 每个 group 一个 bucket；`offset/stride`；`GetBufferSize = lastOffset + capacity*lastSize`；capacity 按 64 对齐 | `vfx/src/compiler/attribute_layout.cpp`（`vfx::buildAttributeLayout`，单 group；V1.0b 默认含全部内置 + 自定义属性） |
| attribute 分组 | `Editor/NewCompiler/Passes/AttributeLayoutPass.cs:30-115` | 根据 task 读写亲和性分组；粒子 capacity 对齐 | V1 单 group；V1.1 可加亲和性分组（见 §5） |
| attribute 声明/读写 | `Editor/GraphCommon/Data/DescriptionWriter/AttributeDataDescriptionWriter.cs:6-43`；`Shaders/Temp/Data/AttributeBuffer.hlsl:8-29`；`Shaders/Temp/Data/ByteAddressBuffer.hlsl:9-120` | 生成 `VFX_ATTRIBUTE_DECLARE`；raw byte address 的 Load/Store，支持 1/2/3/4 分量 | `vfx/src/compiler/attribute_layout.cpp:242`（`generateAttributeShader`，`Load_*`/`Store_*` 等价）；`shaders/vfx/vfx_attributes.sc` |
| 每实例切片 | `Editor/NewCompiler/Data/DescriptionWriter/ParticleSystemDataDescriptionWriter.cs:61-82` | `particleBuffer.Init(attrBuffer, instanceIndex * attributeBufferSize, attributeBufferSize)`；deadList counter/copy/free 布局 | `vfx/src/runtime/runtime.cpp`（EffectImpl；V1 无实例切片） |
| dead list | `Shaders/Temp/Data/DeadListData.hlsl:6-31`；`Shaders/VFXCopyBuffer.compute:63-70` | `counter` 原子分配；`counterCopy` 上限；free list `size - index - 1`；计数器初值 `size` | `vfx/src/compiler/codegen.cpp`（dead_list_init kernel） |
| init 生命周期 | `Shaders/Temp/Templates/Init.hlsl:10-42` | 读 spawnCount/event list；`NewParticle`；写属性 | `vfx/src/compiler/codegen.cpp`（spawner/init kernel） |
| update 生命周期 | `Shaders/Temp/Templates/Update.hlsl:10-29` | 读 alive；处理 subtasks；写回；死亡 `DeleteParticle` | `vfx/src/compiler/codegen.cpp`（update kernel） |
| capacity/间接缓冲名 | `Editor/Data/VFXDataParticle.cs:224-247` | `indirectBuffer`/`sortedIndirectBuffer`；默认 capacity 128 | `.vfxb` system 字段 |
| deadList/AABB buffer desc | `Editor/Data/VFXDataParticle.cs:898-900,729-743` | deadList `capacity+2, stride 4`；AABB `stride 24` | `vfx/src/runtime/runtime.cpp`（buffer 创建/reset） |
| 排序缓冲 | `Editor/Data/VFXDataParticle.cs:1185-1206,1489` | `VFXSortBufferA/B`、`VFXScratchSortBuffer`；KVP stride 8 | V1.2.2 `.vfxb` renderKVP（kind 7）+ sortScratch（kind 8）+ indirection；KVP/sortScratch 在 bgfx 适配层用 StorageVertex |
| instancing 缓冲 | `Editor/Data/VFXDataParticle.cs:1505-1509` | instance indirection/prefix sum（V1 不做） | V2+ |

## 3. 事件、排序、前缀和

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| CPU 事件列表 | `Shaders/Temp/Data/EventListData.hlsl:8-35` | events prefix sum + spawn count prefix sum；二分查找属性索引 | V1.2.4 `Effect::sendEvent` pending queue + `vfx.cpu.events` staging；`event_cpu_upload` 复制到全局 event buffer 后统一 prefix/init |
| GPU 事件列表 | `Shaders/Temp/Data/EventListData.hlsl:37-84` | `elementCount`/`totalCount`/`indexBuffer` 原子 append；`baseEventCount` | V1.2.3 `makeUpdateSource` append；`makeEventInitSource` 读 frameCount/prefix；全局 `vfx.events` |
| 事件生产者 | `Editor/NewCompiler/Builder/GpuEventBuilder.cs`；`ParticleSystemBuilder.cs:565-580` | `AppendEvents(eventCount, particleIndex)`；`baseEventCount` 用于 particleId | `vfx/src/compiler/codegen.cpp`（`makeUpdateSource` 的 auto-death/trigger_event append）；`CompiledEventLink` |
| sort KVP | `Shaders/Sort.compute:65-120` | `{float key; uint value;}`；key 相等按 value 升序；bitonic + merge | V1.2.2 `makeSortBitonicSource`/`makeSortMergeSource`；distance/custom key、ascending/descending、tie-break |
| bitonic kernel 尺寸 | `Shaders/Sort.compute:1-17` | 128/1024/2048/4096 + merge；不同线程数变体 | V1 先覆盖 128/1024/2048/4096；更大走 merge |
| prefix sum | `Shaders/VFXPrefixSum.compute:27-143` | Sklansky；1024/128 两种线程数；instancing 支持 | `makeEventPrefixSource`（128 threads，单 effect，每 link 一个 frameCount/prefix）；V1 不做 instancing |
| fill indirect args | `Shaders/VFXFillIndirectArgs.compute:31-120` | 每元素一个 uint 计数；quads/points/lines/meshes 分支 | `vfx/src/compiler/codegen.cpp`（fill_indirect kernel） |
| copy/zero/init dead list | `Shaders/VFXCopyBuffer.compute:40-86` | copy dword、zero init、dead list 初始化、bounds 初始化 | `vfx/src/runtime/runtime.cpp`（reset clear；deadListInit kernel） |
| 引擎内置 shader 绑定 | `Editor/Inspector/VFXManagerEditor.cs:43-47,184-188` | VFXManager 持有 Indirect/Copy/PrefixSum/Sort/StripUpdate compute | 适配层不再走 VFXManager；由 VFX runtime 直接持有 program |

## 4. 编译、资产、SRP

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| 资产编译入口 | `Editor/Models/VFXGraph.cs:156-206`；`Editor/NewCompiler/VfxGraphCompiler.cs:30-48` | 引擎 importer 回调 `OnCompileResource` → graph 编译 → `VisualEffectAssetDesc`；编译 pass 顺序为 Output/AttributeLayout/DataLayout/... | `vfx/src/core/asset_json.cpp`（JSON → `vfx::Graph`）；`vfx/src/compiler/{codegen,compiler,binary}.cpp`（layout → kernel → shaderc → `.vfxb`）；`vfxbake` CLI 已实现 |
| 表达式 DAG/类型/折叠 | `VFXExpressionGraph.cs:59-113`；`VFXExpressionAbstract.cs:389-510`；`VFXExpressionMath.cs:9-604`；`VFXExpressionRandom.cs:38-104`；`VFXExpressionNoise.cs:9-203`；`VFXExpressionSampleCurve.cs:9-33` | 表达式图归约、逐通道运算、常量折叠、`Rand(attributes.seed)`/`SampleCurve`/noise codegen；类型含 float/floatN/int/uint/bool | `vfx/src/compiler/expression.cpp`（`compileExpression`/折叠/DAG 检测）；`vfx/include/vfx/expression.h`；parser 在 `asset_json.cpp:189-258` 校验；V1.0b.3 只生成宏表达式 |
| shader 创建 | `Editor/Models/VFXGraph.cs:1529-1535` | `ShaderUtil.CreateComputeShaderAsset/CreateShaderAsset` | `vfx/src/compiler/compiler.cpp` 调 `shaderc` 生成 dxbc/spirv blob；同源缓存；失败带 shaderc 输出 |
| 新编译器 Pass 顺序 | `Editor/NewCompiler/VfxGraphCompiler.cs:33-48` | Output/AttributeLayout/DataLayout/ParticleSystem/Spawner/BufferConversion/SystemTaskBindings/CodeGen/Lowering/Instancing | `vfx/src/compiler/codegen.cpp`（V1.0b.5 裁剪：layout→program、buffer；V1.2.2 sort、V1.2.3 GPU event、V1.2.4 CPU event、V1.3.1 parse 阶段 subgraph flatten、V1.3.2 custom HLSL、V1.4.1 lit、V1.4.2 soft particles）；`vfx/src/compiler/binary.cpp` 序列化 binary v6 |
| SRP 抽象 | `Editor/Core/VFXSRPBinder.cs:12-42` | 每 SRP 提供模板/兼容性/材质设置 | `vfx` 的 `ShadingProvider` + hdrp-bgfx shading pack |
| ShaderGraph VFX target | `Editor/ShaderGraph/VFXSubTarget.cs`（按需复核） | VFX 专用 target/pragma/keyword | 不复用；直接生成 `.sc` + lighting pack |
| 默认 shader 模板 | `Shaders/Temp/Templates/*.hlsl`、`Shaders/Temp/Data/*.hlsl` | init/update/output/fill 的模板化入口 | `shaders/vfx/*.sc` 重写 |
| Lit 输出参考 | `Editor/ShaderGraph/Templates/VFXConfig*.template.hlsl`；HDRP lit pass | surface/varying/lighting 组合 | hdrp-bgfx `shaders/lighting/light_loop.sc` |

## 5. 计划中的偏差（必须保留在 checklist）

| ID | 偏差 | 原项目 | 本项目 V1 | 原因 / 关闭条件 |
|---|---|---|---|---|
| D1 | quad 绘制拓扑 | 索引绘制 6 index/quad（`VFXFillIndirectArgs.compute:60-68`） | 非索引 **6 顶点/粒子**（`vertexID / 6`，无 index buffer） | V0 确认 4 顶点非索引无法组成两个三角形；6 顶点与原 6 index 语义等价。bgfx `submitIndirect` 无 16-bit index 限制；减少 index buffer |
| D2 | indirect 分块 | quads >16384 才按 2048 分实例 | V1 单条 indirect draw，`numVertices = 6 * renderCount`（renderCount 等于 alive） | V0 已用 6*alive 验证；后续在 V1.4 评估大粒子数/驱动限制后再分块 |
| D3 | attribute 分组 | 读写亲和性分组（`AttributeLayoutPass.cs:30-115`） | V1 单 group（顺序 offset）；V1.0b 默认布局含全部内置属性 + 顶层自定义属性 | 先保证正确性；V1.1 若 profiling 显示 bucket 收益明显再加；后续按 block 读写集合收窄 |
| D4 | fill indirect | 引擎 VFXManager 用 `VFXFillIndirectArgs.compute` + CPU draw desc（`VFXManagerEditor.cs:184`） | runtime 直接生成 args（简化 kernel） | 自有 runtime 无 Unity 引擎的 draw desc 结构；V1 只需 quad |
| D5 | effect instancing | 多实例共享仿真 + instance split（`VFXDataParticle.cs:1505-1509`） | 每 effect 独立 buffer | 降低 V1 复杂度；V2+ 再加 |
| D6 | 自动 bounds | GPU AABB + readback | 手动 bounds + 编译期 spawner bounds | V1 不做 readback；后续加 GPU AABB |
| D7 | 自定义 HLSL | Unity 允许较完整 HLSL/子图 | 受限 `.sc` 语句片段；`VFX_ATTR`/`VFX_SET_ATTR`/`VFX_PROP` 宏；不可声明 buffer/uniform/texture | 保证双后端可编译与可移植性 |
| D8 | 渲染 pass 排序 | VFX 透明与场景透明物体统一排序 | VFX 输出 pass 位于 `Forward Transparent` 之前，透明 mesh 不与粒子逐对象交错 | V1 先保证顺序稳定；并入 DrawList 需要 VFX indirect batch、effect-level 深度键或逐粒子 CPU 深度键；V1.4.3 已评估成本，留到后续 |
| D9 | random seed 状态 | `Rand(attributes.seed)` 每求值推进粒子 seed（`VFXCommon.hlsl:465`） | V1.0b.3 固定 seed 的确定性 `VFX_RAND`；暂不推进 seed | V1.0b.5 仍使用固定 seed 的确定性 code；seed 推进/每节点 hash 留到 V1.1 生成器完善时关闭 |
| D10 | 粒子质量 | Drag/Force/Turbulence 使用 `mass` 属性并除以 mass | V1.1.2 固定 `mass=1.0`；没有 mass 属性/Set Mass | 避免为未使用的属性扩大全部 asset layout；增加 mass block 后关闭 |
| D11 | Turbulence 噪声 | `VFXNoise.hlsl` 的 Value/Perlin/Cellular curl + float cell hash；`FieldTransform` 变换 | 仅 Value curl；整数 Wang cell hash；无 FieldTransform；`scroll` 为扩展的 field 时间偏移 | 原 float hash 在 CPU/GPU 乘加重排下会超过 1e-3；整数 hash 保证双后端确定性。后续需要时再补 Perlin/Cellular/FieldTransform |
| D12 | Force space | Unity 力向量在 system 空间求值（没有显式 space slot） | 保留 `space` 字段但当前忽略，统一按 system 空间处理 | 避免在 compute 阶段引入未上传的 local→world 矩阵；后续加入 world transform 时关闭 |
| D13 | 碰撞半径/事件/阈值 | `CollisionBase` 支持 radiusMode/FromSize/Custom、BounceSpeedThreshold、LifetimeLoss、roughSurface、碰撞事件；sphere 支持 TSphere transform/椭球 | 当前 schema 只提供 normal/distance 或 center/radius、mode/restitution/friction；粒子半径 0；不写碰撞事件；sphere 固定为 system 空间球 | 先完成 CPU/GPU 一致的解析平面/球和三种响应；后续增加 radius/事件/transform 字段时关闭 |
| D14 | Over-life 组合/采样模式 | `ColorOverLife` 默认 ColorAndAlpha + Multiply，支持 Color/Alpha composition；`AttributeFromCurve` 支持 OverLife/BySpeed/Random/Custom 与 composition | `color_over_life`/`alpha_over_life`/`size_over_life` 固定按 `age/lifetime` 覆盖 RGB/alpha/size；无 composition、BySpeed/Random/Custom | 保持 schema、CPU reference 与 GPU 三处语义一致；后续扩展 block 字段时关闭 |
| D15 | Curve/Gradient LUT 格式 | Unity 使用 baked texture + curve metadata（128 宽）、wrap mode 和可选随机/按速度采样 | 每条 property 256×1 RGBA16F，运行时按类型打包 256×N，双线性/clamp；不支持 wrap mode/元数据 | 简化确定性、绑定和 CPU reference；后续需要时扩展 LUT 头和采样模式 |
| D16 | GPU event payload | Unity event list 只存 source index，consumer 按 source attribute buffer 读取 | V1.2.3 在 emit 时复制最多 4 个属性到 vec4 slots；index 仍保留 | 解耦 producer/consumer 属性布局，代价是 uint/int/bool 走 float 精度；后续可改回 source-index 绑定 |
| D17 | GPU event 范围 | Unity 支持 instancing prefix sum 与多层事件图 | V1.2.3 单 effect、单层 producer→consumer、receiver 不能有 CPU spawner、总 event buffer ≤64 KiB | V1 先保证正确性/双后端；V1.3 子图/事件链和分块后关闭 |
| D18 | event 诊断 | Unity/后续 `vfx_events` 统计精确事件数 | V1.2.3/4 `Diagnostics::eventCount` 为事件链数；精确 `frameCount/totalCount/dropped` 在 event buffer，reference 读回 | V1.4.4 帧诊断暴露 `vfx_events`（事件链数）；精确计数仍走 reference readback，默认帧循环不读回以避免 GPU stall（V2 异步计数环） |
| D19 | CPU event 属性/上限 | Unity 支持 event attribute 集合/多 spawner | V1.2.4 每 receive 一个 payload（≤4 属性），缺省值取 consumer 属性默认，类型不兼容拒绝；容量 = receiver 64 对齐 capacity，超出记 dropped | V1 保持单层/单 payload；V1.3+ 扩展属性集/多事件 |

## 6. HDRP 对齐

| 主题 | 参考 | 行为 | 本项目落点 |
|---|---|---|---|
| VFX camera 准备 | `Runtime/RenderPipeline/HDRenderPipeline.cs:1706-1711` | `VFXManager.PrepareCamera` | 适配层 `FrameContext`（view/proj/camera） |
| 体积雾 CB | `Runtime/RenderPipeline/HDRenderPipeline.cs:3044` | `PrepareAndPushVolumetricCBufferForVFXUpdate` | V1 不做 VFX fog；V2+ |
| VFX 渲染命令 | `Runtime/RenderPipeline/HDRenderPipeline.cs:3049-3054` | `VFXManager.ProcessCameraCommand` | `VfxRenderer::encodeSimulation/encodeOutput` + RenderGraph |
| camera buffers | `Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs:1932-1970,2050-2100` | 按需绑定 depth/normal/color | V1 输出 pass 显式 `readTexture(depth)`；color 作为 attachment |
| VFX fog voxelization | `Runtime/Lighting/VolumetricLighting/HDRenderPipeline.VolumetricLighting.cs:765,1141-1204` | `s_VolumetricFogVFXName` pass | V1 不做 |
| 透明门控 | `Runtime/RenderPipeline/Settings/FrameSettings.cs:107,137-138` | `TransparentObjects = 3` | hdrp-bgfx `src/render/pipeline/frame_settings.h:60` 同 bit |

## 7. hdrp-bgfx 对齐

| 主题 | 参考 | 行为 | 本项目落点 |
|---|---|---|---|
| storage buffer 绑定 | `src/gfx/encoder.h:49-52`；`src/gfx/encoder.cpp:241-274` | `bgfx::setBuffer`；offset 256B 对齐；stage < 16 | runtime buffer bind；按 baseIndex 而非 offset 切片 |
| indirect submit | `src/gfx/encoder.h:53-54`；`src/gfx/encoder.cpp:282-297` | `bgfx::submit(view, program, IndirectBufferHandle, start, num)` | `CommandSink::drawIndirect` |
| compute dispatch | `src/gfx/encoder.h:55-58`；`src/gfx/encoder.cpp:299-324` | `dispatch`/`dispatchIndirect` | `CommandSink::dispatch` |
| indirect 32B | `src/gfx/buffer.cpp:132-138,192-196` | `BGFX_CONFIG_DRAW_INDIRECT_STRIDE = 32`；`createIndirectBuffer(count)` | `.vfxb` args 写入 8×uint |
| storage 类型 | `src/gfx/buffer.h` `BufferType::Storage/StorageVertex/Indirect` | `StorageVertex` 可被 VS/CS 读 | attribute buffer 用 `StorageVertex` |
| Backend 接口 | `docs/vfx/backend-api.md:28-182`；`src/gfx/resource_manager.h`；`src/gfx/encoder.h:49-58` | opaque handle；VFX 库不出现 bgfx/hbg 类型 | `vfx/include/vfx/device.h`、`command_sink.h`、`shading.h` |
| Backend 适配 | `src/gfx/resource_manager.h:37-260`；`src/gfx/encoder.h:30-61`；`src/render/vfx/vfx_spike.cpp:648-655` | buffer/texture/program/uniform 创建；encoder 绑定；dummy 6 顶点 | `src/render/vfx/hbg_vfx_backend.{h,cpp}`（`hbg_vfx_adapter`） |
| Mock/资源测试 | `tests/render/test_render_graph.cpp` | doctest + 资源生命周期 | `vfx/tests/support/mock_backend.h`、`vfx/tests/test_vfx_backend.cpp`、`vfx/tests/test_vfx_hbg_backend.cpp` |
| 编译器/二进制测试 | `tests/render/test_render_graph.cpp`（doctest 模式） | kernel plan、section 布局、确定性、`--check` | `vfx/tests/test_vfx_compiler.cpp`、`vfx/tests/assets/minimal_unlit.vfxb` |
| RenderGraph buffer | `src/render/graph/render_graph.h:85-91,120-121` | `readBuffer/writeBuffer`、compute/raster pass、`allowCulling` | 适配层 pass 声明（V1.0b.7 接线） |
| 前向 pass | `src/render/passes/forward_renderer.cpp:4828-4880` | color attachment + depth read/attachment + light/shadow 绑定 | VFX 输出 pass 复用同一资源 |
| 帧顺序（forward） | `src/render/passes/forward_renderer.cpp:4670-4682` | Sky → Contact → BakedGI → Forward Opaque → Transparent | 仿真插在 Sky 前/后、输出插在 Opaque 与 Transparent 之间 |
| 帧顺序（deferred） | `src/render/passes/forward_renderer.cpp:4731-4756` | Sky/Decal/GBuffer/Contact/Lighting/SSR → ForwardOnly → Transparent | 输出插在 Deferred Lighting 后、ForwardOnly/Transparent 前 |
| compute pass 范例 | `src/render/passes/forward_renderer.cpp:3273-3500,3551` | `addComputePass` + `setStorageBuffer` + `dispatch` | VFX 仿真 pass 模板 |
| 光照循环 | `shaders/lighting/light_loop.sc:339` | `hbgEvaluateLightLoop(SurfaceData, viewWS, worldPosition, ...)` | VFX Lit FS 直接调用 |
| SurfaceData | `shaders/material/surface_data_types.sc:6` | baseColor/alpha/metallic/roughness/normal 等 | VFX Lit FS 填充 |
| 前向光照 stage | `src/render/passes/forward_renderer.cpp:6270-6310`；`shaders/lighting/shadows.sc:7-18`；`shaders/lighting/environment.sc:6-7` | 0-4 material；5 light data；6 directional shadow；7 light list；8 shadow context；9 punctual；10 environment；11 contact；12/13 point/area depth；14 probe maps；15 cookie atlas | VFX Lit 复用光照资源；VFX 变体关闭 point/area sampler，12/13 留给 attribute/indirection buffer |
| shader 后端 | `tools/shader_build.py:49-51` | dxbc `s_5_0`；spirv | `vfx/src/compiler/compiler.cpp` 调同一 shaderc 参数；`vfxbake` 入口 |
| 绑定上限 | `build/vs2022/_deps/bgfx-src/src/config.h:498-500`；`src/bgfx_p.h:211` | 16 samplers / 16 compute bindings | 绑定预算表 |
| 场景 sidecar | `src/asset/scene_loader.cpp:1316-1345,1479-1488` | `.hdrp.json` 解析/加载 | 新增 `"vfx"` 数组 |
| 测试模式 | `tests/render/test_contact_shadow.cpp` | doctest + 参数/资源测试 | `vfx/tests/`；adapter `vfx/tests/test_vfx_hbg_backend.cpp` |

**额外约束（V0/V1 都必须遵守）**：D3D11 的 texture/buffer SRV 共用 `m_textureStage.m_srv[stage]`（`build/vs2022/_deps/bgfx-src/src/renderer_d3d11.cpp:8018-8108`），所以同一 draw/dispatch 内 **buffer stage 与 texture stage 不能重叠**。V0 分配见 `architecture.md` §4.4：compute buffer 0–8 / texture 9–15；raster attributes/indirection buffer 12–13 + shadow context 8，texture 0–7/9–11/14–15。VFX lit 变体必须**不定义** `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`，否则 point/area shadow sampler 会占用 12/13。

**V0 额外发现**：D3D11 的 bgfx draw 路径只有在 `streamMask != 0` 时才提交；`submitIndirect` 仅提供 args、不提供 vertex stream 时不会产生 draw。VFX 输出必须绑定一个 dummy vertex buffer（V0 用 6 顶点）或等价的非空 stream。`startVertex/startInstance` 在 V0 args 中固定为 0；后续若使用非零值，需要单独截图验证。

## 8. Runtime 对齐（V1.0b.6）

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| `.vfxb` 读取 | 编辑器 description writer / buffer desc | 校验 magic/version/hash/section/双后端，按 backend 选 blob | `vfx/src/runtime/runtime_asset.cpp:472` |
| effect 资源 | `ParticleSystemDataDescriptionWriter.cs:61-82` | 每 effect 独立 attribute/deadList/indirection/renderCount/spawnCount/indirect/bounds/renderKVP/sortScratch buffer | `vfx/src/runtime/runtime.cpp` |
| dead list 初始化 | `Shaders/Temp/Data/DeadListData.hlsl:6-31` | 初始化 counter=capacity、free list 逆序；reset 时重新 dispatch | generated `dead_list_init`; `runtime.cpp:781` |
| 常量/Burst spawn | `Shaders/Temp/Templates/Init.hlsl:10-42`；`VFXSpawnerState` 行为；`VFXSpawnerConstantRate.cs`/`VFXSpawnerBurst.cs` | double 累积 rate + 单次/周期 Burst（period/repeat、同帧合并、1e-6 边界），GPU spawner 写 spawnCount；CPU reference 同步语义 | `runtime.cpp` `burstOccurrences`/`update`；`reference.cpp` `updateSpawner` |
| 力场 | `Gravity.cs:39-45`；`Drag.cs:50-64`；`Force.cs:61-67`；`ForceHelper.cs:32-40`；`Turbulence.cs:67-78`；`VFXNoise.hlsl:44-76` | Gravity/Drag（含 useSize）/Force（absolute/relative）/Attractor/Turbulence；`mass=1`；Turbulence 默认 Relative、Value curl noise、scroll=VFX_TIME 偏移；CPU reference 与 GPU 相同顺序和参数 | `codegen.cpp` `compileUpdateBlock`；`cpu_reference.cpp` `applyUpdateBlock`；`tests/assets/forces_1024.vfx.json` | 
| 碰撞 | `CollisionPlane.cs:34-56`；`CollisionSphere.cs:55-159`；`CollisionBase.cs:214-269` | 平面 signed distance / 球射线求交；inside 投影到表面；bounce/friction；kill/bounce/stick | `codegen.cpp` `compileUpdateBlock`/`makeCollisionResponseCode`；`cpu_reference.cpp` `intersectPlane`/`intersectSphere`；`tests/assets/collision_1024.vfx.json` |
| simulation 顺序 | `Init.hlsl`/`Update.hlsl`/`OutputUpdate.hlsl`/`RenderingData` | spawner→init→update→outputUpdate→fillIndirect，reset 时加 deadListInit | `runtime.cpp:781` |
| output | `Output.hlsl:4-60`；`VFXConfigPlanarPrimitive.template.hlsl:13-58` | 绑定 attribute/indirection/texture，按 output blend 状态 indirect draw | `runtime.cpp:861` |
| local transform | Unity local-space system | output VS `u_vfx_world`；local 用 effect transform，world 用 identity | `codegen.cpp:1059,1086` |
| Curve/Gradient | `ColorOverLife.cs:93`；`AttributeFromCurve.cs:332-358`；`SampleCurve.cs:7-27`；`SampleGradient.cs:7-27`；`VFXCommon.hlsl:536-558` | 每个 property 256×1 RGBA16F LUT；同类型打包 `256×N`；compute stage 9/10 双线性采样；over-life 按 `age/lifetime` | `binary.cpp:265-296`；`codegen.cpp:186/195,415,1196,1251`；`runtime.cpp:778,931-997`；`cpu_reference.cpp:526-605` |
| 资源生命周期 | `VFXDataParticle.cs` buffer 创建/释放 | destroyEffect 释放 effect 自建 buffer/program/texture；外部 texture 不释放 | `runtime.cpp` `releaseResources` |


## 9. VFX 集成（V1.0b.7）

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| VFX 实例 sidecar | HDRP `HDRenderPipeline.RenderGraph.cs:1932-1970,2050-2100`；VFX camera buffer 绑定 | `.hdrp.json` `vfx` 数组 -> scene instances；asset 相对 sidecar 解析 | `src/asset/scene_loader.cpp:610,697,1621`；`src/render/scene/render_scene.h:48` |
| 仿真插入点 | HDRP VFX camera buffer / render graph | sky 后、opaque 前 compute；`allowCulling(false)` | `src/render/passes/forward_renderer.cpp:4834,4912` |
| 输出插入点 | HDRP VFX 透明输出 | opaque 与 transparent 之间；color/depth attachment | `src/render/passes/forward_renderer.cpp:4846,4931` |
| 设置/CLI | Pipeline settings | `supportVFX` 默认 false；`--vfx/--vfx-test/--no-vfx` | `src/render/pipeline/pipeline_settings.{h,cpp}`；`src/app/backend.cpp:666` |
| 诊断 | HDRP frame diagnostics | vfx effects/systems/particles/draws/dispatches | `src/render/passes/forward_renderer.h:142`；`src/app/render_test.cpp:124` |
| 示例 | VFX Graph output | camera-only scene + minimal_unlit | `vfx/tests/assets/vfx_scene.gltf`、`vfx_scene.hdrp.json` |

## 10. VFX reference/golden（V1.0b.8）

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| dead list 分配/回收 | `Shaders/Temp/Data/DeadListData.hlsl:20-38`；`VFXCopyBuffer.compute:63-70` | counter/counterCopy/free list 语义；1024 粒子单次 burst 后 counter=0 | `vfx/src/reference/cpu_reference.cpp`；`src/render/vfx/vfx_reference.cpp` |
| init/update CPU 对照 | `Shaders/Temp/Templates/Init.hlsl:10-42`、`Update.hlsl:10-29` | 复用 Wang hash、block seed、position/velocity/age 公式；固定 seed/dt/帧数 | `vfx/include/vfx/reference.h` |
| GPU readback | Unity `VFXManager`/buffer readback 仅用于工具/测试 | `bgfx::read` attributes/deadList/indirection/renderCount/renderKVP/sortScratch/indirect；sorted order 与 CPU KVP sort 对照；不进入交互循环 | `src/render/vfx/vfx_reference.cpp` |
| golden | 固定相机/种子/帧数截图 | 基础场景 D3D11/Vulkan 640×360、3 帧、逐字节 0-diff；V1.1.5 feature golden 为 640×360、90/30/30/300 帧、每后端 golden（forces 跨后端最大 1 LSB） | `vfx/tests/golden/vfx_scene.png`、`vfx/tests/golden/vfx_{bursts,forces,collision,curves}_*.png`、`vfx/tests/run_vfx_golden.cmake` |
| 资产名 roundtrip | description writer name | header `assetNameOffset` 指回 strings；兼容旧 0 值 | `vfx/src/compiler/binary.cpp`、`vfx/src/runtime/runtime_asset.cpp` |

## 11. 实现时必须重新确认的源码点

- `HDRenderPipeline.RenderGraph.cs` 中 VFX camera buffer 的绑定时机（与我们的 output pass 前后关系）。
- `Sort.compute` 的 merge pass uniform 与 dispatch 组合（V1.2 迁移时逐段对照）。
- `VFXPrefixSum.compute` 的 instancing 宏（V1 先单实例，V1.2 事件多 system 时复核）。
- `VFXConfigPlanarPrimitive.template.hlsl` 的 `ShouldCullElement` / indirection 索引规则（输出 VS 必须一致）。
- `forward_renderer.cpp` 的 `bindShadowImsParams`/`bindShadowMomentSat` stage（Lit 可选路径）。
- `config/uniforms/*.idl` 的生成命名/前缀规则（新增 `vfx.idl` 时）。

## 12. V1.3.3 diagnostics / reload

| Topic | Unity reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Error reporting | `Editor/Models/VFXErrorManager.cs:20-49,68-82,120-145` | Compilation/invalidation reporters collect `ReportError(model,type,error,description)`; compile reporter is cleared/refreshed after compilation. | `vfx::ParseError{path,message,source,line,column,location}` + `formatParseError`; JSON path locator and HLSL line/column in `vfx/src/core/asset_json.cpp` / `custom_hlsl.cpp`. |
| Compile error refresh | `Editor/Models/VFXGraph.cs:156-197,1447-1457` | `OnCompileResource` refreshes compilation report; `Compile()` runs compiler then `errorManager.GenerateErrors()`. | `compileGraph(json, options, sourceName)` fills `CompileResult.errors`; codegen `path: message` errors are annotated with JSON line/column. |
| Hot reload | `Editor/Models/VFXGraph.cs:1664-1674` | `CompileAndUpdateAsset` clears preview assets, recompiles, then replaces `VisualEffectAssetDesc`. | `Runtime::reloadEffect` creates a replacement effect first and destroys the old effect only on success; V1 is destroy/recreate, not incremental graph reload. |
| Asset lifetime | Unity importer owns `VisualEffectAsset`; material resync after compile (`VFXGraph.cs:1685`). | `Runtime::pruneAssets` drops assets no longer referenced by any effect; caller `shared_ptr` remains valid; diagnostics are recomputed. |

## 13. V1.3.4 subgraph/custom HLSL acceptance

| Topic | Unity reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Subgraph flatten | `Editor/Models/Blocks/VFXSubgraphBlock.cs:135-168,196-216` | RecreateCopy copies nested blocks; PatchInputExpressions recursively substitutes inputs and nested subgraph calls. | `vfx/src/core/subgraph.cpp` flattens pure-function outputs and init/update block subgraphs before codegen; `subgraph_visual` combines a pure function (`Tint`) and a block subgraph (`SeedParticles`). |
| Subgraph inputs/outputs | `Editor/Models/Operators/VFXSubgraphOperator.cs:14-34,214-227` | TransferExpressionToParameters and input/output properties remap call arguments to subgraph inputs and select outputs. | `asset_json.cpp` parses `{"op":"subgraph"}` and `{"type":"subgraph"}`; `subgraph_visual` uses input substitution plus nested pure-function call; declarations/nesting/cycles are covered by `test_vfx_asset_json.cpp`. |
| Custom HLSL | `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:11-42,389-447` | Validates void/VFXAttributes/input shape; BuildSource initializes attributes, invokes the function, and writes declared attributes back. | `vfx/src/core/custom_hlsl.cpp` validates `code`/`reads`/`writes`, injects `VFX_ATTR`/`VFX_SET_ATTR`/`VFX_PROP`; `custom_hlsl_visual` writes color/alpha/size from position and bakes dxbc+spirv. |
| Dual-backend golden | Fixed camera/seed/frame screenshots | D3D11/Vulkan renders are compared byte-for-byte per backend. | `vfx/tests/golden/vfx_subgraph_{d3d11,vulkan}.png` and `vfx/tests/golden/vfx_custom_hlsl_{d3d11,vulkan}.png`; both pairs are currently identical. |
| Asset check | Unity importer/reimport keeps baked asset consistent | Recompile all checked-in graphs and compare binary byte-for-byte. | `vfxbake --check vfx/tests/assets` covers 21 assets at V1.3.4, 22 after the V1.4.1 lit fixture, 24 after the V1.4.2 soft fixtures, 27 after the V1.4.4 performance fixtures, and 28 after the V1.4.5 empty-effect fixture; see §14–§18. |

## 14. V1.4.1 lit VFX output

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Lit quad SurfaceData | `Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`; `Shaders/Temp/Templates/Output.hlsl:4-60` | VFX output builds PBR surface from particle color/alpha/size; billboard normal faces camera. | `makeOutputFragmentSource` in `vfx/src/compiler/codegen.cpp` fills `SurfaceData` from `v_color`/`s_vfxMain`, with billboard normal/tangent. |
| Light loop | `shaders/lighting/light_loop.sc:339`; `shaders/lighting/direct_lighting.sc` | Evaluate ambient + directional/punctual/area lights, contact shadow and IBL. | Lit FS includes `lighting/light_loop.sc` and calls `hbgEvaluateLightLoop`; `hbgSampleDirectionalShadow` supplies directional visibility. |
| Forward lighting bindings | `shaders/passes/forward.fs.sc:1-36`; `forward_renderer.cpp:6270-6310` | Pass/global/object/cluster/environment/probe uniforms and stages 5/6/7/8/9/10/11/14/15. | Shared `ForwardLightingContext`/`bindForwardLighting()` in `src/render/passes/forward_lighting.h`; forward draw paths and `HbgVfxBackend::bindLighting` both call it. |
| Stage budget | HDRP forward sampler layout; VFX attribute stage contract | Point/area shadow samplers would occupy 12/13, conflicting with VFX attribute/indirection buffers. | VFX lit variant does not define `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`; 12/13 remain VFX buffers. |
| Material properties | material metallic/roughness semantics | Lit output exposes metallic/roughness with 0/0.5 defaults. | `OutputDesc` accepts 0..1 constants or float property names; property uniforms are uploaded at output time. |
| Golden | Fixed camera/seed/frame screenshots | Compare forward/deferred D3D11/Vulkan lit VFX with a lit mesh and shadow caster. | `vfx/tests/assets/vfx_lit.gltf` + `vfx_lit.hdrp.json`; four goldens; lit tolerance <=4 px, max 1 LSB. |

## 15. V1.4.2 soft particles

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Soft option | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:108,280-343,460-461,573-574` | `useSoftParticle` requires a transparent blend; exposes `softParticleFadeDistance` (min 0.001) and derives inverse fade distance. | `OutputDesc.softParticles`/`softDistance` (default 0.1, min 0.001); `asset_json.cpp` validation and graph hash; `.vfxb` v6 output record. |
| Scene depth | HDRP `Runtime/VFXGraph/Shaders/VFXCommon.hlsl:180-183` | `VFXSampleDepth(posSS)` loads camera depth at screen position. | FS declares `s_vfxSceneDepth` at stage 4; runtime binds `FrameContext::sceneDepth`; missing depth uses white fallback. |
| Fade | `Shaders/VFXCommonOutput.hlsl:108-159` | Perspective uses `LinearEyeDepth`; orthographic has separate path; `fade=saturate(delta/distance)`, then `fade*fade*(3-2*fade)`. | `hbgVfxSoftFade` reconstructs scene world from raw depth and `u_global_invViewProj`, converts both scene/particle to eye depth via `u_global_view`, and applies smoothstep. |
| Varying budget | `ParticlePlanarPrimitives/PassForwardCommon.template:20-21,68`; `VFXParticleCommon.template:15-17` | Per-vertex `invSoftParticlesFadeDistance` is interpolated in `builtInInterpolants.x`. | V1 uses one per-output uniform `u_vfxSoftParams.x` instead of a new varying; `u_vfxSoftParams.yz` stores inverse viewport for depth UV. |
| Read-only depth | HDRP forward transparent prepass keeps depth bound while sampling. | Output pass samples depth while depth attachment remains read-only. | `RenderGraphBuilder::setDepthAttachment(depth, 0, true)` and `FrameBufferKey` read-only-depth flag; D3D DSV uses `BGFX_ATTACHMENT_READ_ONLY_DEPTH\|STENCIL`. |
| Golden | Fixed camera/seed/frame screenshots | Compare soft on/off. | `soft_visual`/`soft_visual_off`, `vfx_soft*.gltf`, four goldens; same-backend repeat 0 diff; D3D/VK soft differs by 721 px max 1 LSB. |

## 16. V1.4.3 frame order and sorting

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Particle sort criteria | `Editor/Models/Contexts/VFXSortingUtility.cs:9-56`, `Editor/NewCompiler/Builder/OutputUpdateTaskBuilder.cs:79-127` | Four criteria: distance to camera, camera depth, youngest in front, custom scalar; per-camera criteria require a view. | V1.2.2 KVP sort already implements distance/custom with deterministic tie-break; V1.4.3 leaves it per output and does not merge scene transparent draws. |
| Output sort activation | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:110-120` | Transparent alpha/premultiplied outputs sort automatically; custom sorting is an output setting. | Alpha/additive outputs use the existing system sort; no new schema field in V1.4.3. |
| Transparent frame order | HDRP `Runtime/RenderPipeline/HDRenderPipeline.cs:3465-3498`, `HDRenderPipeline.RenderGraph.cs:1899-1911` | All transparent forward objects share a renderer list sorted with `CommonTransparent \| RendererPriority` before forward transparent. | Scene transparent DrawList uses `kTransparentSortingCriteria`; VFX output stays before that list and is documented as D8. |
| Lit clustered lighting | HDRP transparent forward uses punctual light lists/clusters. | Lit transparent VFX binds the cluster light list rather than the global fallback. | `prepareVfxClusterLighting` reuses the texture cluster or builds a CPU cluster-texture mirror when the GPU light-list buffer path is active; VFX output reads `clusterLightList`. |
| Frame insertion | HDRP VFX update/output order. | Simulation before opaque/GBuffer; output after deferred lighting/SSR and forward-only opaque, before transparent. | Forward `forward_renderer.cpp:5384/5396`; deferred `:5462/5481`. |
| Golden | Fixed camera/seed/frame screenshots. | Order + transparent + point-light cluster fixture. | `vfx_order.gltf` + `vfx_order.hdrp.json`; forward/deferred D3D11/Vulkan goldens; `vfx_order_off` baseline and no-VFX compare. |

## 17. V1.4.4 diagnostics, performance and resource lifetime

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| CPU effect timing | `Editor/GraphView/Profiling/VFXProfilingBoard.cs:470-485,566-582,605-611` | Profile `FullUpdate`/`EvaluateExpressions` plus per-system recorders; display averaged milliseconds. | `Runtime` records `updateMs`, `simulationMs`, `outputMs`, `encodeMs`, `totalMs`; `render performance` emits `vfx_update_ms`/`vfx_sim_ms`/`vfx_output_ms`/`vfx_ms`. |
| GPU memory/time | `VFXProfilingBoard.cs:617-621` | `VFXManager.GetBatchedEffectInfo` reports GPU size/capacity; GPU recorder aggregates effect time. | V1 exposes `gpu_frame_ms` (sim+draw+sort) and draw/dispatch/sort-pass counts; per-effect GPU timers remain out of scope. |
| Alive/capacity | `Editor/GraphView/Profiling/VFXSystemProfilerUI.cs:200-212,294-301`; `Editor/Debug/VFXUIDebug.cs:882-896` | Show alive count, capacity and efficiency; capacity is the hard particle limit. | `Diagnostics.particles`/`systems`/`effects`; performance fixtures use 10240/102400/1048576 capacities and report `vfx_particles`. |
| Capacity validation | `Editor/Models/Contexts/Implementations/VFXBasicInitialize.cs:92-100`; `Editor/Data/VFXDataParticle.cs:246-247` | Capacity over `VFXManager.maxCapacity` is a compile error; spawned particles beyond capacity are discarded. | JSON/compiler reject `>1048576`; runtime `validateDeviceLimits` rejects invalid capacity before allocating; CPU event overflow records `dropped`. |
| Binding limits | Device capability contract | Shader/binding limits are checked before resource creation. | `validateDeviceLimits` checks current backend blob and compute/vertex/fragment binding stages against `Device::Limits`; error includes program/stage/limit. |
| Effect lifetime | Unity asset/manager owns batched GPU resources | Create/reload/destroy must release programs, buffers and textures; shutdown must be leak-free. | `createEffect` prunes unused assets on failure; `releaseResources` covers destroy/reload/shutdown; tests repeat 16 create/destroy and 4 backend/runtime sessions; `--resize-test` covers full/half size switches. |
| Event diagnostics | Unity `VFXManager` event counters | Exact event counts come from event-buffer counters. | V1.4.4 exposes event link count as `vfx_events`; exact `frameCount/totalCount/dropped` stay in `--vfx-event-reference` readback to avoid a synchronous GPU stall in the default frame loop. |
| Benchmark | Unity profiler CPU/GPU views | 10k/100k/1M quad scaling, sort on/off. | `perf_10k`/`perf_100k` (distance sort), `perf_1m` (sort off due V1 limit), `vfx_perf_*.gltf`, dual-backend perf smoke; 640×360×60 Debug data recorded in `v1-checklist.md`. |

## 18. V1.4.5 release gate

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Empty effect | `Editor/NewCompiler/Passes/OutputPass.cs:229-235` | Spawner systems without tasks are skipped and emit no system/task descriptions. | `empty_visual.vfxb` has zero rate, zero bursts and zero alive; `hbg_vfx_empty_*` verifies it is pixel-identical to the no-VFX baseline. |
| No-VFX buffer gating | HDRP `HDRenderPipeline.RenderGraph.cs:1937,1959-1961,2062-2069,2092-2097` | Normal/depth/color buffers are sent to VFX only when `VFXManager.IsCameraBufferNeeded` requests them. | `--no-vfx` and empty effect keep the existing frame output; both release tests compare byte-for-byte on D3D11/Vulkan. |
| Lifecycle stress | `Tests/Editor/VisualEffectGraphEditorTests.cs:17-31` | Repeated component add/undo must not leave a VisualEffect or VFXRenderer behind. | Runtime repeats 16 create/destroy and 4 backend/runtime sessions; `--resize-test` covers full/half switches; shutdown has 0 live buffer/program/texture. |
| Golden matrix | Existing V1.1–V1.4 references | Unlit/lit, additive/alpha, soft, bursts, collision, events, sort, subgraph, custom HLSL, order | D3D11/Vulkan CTest matrix all pass; `hbg_vfx_order_off_*` and `hbg_vfx_empty_*` are exact comparisons. |
| Release checks | Unity VFX package tests are mostly editor lifecycle sanity; project gate is stricter | CTest, IDL check, baked-asset check, CPU reference, resource/validation checks | `hbg_idl_check`, `hbg_vfx_asset_check` (28 assets), full CTest 111/111, CPU 1024×10 reference pass. |

## 19. V2.0–V2.4 editor foundation

| Topic | Unity reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Editor layout serialization | `Editor/Models/VFXUI.cs:33-106` (`UIInfo.position`, system context association), `Editor/Models/VFXUI.cs:108-132` (sanitize invalid UI nodes), `Editor/Models/VFXGraph.cs:529-544` (`m_UIInfos` serialized with graph) | Node positions/collapse state persist with the graph asset; stale UI references are removed on load. | `vfx/include/vfx/editor.h` + `vfx/src/editor/document.cpp` store `systems/<i>/...` positions in `<asset>.editor.json`, plus `graphHash`; stale paths are pruned. Deviation: sidecar instead of graph field keeps `.vfx.json`/`.vfxb` unchanged. |
| Graph validation | `Editor/GraphView/Views/Controller/VFXGraphValidation.cs:25-46` (validate children then slot links) | Invalid models/links are surfaced before save/compile. | Every typed mutation re-runs `parseGraphJson()`; failure restores the previous JSON and keeps `ParseError{path,line,column}`. |
| Error reporter | `Editor/Models/VFXErrorManager.cs:20-49,94-130` (refresh/generate, per-model dedupe) | Errors are collected per model and shown in editor/dirty badges. | `Document::errors()`/`lastError()` + ImGui diagnostics/Compile panel; `editor::errorFocus()` maps `ParseError.path` to context/block/property and centers the canvas. |
| Node creation | `Editor/GraphView/Views/VFXView.cs:575-611`, `Editor/GraphView/Views/Controller/VFXViewController.cs:1274-1304` | Create operator/context/parameter, then sync controller/group. | V2.2 adds system/block/output/property create/remove/move; operators/subgraphs remain out of V2. |
| Block insertion | `Editor/GraphView/Elements/VFXContextUI.cs:751-775`, `:786-833` | Compute insertion index from mouse Y, then `controller.AddBlock` through the block provider. | V2.2 appends blocks from the shared `vfx::detail` block catalog with editable defaults; mouse-position insertion is not yet wired. |
| Save/compile | `Editor/GraphView/Views/VFXView.cs:1977-2004`; `Editor/Models/VFXGraph.cs:1664-1674`; `Editor/GraphView/VFXViewWindow.cs:395-443` | `OnSave` writes asset; dirty graph auto-compiles; compile updates asset desc and errors. | V2.3 `Save`/`Compile` write `.vfx.json` + sidecar, call `compileGraphFile`, atomically-ish replace sibling `.vfxb`; `On Save` toggle. `--vfx-shaderc`/`--vfx-bgfx-src`/`--vfx-shader-pack` + env/CMake defaults supply toolchain. |
| Hot reload | `VFXViewWindow.cs:395-443` (compile then refresh errors); V1 `Runtime::reloadEffect` | Dirty graph recompiles and errors refresh without closing the window. | `ForwardRenderer::reloadVfxAsset()` matches scene VFX instance path, calls `Runtime::reloadEffect`, reapplies instance properties; failures keep old effect and appear in status. No matching instance = compile succeeds, reload skipped. |
| Playback controls | `Editor/Inspector/VisualEffectEditor.cs:21-44` (`ControlStop/PlayPause/Step/Restart`), `:507-531` (Scene View toolbar), `:546-564` (play-rate percent/presets); `Editor/GraphView/VFXComponentBoard.cs:149-160,390-445`; `Editor/Inspector/VFXAssetEditor.cs:283-303` | Play/pause toggles pause; step pauses then advances one frame; restart reinits and plays; play-rate is a multiplier. | V2.4 `Effect::advanceOneFrame()` + `ForwardRenderer` playback APIs; Inspector Playback panel exposes Play/Pause, Step, Replay, Time scale (0–4x), fixed scene seed and aggregate bounds. Multiple matching instances share commands. |
| Preview | `Editor/Inspector/VFXAssetEditor.cs:130-310` (PreviewRenderUtility + preview VisualEffect, pause/Reinit) | Asset preview is a transient effect; play/pause/restart do not dirty the asset. | `Preview` compiles unsaved graph to temp `.vfxb`, then `ForwardRenderer::previewVfxAsset()` replaces matching scene effect or creates editor-only effect; canonical graph/layout/`.vfxb` untouched; failures preserve old effect. `--vfx-editor-preview` covers CI. |
| Runtime diagnostics | `Editor/GraphView/Profiling/VFXProfilingBoard.cs:13` (profiling board); V1.4.4 `FrameDiagnostics` | CPU/GPU per-pass timing and particle/draw counters. | Editor Runtime panel displays effects/systems/particles/draws/dispatches/sorts/events and update/sim/output/total ms from existing `FrameDiagnostics`; no new runtime pass. |
| UI technology | Unity UIElements/GraphView (`VFXView`, `VFXContextUI`) | Node canvas, selection, pan/zoom, inspector, blackboard. | `src/app/vfx_editor_ui.cpp` uses Dear ImGui and the existing DebugUI context; custom canvas, no Qt, no new render pass. |
| Undo/redo | `Tests/Editor/VisualEffectGraphEditorTests.cs:17-31` covers editor lifecycle; Unity uses graph undo stack | Edits are undoable without corrupting the graph. | V2.2 snapshot undo/redo of `root`/dirty/layout-stale, 128-entry cap, 500ms coalescing; graph revalidated on restore. |
| Text input | Unity GraphView/UIElements text fields | Names, paths and block string parameters are keyboard-editable. | GLFW key/char callbacks feed `ImGuiKey`/`AddInputCharacter`; system name/new property/block strings/texture paths use `InputText`. |
| Block defaults | Unity block variants expose serialized default properties | New blocks start with valid defaults defined once. | `vfx/src/core/block_catalog.{h,cpp}` shares parser field specs and editor default JSON; `trigger_event`/over-life use graph context, `subgraph` insertion is deferred. |
| Editor release checks | Unity editor tests cover open/add/undo/save lifecycle (`Tests/Editor/VisualEffectGraphEditorTests.cs:17-31`) | Editor load/edit/save must not corrupt graph or runtime assets. | `hbg_vfx_tests` 80/80 (38,056 assertions; 13 editor cases / 299 assertions), `hbg_vfx_runtime_tests` 19/19 (494 assertions), `hbg_vfx_editor_{d3d11,vulkan}` smoke, `hbg_vfx_editor_compile` canonical+preview CTest; full CTest 114/114 (1396.72s). V2.3 hot reload verified with temp scene + `--vfx` + Ctrl+S; V2.4 preview verified with `--vfx-editor-preview` and graph/canonical SHA256 checks. No checked-in editor sidecar or generated test binary. |
