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
| D2 | indirect 分块 | quads >16384 才按 2048 分实例 | V3.11 起 output draw 固定按 2048 粒子/instance 分块，保留非索引 6 顶点；`numVertices = 6*min(count,2048)`、`numInstances=ceil(count/2048)` | V0 已用单 draw 验证；V3.11 已关闭大粒子数分块，Unity 的 indexed/instance-split 语义仍见 D1/D5 |
| D3 | attribute 分组 | 读写亲和性分组（`AttributeLayoutPass.cs:30-115`） | V1 单 group（顺序 offset）；V1.0b 默认布局含全部内置属性 + 顶层自定义属性 | 先保证正确性；V1.1 若 profiling 显示 bucket 收益明显再加；后续按 block 读写集合收窄 |
| D4 | fill indirect | 引擎 VFXManager 用 `VFXFillIndirectArgs.compute` + CPU draw desc（`VFXManagerEditor.cs:184`） | runtime 直接生成 args（简化 kernel） | 自有 runtime 无 Unity 引擎的 draw desc 结构；V1 只需 quad |
| D5 | effect instancing | 多实例共享仿真 + instance split（`VFXDataParticle.cs:1505-1509`） | 每 effect 独立 buffer | 降低 V1 复杂度；V2+ 再加 |
| D6 | 自动 bounds | GPU AABB + readback | 手动 bounds + 编译期 spawner bounds | V1 不做 readback；后续加 GPU AABB |
| D7 | 自定义 HLSL | Unity 允许较完整 HLSL/子图 | 受限 `.sc` 语句片段；`VFX_ATTR`/`VFX_SET_ATTR`/`VFX_PROP` 宏；不可声明 buffer/uniform/texture | 保证双后端可编译与可移植性 |
| D8 | 渲染 pass 排序 | VFX 透明与场景透明物体统一排序 | VFX 输出 pass 位于 `Forward Transparent` 之前，透明 mesh 不与粒子逐对象交错 | V1 先保证顺序稳定；V3.16 已实现 opt-in effect-level 深度键；完整逐粒子/indirect batch 插入仍需后续 |
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
| D20 | 粒子 motion vector | Unity 由 output feature 控制，输出更新保存上一帧 element-to-VFX 变换；支持 flipbook/逐顶点 MV | V3.0 每个 output 在 `FrameSettings::MotionVectors` 开启时写入场景 MV buffer；codegen 在 init/update 维护 `oldPosition` 作为上一帧位置，输出用其重建 previous clip；V3.2 增加 per-output `generateMotionVector` gate；仍无 flipbook/逐顶点/soft-particle 边缘裁剪 | 先以同一 `.vfxb` v6 结构接入 TAA/motion blur（需重烘焙 checked-in binary）；后续增加 effect transform 历史与 alpha 设置时关闭 |

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
| Particle sort criteria | `Editor/Models/Contexts/VFXSortingUtility.cs:9-56`, `Editor/NewCompiler/Builder/OutputUpdateTaskBuilder.cs:79-127` | Four criteria: distance to camera, camera depth, youngest in front, custom scalar; per-camera criteria require a view. | V1.2.2 KVP sort implements distance/custom with deterministic tie-break; V3.8 adds cameraDepth/youngest on the shared system sort; V3.9 adds per-output direction while keeping the shared key list. V3.16 adds opt-in effect-level insertion into the scene transparent DrawList; particle-level interleaving remains open. |
| Output sort activation | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:110-120` | Transparent alpha/premultiplied outputs sort automatically; custom sorting is an output setting. | Alpha/additive outputs use the existing system sort; no new schema field in V1.4.3. |
| Transparent frame order | HDRP `Runtime/RenderPipeline/HDRenderPipeline.cs:3465-3498`, `HDRenderPipeline.RenderGraph.cs:1899-1911` | All transparent forward objects share a renderer list sorted with `CommonTransparent \| RendererPriority` before forward transparent. | Scene transparent DrawList uses `kTransparentSortingCriteria`; V3.16 opt-in inserts each VFX effect at an effect-level key; default remains VFX-before-transparent (D8). |
| Lit clustered lighting | HDRP transparent forward uses punctual light lists/clusters. | Lit transparent VFX binds the cluster light list rather than the global fallback. | `prepareVfxClusterLighting` reuses the texture cluster or builds a CPU cluster-texture mirror when the GPU light-list buffer path is active; VFX output reads `clusterLightList`. |
| Frame insertion | HDRP VFX update/output order. | Simulation before opaque/GBuffer; output after deferred lighting/SSR and forward-only opaque, before transparent by default. | Forward/deferred use the shared `addTransparentFramePasses()`; default VFX pass is before transparent, V3.16 opt-in inserts effect-level passes among sliced transparent segments. |
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

## 19. V2.0–V2.5 editor foundation

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
| Block defaults | Unity block variants expose serialized default properties and optional input slots (`VFXPlanarPrimitiveOutput.cs:84-98`, `VFXMeshOutput.cs:67-103`) | New blocks start with valid defaults defined once; optional fields can be added explicitly. | `vfx/src/core/block_catalog.{h,cpp}` shares parser field specs and editor default JSON; optional fields carry `defaultJson`; `trigger_event`/over-life use graph context; V2.2b adds subgraph invocation and optional-parameter insertion with raw defaults. |
| Clipboard/duplicate | `Editor/GraphView/Views/VFXCopy.cs:27-47` (`Copy`/`CopyBlocks`), `:311-390` (node flags/input slots/block copy); `Editor/GraphView/Views/VFXPaste.cs:145-234` (block target: selected block + 1 or context append); `Editor/GraphView/Views/VFXView.cs:2546-2560,2705-2735` | Copy/paste/duplicate blocks and blackboard properties; selection stays valid. | V2.2b raw block/property JSON clipboard; paste block after selected block or append to init/update context; duplicate block/property/system/output; toolbar + `Ctrl+C/V/D`. Single-selection scope. |
| Subgraph invocation | `Editor/GraphView/Views/VFXView.cs:575-611` (subgraph node creation), `Editor/Models/VFXSubgraphBlock.cs:124-183` (resource copy/resync) | A subgraph block/operator is inserted as a node; runtime graph is expanded separately. | `GraphParseMode::PreserveSubgraphs` keeps raw invocations in `Document`; compiler still flattens. `addSubgraphBlock()` builds args from raw input defaults and UI lists stage-compatible definitions. |
| Expression editing | Unity slot/operator graph (`VFXExpression`), not raw text | Values can be edited through typed slots and expression nodes. | V2.2b Inspector raw JSON editor for block `Expression`/`Object`/non-numeric arrays (`blockParameterJson`/`setBlockParameterJson`), with parse/rollback and undo. Deviation: no operator node graph yet. |
| Recovery/dirty state | `Editor/GraphView/Views/VFXView.cs:188-207` (`Save`/`Cancel`/`Discard Changes` dialog), `Editor/GraphView/VFXViewWindow.cs:289-301,354-379` (save/discard/window close) | Unsaved edits are not silently lost; Cancel keeps the window open. | Dirty graph debounce-writes temp recovery; opening prompts Recover/Discard; window close is intercepted and shows Save/Cancel/Discard modal; Save writes graph+layout, Discard clears recovery, Cancel keeps editing; layout sidecar is not in recovery. |
| Editor release checks | Unity editor tests cover open/add/undo/save lifecycle (`Tests/Editor/VisualEffectGraphEditorTests.cs:17-31`) | Editor load/edit/save must not corrupt graph or runtime assets. | `hbg_vfx_tests` 95/95 (38,249 assertions), `hbg_vfx_runtime_tests` 24/24 (566 assertions), `hbg_vfx_compiler_tests` 27/27 (559 assertions), `hbg_vfx_editor_{d3d11,vulkan}` smoke, `hbg_vfx_editor_compile` canonical+preview CTest; V2.5 `hbg_vfx_editor_roundtrip` saves/reloads 30 assets then `vfxbake --check` per asset (dxbc+spirv), plus D3D11/Vulkan 1280×720 screenshots; full CTest not used as a gate. V2.3 hot reload verified with temp scene + `--vfx` + Ctrl+S; V2.4 preview verified with `--vfx-editor-preview` and graph/canonical SHA256 checks. No checked-in editor sidecar or generated test binary. |

## 20. V3.0 particle motion vectors

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Output feature gate | `Editor/Models/Contexts/VFXAbstractRenderedOutput.cs:62-90` (`generateMotionVector`, `hasMotionVector`), `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:227-228` | Motion vectors are generated only when the output implements and enables them; transparent outputs use alpha clipping. | V3.0 writes MV when `FrameSettings::MotionVectors` is enabled and the fixed program resolves; V3.2 adds per-output `generateMotionVector` (default true for V3.0 compatibility; explicit false skips the MV draw). |
| Current/previous clip | `Shaders/VFXParticleCommon.template:199-220,217-218`; `Shaders/VFXCommonOutput.hlsl:311-323`; HDRP `Runtime/VFXGraph/Shaders/VFXCommon.hlsl:113-122`, `Runtime/ShaderLibrary/ShaderVariablesFunctions.hlsl:152-155` | Vertex stage computes current and previous clip positions; previous transform comes from output-update history. | `vfx/shaders/vfx_motion_vectors.vs.sc` reads `oldPosition` and, since V3.3, transforms it with `u_vfx_prevWorld` before `u_global_prevViewProj`. Deviation: previous billboard offset still uses current size/angle and there is no per-particle previous element matrix. |
| Encoding | `Shaders/VFXParticleCommon.template:243-258`; HDRP `Runtime/Material/Builtin/BuiltinData.hlsl:49-56`; `Runtime/RenderPipeline/ShaderPass/ShaderPassMotionVectors.hlsl:91-106` | `currentNDC - previousNDC`, clamp, multiply by 0.5, write RG16F. | `vfx/shaders/vfx_motion_vectors.fs.sc` follows the existing `shaders/passes/motion_vectors.fs.sc` convention; `addVfxMotionVectorsPass` reuses the scene motion-vector buffer. |
| Pass ordering | HDRP VFX output/motion-vector passes run after simulation and before post-process. | VFX color remains in its existing pass; MV is a separate pass with scene depth. | `ForwardRenderer::addVfxMotionVectorsPass()` records after VFX Output and before transparent forward; debug `motion-vectors` still runs VFX simulation/output. |
| Verification | Unity editor tests do not compare VFX MV pixels. | Need proof that VFX contributes to the MV buffer. | D3D11/Vulkan `hbg_vfx_motion_vectors` render `vfx_forces.gltf` with `--vfx-test` and `--no-vfx` and require ≥100 differing pixels in the motion-vector debug image. |

## 21. V3.1 particle shadows

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Per-output gate | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:132,149,600` (`castShadows`, `hasShadowCasting`, `USE_CAST_SHADOWS_PASS`) | Only outputs with `castShadows` enter shadow caster passes; transparent outputs use alpha test. | JSON optional `castShadows`; `.vfxb` v6 OUTPUTS reserved byte 21; editor Inspector checkbox. |
| Shadow shader | `Shaders/ParticlePlanarPrimitives/PassShadowCaster.template`, `PassDepthOrMV.template:91-106` | `ZWrite On`, `Blend Off`; `VFX_PASSDEPTH_SHADOW` with `VFXApplyShadowBias`. | Fixed `vfx/shaders/vfx_shadow.{vs,fs}.sc`: camera billboard, world transform, shadow VP, depth write; alpha clip for transparent, no clip for opaque. Deviation: no normal-bias function, uses request slope/depth bias. |
| Integration | HDRP culls/draws VFX shadow casters into its shadow atlases. | VFX must be simulated before shadow pass and exempt from static shadow cache when dynamic. | `ForwardRenderer` records VFX simulation before `addShadowPass`; each request calls `renderVfxShadow` after mesh shadows; `Runtime::hasShadowOutputs()` suppresses directional/punctual/area caches. |
| Verification | Unity shadow tests are editor-level. | Need proof that particles write the shadow atlas and that flag false writes nothing. | D3D11/Vulkan `hbg_vfx_shadows` compares `directional-shadow-atlas` between `vfx_shadow.gltf` (`castShadows:true`) and `vfx_lit.gltf` (`false`): ≥100 differing pixels, on log `vfx_shadow_draws=4`, off `0`. |

## 22. V3.2 per-output motion-vector gate

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Gate semantics | `Editor/Models/Contexts/VFXAbstractRenderedOutput.cs:62,78-86` (`generateMotionVector`, `hasMotionVector`); `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:227-228` | MV output is enabled only when the sub-output supports it, the output implements it, and the toggle is on. | `OutputDesc::generateMotionVector` defaults true for V3.0 compatibility; explicit false skips `encodeMotionVectors`. |
| Binary compatibility | Unity serializes the setting in the asset. | Existing assets should keep their behavior. | `.vfxb` v6 OUTPUTS byte 22: `0` = generate, `1` = disabled (old assets have 0); no record size/version change. |
| Editor | Inspector toggle. | Users can opt out per output. | `Document::setOutputGenerateMotionVector`; Inspector `Generate motion vectors`; node summary `motion on/off`; graphHash includes it. |
| Verification | Unity editor tests do not compare VFX MV pixels. | Need proof gate-on writes and gate-off writes nothing. | `hbg_vfx_motion_vectors` keeps gate-on coverage; `hbg_vfx_motion_gate` uses `shadow_visual` gate off and requires VFX on/off motion debug 0 diff. |

## 23. V3.3 effect transform motion history

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Previous object transform | `VFXParticleCommon.template:217-218` (`VFXGetPreviousElementToVFX`), HDRP `VFXCommon.hlsl:113-122` (`TransformPreviousObjectToWorld` + previous VP), `ShaderVariablesFunctions.hlsl:152-155` | Previous world position uses the previous object-to-world matrix, not the current transform. | `EffectImpl` keeps `m_previousTransform`; `setTransform()` commits one history per frame and `update()` clears the frame marker. First frame/reset uses current as previous. |
| Shader binding | `VFXCommonOutput.hlsl:311-323` (per-element previous matrix buffer) | Previous matrix is available during motion-vector vertex shading. | `vfx_motion_vectors.vs.sc` adds `u_vfx_prevWorld`; local systems bind prior effect transform, world systems bind identity. Missing uniform skips the MV pass. |
| Verification | Unity editor-level motion-vector behavior. | Prove transform movement changes previous clip, not current-only. | `hbg_vfx_runtime_tests` asserts current/previous uniform matrices 1/1→2/1→3/2 on local `curve_visual`; existing `hbg_vfx_motion_vectors`/`motion_gate` and dxbc+spirv shader builds stay green. |

## 24. V3.4 automatic bounds

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Bounds mode | `Editor/Data/VFXDataParticle.cs:215-299` (`BoundsSettingMode`, `needsComputeBounds`, `boundsMode`), `:928-938` (`SystemNeedsComputeBounds`/`SystemAutomaticBounds`) | Per-system bounds can be manual, recorded, or computed from alive particles. | `BoundsMode::Automatic`; JSON `"bounds":{"mode":"automatic"}`; `.vfxb` SYSTEMS reserved byte 60; editor Inspector mode combo. Missing mode/old byte 0 = manual. |
| GPU reduction | `VFXUpdate.template:182-203`; `VFXBoundsUtils.hlsl:109-168` | Update reads alive position/size, computes a local bound, and interlocked-min/max packs FloatFlip words into `boundsBuffer`. | Automatic systems add `bounds_init` and `bounds` kernels (64 threads) after Update; V3.4 used `0.5*abs(size)*length(scale)`, V3.13 uses the flat-billboard `0.5*abs(size)*length(scale.xy)`, and V3.20 uses camera-aware oriented extents with the 2D radius as fallback. V3.5 adds CPU padding; OBB remains deferred. |
| Readback | Unity runtime asynchronously reads the bounds buffer and exposes the AABB. | CPU sees a slightly delayed AABB. | `Device::readBufferAsync`/`readbackReady`/`copyReadback`/`releaseReadback`; Hbg uses `bgfx::read` + sentinel polling; runtime polls and decodes FloatFlip AABB, then transforms local bounds by the effect matrix. |
| Verification | Unity editor-level bounds checks. | Prove GPU data reaches `Effect::bounds()`. | `hbg_vfx_tests` JSON/editor tests; `hbg_vfx_runtime_tests` mock readback AABB + local transform; `hbg_vfx_compiler_tests` bounds kernels/binary mode; D3D11/Vulkan `hbg_vfx_reference_auto_bounds` validates real readback and finite bounds. Hbg D3D11 headless unit test is skipped, but the reference CTest covers the path. |

## 25. V3.5 automatic bounds padding

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Padding setting | `Editor/Models/Contexts/Implementations/VFXBasicInitialize.cs:64-72` (`InputPropertiesPadding.boundsPadding`; range includes negative values) | Additional size added to the culling bounds; mainly useful for recorded bounds. | JSON `bounds.padding` defaults to zero and is accepted for finite values, including negative values (which may shrink/invert the AABB). |
| CPU mapping | `Editor/Data/VFXDataParticle.cs:956-976` | Manual mode maps `bounds`, Recorded/Automatic map `boundsPadding`. | Padding is applied only to `BoundsMode::Automatic` in `EffectImpl::refreshBounds`; manual bounds ignore it. |
| Recorder semantics | `Editor/GraphView/BoundsRecorder/VFXBoundsRecorder.cs:211-212,275-276` | `currentBounds.extents += padding` for the displayed/padded bounds. | Runtime adds the padding vector to the readback/fallback AABB before local-to-world transform. No GPU codegen change. |
| Binary/editor | Unity serializes the padding value in the graph. | Old assets keep zero padding. | Optional `BOUNDS` section (id 11, `u32 count + count*3*f32`); omitted when all automatic padding is zero. `.vfxb` v6 record/version unchanged; `Document::setSystemBoundsPadding`; automatic Inspector field; graphHash includes nonzero padding. |
| Verification | Unity editor-level bounds. | Prove padding reaches `Effect::bounds()` and local transform, with D3D11/Vulkan parity. | `hbg_vfx_tests` JSON/editor round-trip; `hbg_vfx_runtime_tests` mock AABB `[(-1.25,-0.5,-2.0),(0.75,0.5,1.0)]` + padding `[0.25,0.5,0.75]` + x=10 transform; compiler/BINARY round-trip; reference logs show z `±0.173` → `±0.923` on both backends. |

## 26. V3.6 culling flags

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Asset flags | `Editor/Inspector/VFXAssetEditor.cs:441-451` (`k_CullingOptionsContents`/`k_CullingOptionsValue`; three option combinations) | Asset-level flags choose whether simulation and bounds update stop when the effect is not visible. | JSON `culling`: `"none"` (0), `"simulation"` (1), `"visible"` (3); `Graph::cullingFlags`; default 0 preserves existing assets. |
| Automatic bounds | `Editor/Data/VFXDataParticle.cs:287-299` forces `VFXCullingFlags.CullNone` when bounds mode becomes Automatic. | Automatic systems always simulate and update bounds. | JSON/reader reject automatic bounds plus non-zero culling; editor clears culling when switching to automatic. |
| Runtime | Unity native runtime culls effects by camera visibility and honors the asset flags. | Off-screen effects stop simulation/bounds according to flags; output is not submitted. | `Effect::setCulled`; `EffectImpl` skips simulation when `CullSimulation` is set, skips output and motion-vector draws when culled, and refreshes `isCulled()`. Manual bounds have no separate bounds update; automatic culling is rejected. |
| Renderer | HDRP gathers visible VFX instances and submits only visible output. | Camera frustum test uses effect world bounds. | `ForwardRenderer::updateVfx` converts `Effect::bounds()` and tests `frustumPlanes`; shadows remain on their independent shadow-request path. |
| Binary/editor | Unity serializes culling flags in the asset info. | Old assets default to always simulate; new assets can opt in. | Optional `CULLING` section (id 12, `u32 flags`), omitted for zero; `Document::setCullingFlags`; Inspector `Effect culling` combo. |
| Verification | Unity editor-level visibility behavior. | Prove simulation and draw suppression, plus compatibility. | Runtime mock test covers `CullSimulation` dispatch suppression and output suppression; parser/compiler/editor tests cover flags and BINARY round-trip; D3D11/Vulkan reference auto bounds and motion tests remain green. |

## 27. V3.7 output sort priority

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Output order | `Editor/Models/Contexts/IVFXSubRenderer.cs:9-16`, `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:110-111,246-256`, `Editor/Inspector/VFXAssetEditor.cs:98-154` | Output contexts are ordered by an integer priority; the reorder list assigns indices. | JSON output `sortPriority` (int32); runtime stable-sorts output submissions by ascending priority. Default 0 preserves declaration order. |
| Runtime mapping | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:792` maps `sortPriority` to the runtime; `Editor/Models/Contexts/Implementations/VFXStaticMeshOutput.cs:261` does the same for static mesh outputs. | Output order is per asset, across systems. | `AssetProgram::sortPriority`; `EffectImpl::m_outputOrder`; color, motion-vector, and shadow encode paths share the sorted list. |
| Serialization/editor | Unity serializes the priority on output contexts. | Old assets default to 0; no record/version change. | OUTPUTS offset 28 `i32`; reader defaults to 0; graphHash includes only non-zero; `Document::setOutputSortPriority`; Inspector `Sort priority`. |
| Verification | Unity Output Order editor list. | Prove cross-system output ordering and old-asset compatibility. | Runtime test patches `vfx_event.vfxb` priorities to reverse Emitter/Receiver draw order; parser/compiler/editor round-trip plus D3D11/Vulkan order/force goldens stay green. |

## 28. V3.8 camera depth and youngest sorting

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Sort criteria | `Editor/Models/Contexts/VFXSortingUtility.cs:9-56` | Four criteria: distance to camera, camera depth, youngest in front, custom scalar. | JSON/editor `sort.mode` adds `cameraDepth` and `youngest`; `distance`/`custom` are unchanged. |
| Sort keys | `Shaders/VFXParticleCommon.template:337-365`, `Editor/NewCompiler/Builder/OutputUpdateTaskBuilder.cs:95-125` | Lowest key renders first; camera depth is camera-forward distance; youngest is `age`; `SORTING_SIGN` handles reverse sorting. | `output_update` writes `dot(cameraRelativePosition, cameraForward)` for `cameraDepth` and `attributes.Load_age()` for `youngest`; `ascending`/`descending` maps to the existing KVP comparator. |
| Camera data | `VFXParticleCommon.template:349-357` | Depth uses the current view's forward axis and camera-relative position. | Kernel binds `u_vfx_world`, `u_global_cameraPos`, and `u_global_invView`; runtime passes the inverse of `FrameContext::view`. |
| Serialization | `VFXAbstractParticleOutput.cs:110-120`; Unity stores the criterion on the output. | `.vfxb` sort enum is additive. | SYSTEMS/OUTPUTS `sortMode` values 3=cameraDepth, 4=youngest; reader accepts them; old 0/1/2 bytes and assets are unchanged. |
| Verification | Unity sorting templates and output UI. | Prove key generation, binary round-trip, and GPU ordering. | Parser/compiler tests cover both modes and BINARY round-trip; `--vfx-reference` with 1024 particles passes on D3D11 and Vulkan for both modes; existing distance/custom goldens remain green. |

## 29. V3.9 per-output sort direction

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Output sort order | `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:117-122` (`sortMode`, `revertSorting`); `Editor/Models/Contexts/VFXOutputUpdate.cs:207-218` | Each output can reverse its particle draw order independently of the system's sort criterion. | JSON output `sortOrder` (ascending/descending); omitted means inherit system order. `.vfxb` v6 reuses OUTPUTS offset 11 `sortOrder`; record/version unchanged. |
| Shared key list | `Shaders/VFXParticleCommon.template:337-365` (`SORTING_SIGN`) | Reverse changes draw order but not the sorted key values or system sort pass. | Runtime keeps one system KVP/indirection; color VS reads stage 14 `RenderCount` and reverses `drawParticle`; MV/shadow passes reverse using existing params `.z`/`.w`. |
| Editor | Unity output inspector exposes revert sorting. | Old assets inherit; explicit override is editable. | `Document::setOutputSortOrder()`; Output Inspector `Sort order` with Inherit/Ascending/Descending; `setSystemSort()` leaves per-output overrides intact. |
| Compatibility | Unity settings serialize per output. | Old assets byte-identical. | Parser defaults to inherit; graphHash only includes explicit `sortOrder`; compiler writes effective order to OUTPUTS offset 11; old binaries retain system order. |
| Verification | Unity output sort controls. | Prove parser/compiler/runtime direction and default compatibility. | `hbg_vfx_tests` 96/96, compiler 28/28, runtime 25/25; reverse temp asset bakes and `--vfx-reference` passes D3D11/Vulkan; 30 assets byte-identical; order/force/motion/shadow goldens green; CTest 137. |

## 30. V3.10 per-output sort criteria

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Per-output criterion | `Editor/Data/VFXDataParticle.cs:614-666` (`NeedsGlobalSort`, `OutputNeedsOwnSort`); `Editor/Models/Contexts/VFXSortingUtility.cs:73-82` | Outputs with a different criterion need their own sorted list; outputs sharing a criterion use a global sort. | JSON output `sort` may differ from `system.sort.mode`; compiler emits an extra reset/output-update/sort pipeline per distinct extra criterion. |
| Sort key generation | `Editor/Models/Contexts/VFXOutputUpdate.cs:41,215`; `Shaders/VFXParticleCommon.template:337-365` | Each output's update uses its own criterion and `SORTING_SIGN`. | Extra pipeline reuses one KVP/indirection; `SortReset` (task 16) clears `RenderCount`, then output update/sort/fillIndirect run before the draw. |
| Ordering | Unity performs one global sort for the majority criterion and per-output sorts for the rest. | Output order remains declaration/priority order. | Runtime re-sorts on criterion changes while walking `m_outputOrder`; consecutive outputs with the same criterion reuse the list. |
| Binary | Unity serializes criterion/revert per output. | Existing v6 assets remain byte-identical. | OUTPUTS offset 10 stores the effective criterion; KERNELS offset 7 tags extra pipelines (0/old = base); no buffer/section/version change. Existing assets without extra pipelines are unchanged. |
| Uniform isolation | Output update and color/MV passes use separate uniforms. | Avoid duplicate-uniform assertions when sort compute and raster passes share an encoder. | Assets with extra pipelines compile base/extra output-update shaders with `u_vfxSortWorld`/`u_vfxSortCameraPos`/`u_vfxSortInvView`; runtime selects them via a per-system flag. |
| Verification | Unity per-output sorting. | Prove parser/compiler/runtime/GPU behavior. | `hbg_vfx_tests` 98/98, compiler 30/30 (616), runtime 26/26 (628); `sort_outputs` asset bakes dxbc+spirv; D3D11/Vulkan smoke tests pass; CTest 139. |
| Limits | Unity can keep independent lists. | V3.12 keeps independent sorted indirection per extra criterion while sharing KVP/RenderCount; no concurrent GPU sort. | Criterion switches still dispatch output update/sort once per extra list, but color/MV/shadow reuse the list without re-sorting. Assets with extra pipeline task 16 are rejected by older runtimes. |

## 31. V3.11 output draw instancing

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Draw chunk size | `Editor/NewCompiler/Passes/VfxTemplateCodeGenerationPass.cs:12-15`: `k_QuadParticlesPerDrawInstance = 2048`, matching native `RenderQuadCommand::kNbParticlePerInstance`. | One output draw covers at most 2048 particles per draw instance. | `fill_indirect` writes `numVertices = 6 * min(count, 2048)` and `numInstances = ceil(count / 2048)`; start vertex/instance stay 0. |
| Particle mapping | `VfxTemplateCodeGenerationPass.cs:546-576`: output `linearThreadIndex = (id >> 2) + input.instanceID * 2048`. | The instance ID selects the particle slice. | Output VS and shared MV/shadow VS compute `linearParticle = vertexID / 6 + gl_InstanceID * 2048`; the project keeps its 6-vertex quad instead of Unity's 4 vertices + index buffer. |
| Tail handling | Unity's fixed-size output path pads each instance to a whole dispatch. | Invalid tail particles must not index outside the shared indirection. | VS always binds stage 14 `RenderCount`; `linearParticle >= renderCount` writes a degenerate vertex, and reverse sorting is applied only after the guard. |
| Runtime/binary | Unity uses native per-instance output draws. | One indirect draw per output, with instanceCount from the GPU. | No API or v6 section/record change; `drawIndirect` consumes the new args. Shader/fillIndirect changes require rebaking every checked-in asset. |
| Verification | Unity quad output instancing. | Prove multi-instance and partial-tail behavior on both backends. | `reference_sorted_8192` D3D11/Vulkan reference passes with `[12288, 4]`; `reference_sorted_262144` passes with 128 instances; a temporary 2560-particle partial asset passes with `[12288, 2]`; sort/MV/shadow smokes pass; old HEAD 8192 asset and current 4-instance asset render byte-identical D3D11/Vulkan screenshots; compiler 30/30 (614 assertions), runtime 26/26 (592 assertions); CTest 139. |
| Limits | Unity also instances effect simulation/instance splits. | This step only chunks output draws. | Scene instances still own separate simulations/buffers; no instance split/property split; no indexed quad; last partial chunk may submit up to 2047 guarded vertices. |

## 32. V3.12 per-output sorted indirection lists

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Per-output sort task | `Editor/Data/VFXDataParticle.cs:1470-1500` | Each output sort task writes to its own `dstBuffer` (`sortedIndirect + j`); outputs with an own criterion do not overwrite another output's list. | Runtime allocates one sorted indirection buffer per extra criterion (`vfx.<system>.sorted.<mode>`), shared by outputs with the same criterion. |
| Sort key storage | `VFXDataParticle.cs:1478-1493` (`srcBuffer`/`dstBuffer`) and `Shaders/Sort.compute` | The sorted indirection is the per-output draw list; KVP keys are transient sort input. | V3.12 shared KVP/RenderCount/indirect args; V3.14 gives each extra criterion its own KVP/RenderCount/indirect and writes them through `SortBufferSet`. |
| Draw binding | `VFXOutputUpdate` selects the output list for its draw. | Color, MV, and shadow passes use the output's own sorted list. | `outputBuffers()` chooses stage 13/14 and indirect args; color binds them through vertex bindings, MV/shadow bind them explicitly. |
| Lifecycle | Unity refreshes output lists from simulation/output update. | Lists must not be reused after simulation changes. | `m_sortGeneration` increments on simulation; extra lists are sorted once per generation on first use; base list comes from the simulation base pipeline. |
| Verification | Unity per-output sorting. | Prove list separation and reuse. | `sort_outputs` is distance → youngest → distance; runtime test observes stage 13/14/args base → youngest → base and zero extra MV sorts. D3D11/Vulkan smokes pass. |
| Limits | Unity performs per-output GPU sort scheduling. | No concurrent/partitioned sorting in this step. | Lists are per criterion, not per output; different criteria are still sorted serially; V3.14 adds independent KVP/RenderCount/indirect per criterion and paused camera changes reuse the previous distance/cameraDepth list. |

## 33. V3.13 tight billboard automatic bounds

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Flat-element radius | `VFXBoundsUtils.hlsl:52-59` `ComputeBoundingRadius2DFromMatrix()` uses `0.5*sqrt(xAxisSqrLength + yAxisSqrLength)` | A flat element's bounding sphere uses only the two in-plane axes; its normal/z axis does not contribute. | `makeBoundsSource()` writes `0.5 * abs(size) * length(scale.xy)`; `scale.z` remains in the attribute layout but is excluded from automatic bounds. |
| 3D/OBB boundary | `VFXBoundsUtils.hlsl:61-65` `GetBoundingRadius3D_Local()` includes all three axes; `GetTightBoundingExtents_VFX()` uses the element-to-VFX matrix. | Unity has a 3D sphere path and a separate tight-extents path for oriented elements. | This step specializes only the camera-facing quad path; V3.20 adds camera-aware AABB extents, while mesh/3D outputs and tight OBB with axis/angle/pivot remain deferred. |
| Binary/assets | Unity recomputes bounds from the compiled graph. | The radius change must be reflected in the generated kernel and baked asset. | No section/version/API change; only `auto_bounds.vfxb` is rebaked; `vfxbake --check` keeps the other 30 assets byte-identical. |
| Verification | Unity editor-level bounds. | Prove the generated formula and real GPU readback on both backends. | Compiler 30/30 (616 assertions) checks `length(scale.xy)` and rejects `length(scale)`; runtime 26/26 (605); `hbg_vfx_tests` 98/98 (38,286); D3D11/Vulkan auto-bounds reference pass; D3D11 readback `min=(-1.3821943,-0.6414214,-0.8914214)` / `max=(1.3291512,0.6414214,0.8914214)` with fixture `scale.z=4` confirms z does not contribute and matches radius `0.1414` plus padding `[0.25,0.5,0.75]`; CTest 139. |
| Limits | Unity can compute 3D/tight bounds for oriented elements. | V3.13 is a billboard-specific tightening, not a general OBB. | Bounds remain an AABB readback; V3.20 adds camera-aware billboard extents, but there is no per-particle tight OBB, mesh extents, or OBB padding. |

## 34. V3.14 per-output sort state

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Per-output indirect state | `Shaders/VFXOutputUpdate.template:31-45` (`Kvp`, `IndirectOutputType`, `outputBuffer0..N`); `Editor/Data/VFXDataParticle.cs:1478-1493` (`srcBuffer`/`dstBuffer`) | Each output update/sort has its own KVP and indirect buffer state. | Runtime allocates an independent KVP, RenderCount, and indirect args buffer per extra criterion; names are `vfx.<system>.sorted.<mode>[.kvp|.count|.indirect]`. |
| Fill and draw | `Shaders/VFXFillIndirectArgs.compute:60-68` writes args from the active output's count. | The output draw consumes the args belonging to its own sorted list. | `SortBufferSet` overrides compute bindings; `fillIndirect` writes the criterion's KVP/count/args; color/MV/shadow draw from that criterion's indirect handle. |
| Isolation | Outputs with different criteria do not mutate each other's list or args. | Base and extra outputs can be drawn in any order. | Extra sort/reset/fill no longer touches the base KVP/count/args; the simulation base list remains valid. |
| Binary/assets | Unity stores sort/output state in the runtime asset. | No serialization change for this step. | `.vfxb` v6, compiler, shaders, and all checked-in assets are unchanged; the new buffers are runtime-only. |
| Verification | Unity per-output sort state. | Prove stage 13, stage 14, and indirect args isolation on both backends. | Runtime 26/26 (628) checks base → youngest → base for stage 13, stage 14, and draw args, plus MV reuse; `hbg_vfx_tests` 98/98 (38,286), compiler 30/30 (616), adapter 8/8 (328); 12 D3D11/Vulkan smokes pass; CTest 139. |
| Limits | Unity can schedule per-output GPU sorts. | V3.14 is state isolation, not concurrent sorting. | Sort state is per unique criterion; extra sorts are serial on first use; scratch is shared; no per-output sort state for equal criteria or serialized runtime buffers. |

## 35. V3.15 effect simulation instancing v1

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Instancing state | `Shaders/VFXInstancing.hlsl:1-115` (`instancingCurrentCount`, `instancingActiveCount`, `instancingBatchSize`, current/active/batch indirection) | A batch keeps per-instance spawn/count state and resolves current → active → batch indices. | Runtime keeps one current/previous transform, local bounds, and cull bit per effect instance; no prefix-sum or batch indirection. |
| Buffer layout | `Editor/Data/VFXDataParticle.cs:918-920` (`instancingIndirectAndActiveIndirect`), `:1505-1509` (`1 + instanceSplitDescs.Count` indirection and prefix-sum sizes) | Instance split descriptions allocate shared per-system instancing buffers. | All compatible instances share the existing system attribute/dead-list/event/sort buffers; no new GPU instancing buffers. |
| Batch discovery | `Editor/NewCompiler/Passes/OutputPass.cs:300-312` sets `SystemUsesInstancedRendering` and emits `instanceSplitDescs`. | The compiler records which CPU/GPU values require separate batches. | Renderer groups consecutive instances at scene init by asset path, seed, playRate, playOnAwake, and property list; no compiled split data. |
| Init/update indexing | `Shaders/VFXInit.template:74-112`, `Shaders/VFXUpdate.template:58`, `Shaders/VFXParticleCommon.template:313-325` call `VFXInitInstancing` per dispatch/draw. | Instance index selects the spawn/count slice for the batch. | Simulation runs once; output/MV/shadow iterate instances on the CPU and set `u_vfx_world`/`u_vfx_prevWorld` per draw. |
| Eligibility | Unity supports split properties and per-camera sorting within batches. | Instancing is not limited to one sort mode or one property set. | v1 only accepts local-space assets with `none`/`youngest` output sorting; other criteria and mismatched properties fall back to separate effects. |
| Verification | Unity `VFXInstancing.hlsl`/`VFXDataParticle` instancing path. | Prove shared simulation with distinct per-instance transforms and byte-identical fallback rendering. | Runtime 29/29 (671) covers two-instance bounds/draws, previous transforms for MV, and world-space rejection. D3D11/Vulkan `hbg_vfx_instancing` logs effects 2 → 1 with two instances and compares screenshots byte-for-byte; 32 goldens + 12 smokes + CTest 141. |
| Limits | Unity also batches instanced draws and splits by property/texture. | This step is simulation sharing, not full instancing. | V3.15 per-instance `drawIndirect`; V3.17 batches all-visible instances. No instance split/prefix-sum/batch indirection, no GPU culling compaction, no hot-reload regrouping. |

## 36. V3.16 VFX transparent effect-level ordering v1

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Transparent list key | `Runtime/RenderPipeline/HDRenderPipeline.cs:3460-3499` uses `SortingCriteria.CommonTransparent \| SortingCriteria.RendererPriority`; `HDRenderPipeline.RenderGraph.cs:955-965,1899-1911` builds/draws the forward transparent RendererList. | Transparent renderers are sorted by layer/queue/priority/order/depth. | Effect key uses transparent queue 3000, distance from bounds center, and `kTransparentSortingCriteria`; no per-renderer queue/priority metadata exists for a VFX effect. |
| VFX internal order | `Editor/Models/Contexts/VFXSortingUtility.cs:9-56`, `Shaders/VFXParticleCommon.template:337-365`. | Particles are sorted by distance/camera depth/youngest/custom before output draw. | Unchanged: each effect still uses its V3.8–V3.14 per-output KVP/indirection state. |
| Effect insertion | HDRP transparent RendererList contains scene renderers; VFX Graph submission is handled by `VFXManager.ProcessCameraCommand` / VFX output scheduling. | VFX and transparent scene renderers share a frame order, but VFX particles are not individually merged into the RendererList. | V3.16 inserts one VFX pass per effect between sliced transparent DrawList segments; opt-in only. |
| DrawList slicing | HDRP RendererList owns the sorted transparent set. | Transparent renderers can be drawn in sorted order. | `DrawList::buildRange()` copies a sorted item range and rebuilds batches; segments live in `m_vfxTransparentSegments`. |
| Runtime API | VFX output is submitted as a batch. | One effect can be encoded independently. | `Runtime::beginOutputFrame()` and `Runtime::encodeOutputEffect()` added; full `Runtime::encodeOutput()` uses the same path. |
| Frame paths | HDRP forward/deferred transparent lists. | Both paths use the same transparent sorting criteria. | Shared `addTransparentFramePasses()` for forward/deferred; pre-refraction/backface/GPU-driven paths fall back to old ordering. |
| Verification | Unity transparent RendererList and VFX output behavior. | Prove effect-level order changes while particle order stays stable. | New `vfx_transparent_order` scene has far/near effects around a transparent panel; D3D11/Vulkan off/on tests and goldens pass; off/on differs by 4,680 pixels (max delta 81). |
| Limits | Full DrawList integration would require per-particle/indirect batch keys. | V3.16 does not merge particles with transparent meshes. | Effect-level only; approximate bounds-center depth; default queue; no refraction/backface merge; CPU segmented transparent fallback when GPU-driven is active. |

## 37. V3.17 effect GPU instancing v2: instanced draw v1

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Instance indexing | `Shaders/VFXInstancing.hlsl:1-115` (`instancingCurrentCount/ActiveCount/BatchSize`, current → active → batch indirection, `VFXInitInstancing()`). | Fixed/variable-size batches select an instance and then its active/batch index. | Effect instance is derived from `gl_InstanceID / particleChunks`; particle chunk is the remainder. No active/batch indirection or variable-size prefix sum. |
| Transform data | `Shaders/VFXOutputUpdate.template:147-152`, `Shaders/ParticlePlanarPrimitives/PassVS.template:113-142` use per-instance `elementToVFX` and attributes. | Each instance has its own element matrix and state. | One `vfx.instance.transforms` buffer stores current + previous `Mat4` per effect instance at stage 15; raw columns are applied in output/MV/shadow VS. |
| Indirect draw count | `Editor/NewCompiler/Passes/OutputPass.cs:300-312` sets `SystemUsesInstancedRendering`/`instanceSplitDescs`; output update/fill indirect emits per-instance draw args. | Unity batches output draws using instanced properties/index buffers. | `fill_indirect` writes `particleChunks * effectInstances` as `numInstances`; all instances submit through one `drawIndirect` per output when visible. |
| Culling | Unity supports active/batch indirection and GPU culling. | Culled instances can be omitted from the batch. | All-or-nothing CPU fallback: any culled instance makes the whole effect use per-instance draws; no GPU compaction. |
| Verification | Unity instanced output draw path. | Prove batched draws preserve distinct transforms and fallback behavior. | Runtime 30/30 (699) covers color/MV/shadow batch + cull fallback; D3D11/Vulkan instancing color and MV screenshots are 0 diff with rotation/scale; `vfx_draws` 4 → 2. |
| Limits | Full instancing includes property/texture splits, variable-size batches, active/batch indirection, and per-instance sort state. | Unity is broader than this v1. | Fixed-size compatible groups only; V3.18 adds active indirection and V3.19 active-count args, but no split/prefix-sum/compaction; `distance`/`cameraDepth`/`custom` outputs are not grouped. |

## 38. V3.18 effect GPU instancing v3: active instance indirection

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Fixed-size indexing | `Shaders/VFXInstancing.hlsl:37-54` derives `instanceIndex` from the current draw/dispatch index and clamps to the current count. | Outputs use one instance per fixed-size chunk. | `gl_InstanceID / particleChunks` remains the current effect instance; the particle chunk is the remainder. |
| Active indirection | `Shaders/VFXInstancing.hlsl:69-80` maps current -> active through `instancingIndirectAndActiveIndirect[instancingActiveIndirectOffset + instanceCurrentIndex]`; `Editor/Compiler/VFXCodeGenerator.cs:962-988` enables active indirection; `Editor/Data/VFXDataParticle.cs:1505` allocates the indirection buffer. | Unity removes culled instances from active compute/draw work. | CPU rebuilds a visible-instance list each frame and appends it to stage 15 after N transform slots; the output/MV VS maps `instanceIndex` through `u_vfxInstanceParams.z` when `w > 0.5`. |
| Output path | Unity output VS reads `_InstanceActiveIndex`/`_InstanceIndex` per-instance properties (`Editor/ShaderGraph/Templates/VFXConfig.template.hlsl`). | Material instancing supplies active and batch indices. | No material instancing; one effect-level buffer holds current/previous transforms plus the active list. |
| Partial culling | Active instances can be drawn while inactive ones are omitted. | One draw covers the active subset. | Visible count >1 uses one batch draw; `fill_indirect` still submits `totalInstances` slots and inactive slots degenerate. Visible count 1 keeps per-instance fallback. |
| Shadow | Unity shadow output can ignore camera culling for shadow casters. | Shadow keeps all effect instances. | `encodeShadow()` uses `totalInstances` with active indirection disabled and always batches N>1 instances. |
| Verification | Unity instance indexing/active indirection semantics. | Prove partial culling preserves output and MV. | New `vfx_instancing_cull` (3 instances, one off-screen): D3D11/Vulkan color and MV 0 diff; off `vfx_draws=4`, on `vfx_draws=2`; runtime 31/31 (725) and compiler 30/30 (626). |
| Limits | Unity supports batch indirection, instance/property splits, variable-size prefix sums, and GPU compaction. | This step only adds an active list. | V3.18 itself has no GPU compaction, active-count indirect args, batch indirection, property/texture split, or prefix sum; active slots beyond the visible count still execute a degenerate VS; shadow bypasses the active list. V3.19 adds active-count args but keeps the other limits. |

## 39. V3.19 effect GPU instancing v4: active-count indirect args

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Batch sizes | `Shaders/VFXInstancing.hlsl:10-12` exposes `instancingCurrentCount`, `instancingActiveCount`, and `instancingBatchSize`. | Draw/dispatch work is bounded by the active batch size. | `m_activeArgsRequired` is `instancing && 0 < active < total`; color/MV args use active count, full batches keep total args. |
| Batch indirection | `Shaders/VFXInstancing.hlsl:29-31` stores active→batch and per-split current→active sections; `:81-92` maps active→batch when `instancingActiveCount < instancingBatchSize`. `Editor/Compiler/VFXCodeGenerator.cs:984-988` enables both defines; `Editor/Data/VFXDataParticle.cs:1505` allocates the indirection buffer. | Unity can address a batch smaller than the active set. | No instance split, so batch order equals active order; the V3.18 active list is the batch→current mapping and no active→batch buffer is needed. |
| Args generation | Unity output/fill-indirect path derives per-instance draw args from active/batch state. | Active count affects the draw instance count. | `prepareActiveIndirect()` lazily creates `.active.indirect` per system/sort list and dispatches the existing `fill_indirect` with `u_vfxInstanceParams.x = active`; shadow keeps the simulation-generated total args. |
| Single visible | Unity output still uses its active/batch path. | A one-instance batch should not submit all effect instances. | Visible count 1 uses active args with `particleChunks * 1` and the `u_vfx_world` fallback, avoiding the V3.17/V3.18 total-count duplicate submission. |
| Verification | Unity active-count/batch semantics. | Prove partial culling preserves output and MV, including non-contiguous active instances. | `vfx_instancing_cull` and `vfx_instancing_cull_gap` (active `[0,2]`) are D3D11/Vulkan color/MV 0 diff, off `vfx_draws=4`, on 2; runtime 31/31 (743), compiler 30/30 (626). |
| Limits | Unity supports splits, property/texture splits, variable-size prefix sums, and GPU compaction. | This step only narrows fixed-size args. | No split/batch-smaller-than-active active→batch mapping, property/texture split, prefix-sum batch, or GPU cull compaction; active list remains CPU-owned and shadow ignores it. |

## 40. V3.20 tight billboard automatic bounds v2: camera-aware AABB

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Tight extents | `Shaders/VFXBoundsUtils.hlsl:73-85` `GetTightBoundingExtents_VFX()` uses the element matrix columns: `extents.x = max(abs(m00), abs(m01))`, etc. `:52-56` keeps the 2D radius fallback. | An oriented flat element gets tighter AABB extents than its bounding sphere. | Bounds kernel uses camera right/up, particle `angle`, `scale`, and `size` to build `billboardRight/Up` and `extents = 0.5*|size|*(|right|*|scale.x| + |up|*|scale.y|)`. |
| Camera basis | Unity's element matrix already contains the element orientation. | Orientation is camera-dependent for billboards. | Runtime uploads `FrameContext::cameraRight/Up` only for automatic-bounds systems via `u_vfxBoundsRight/Up`; `u_vfxBoundsParams.x` enables the tight path. |
| Fallback | Unity keeps `ComputeBoundingRadius2DFromMatrix()`. | Old/unsupported elements remain conservative. | V3.13 `radius = 0.5*|size|*length(scale.xy)` remains fallback when the tight flag is off or `angle` is absent (angle=0). |
| Assets | Unity recompiles the graph for bounds changes. | The generated bounds kernel is embedded per system. | Only `auto_bounds.vfxb` is rebaked; `auto_bounds` reference logs tight y/z `±0.6/±0.75` instead of the old sphere `±0.6414/±0.8914`. |
| Verification | Unity editor-level bounds. | Prove generated formula and real GPU readback on both backends. | Runtime 31/31 (751) checks camera uniforms/readback; compiler 30/30 (631) checks the source; new `run_vfx_auto_bounds.cmake` checks D3D11/Vulkan bounds and tight y/z; 90 VFX CTest, 31 assets, CTest 146. |
| Limits | Unity supports axis/angle/pivot tight OBB and mesh bounds. | V3.20 only tightens the billboard AABB. | Still AABB, not OBB; no mesh/3D extents or OBB padding. Camera changes while paused do not dispatch new bounds; readback remains asynchronous. |

## 41. V3.21 cull-aware visible instance bounds

| Topic | Unity VFX Graph/renderer reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Per-instance bounds | `Shaders/VFXBoundsUtils.hlsl:150-168` writes min/max at `boundsBufferBaseIndex = instanceIndex * 6`; `Editor/Data/VFXDataParticle.cs:928-938` marks automatic bounds. | Bounds are owned per instance and can be resolved against visibility. | `instanceBounds(i)` remains the per-instance transformed AABB; V3.21 adds `visibleBounds()` as the union of non-culled valid instance bounds. |
| Effect-level sort | Unity renderer culls instances before submitting visible renderers. | A culled instance must not influence the visible set's sort position. | `ForwardRenderer::addTransparentFramePasses()` uses `visibleBounds()` for the V3.16 sort center; invalid visible bounds fall back to the primary instance position. |
| Full bounds | Renderer diagnostics/debug still expose the complete effect extent. | Culling and debug may need different aggregates. | `bounds()` remains the all-instance union; only transparent ordering and the new API use the culled subset. |
| Cull updates | Visibility changes are consumed when the renderer updates culling. | Sort should follow the latest cull state available in the frame. | `refreshBounds()` and `setCulled()`/`setInstanceCulled()` refresh the cached visible bounds; the next `updateVfx` consumes camera changes. |
| Verification | Unity per-instance bounds/culling. | Prove the visible aggregate excludes culled instances and recovers. | Runtime 31/31 (761) checks all-visible, one-culled `[2,4]`, all-culled invalid, and restored bounds; 90 VFX CTest, 98/98 VFX tests, 8/8 adapter, 318/33,587 hbg tests, 31 assets, CTest 147. |
| Limits | Unity can interleave individual VFX renderers with transparent objects. | This step only corrects the aggregate key. | Still one effect-level key per instanced group; no per-instance/per-particle transparent interleave, GPU cull compaction, or shadow active list. |

## 42. V3.22 fixed-size active instance split

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Batch sizes | `Shaders/VFXInstancing.hlsl:10-12` exposes current/active/batch counts; `:81-92` maps active→batch when the batch is smaller than active. | Draw work can be bounded by a batch size. | `EffectDesc::instanceBatchLimit` / `PipelineSettings::vfxInstanceBatchSize` / `--vfx-instance-batch=<n>`; 0 means one full active batch. |
| Active→batch mapping | `Shaders/VFXInstancing.hlsl:29-31` stores active→batch and current→active indirection; `Editor/Compiler/VFXCodeGenerator.cs:984-988` enables it. | Active and batch indices can differ. | No instance split, so batch order equals active order; batch-local index is resolved as `activeBase + batchOffset`. No separate active→batch buffer. |
| Args | Unity derives per-batch draw args from the batch state. | Each batch gets its own instance count. | `BatchIndirectRuntime` creates `.active.indirect` / `.active.indirect.1...`; `fill_indirect` writes each batch's `particleChunks * localInstances`. |
| Shader | Unity output VS receives `_InstanceActiveIndex`/`_InstanceIndex` per instance. | Batch-local index must map to a current instance. | Existing generated output/MV VS is unchanged; `u_vfxInstanceParams.z` carries `activeBase + batchOffset` and the V3.18 active list maps to current. |
| Shadow | Shadow output can keep all effect instances. | Camera culling need not split shadow batches. | V3.22 itself leaves shadow at one total batch; V3.23 applies the same limit to total-instance shadow batches while still ignoring the active list. |
| Verification | Unity active/batch semantics. | Prove split preserves pixels and cull mapping. | Runtime 32/32 (792): limit 2 on 3 active gives args `{3,2,96,1}` / `{3,1,98,1}`; culling the middle gives `[0,2]`. Instancing fixture limit 1 emits 4 draws and is 0 diff on D3D11/Vulkan color/MV; cull fixtures also remain 0 diff, including active `[0,2]`. |
| Limits | Unity supports instance/property/texture splits, variable-size prefix sums, and GPU compaction. | This step only slices the active list. | Fixed-size contiguous batches; no property/texture split, prefix-sum batch, independent active→batch buffer, or GPU cull compaction. `instanceBatchLimit` is creation-time only. |

## 43. V3.23 fixed-size shadow instance split

| Topic | Unity VFX Graph reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Draw offset | `Shaders/VFXInstancing.hlsl:5,10-17` exposes current/active/batch counts and `instancingCurrentOffset`; `:37-54` adds the offset to a fixed-size current index. | A shadow batch-local instance must resolve to the global transform slot. | `vfx_shadow.vs.sc` adds `u_vfxInstanceParams.z` to the batch-local instance index. |
| Batch sizing | `Editor/Compiler/VFXCodeGenerator.cs:185-186` emits `instanceIndex` at task entry; shadow is another instanced task. | Shadow can use the same batch limit but must include every effect instance. | `refreshDrawInstancing()` computes `shadowBatchSize/Count` from total instance count and `instanceBatchLimit`, independent of active color/MV batches. |
| Args | Unity derives per-draw args from its current/batch state. | Each shadow batch needs its own instance count. | `SystemRuntime::shadowIndirect` holds `.shadow.indirect[.N]`; `prepareShadowIndirect()` fills each with `particleChunks * localInstances` and caches by `m_shadowArgsSerial`. |
| Submission | Shadow receives the same instance offset semantics as other draws. | Camera culling must not remove shadow instances. | `encodeShadow()` loops total batches with `{totalInstances, localInstances, batchOffset, 0}` and never reads the active list. |
| Verification | Unity instancing offset semantics. | Prove batch-local shadow instances map to the right transforms. | Runtime 33/33 (814): 3 instances, limit 2 gives two shadow draws and params `{3,2,0,0}` / `{3,1,2,0}`; culling does not change shadow batching. DXBC/SPIR-V shader compile passes. Manual D3D11/Vulkan smoke on 3 local shadow instances, limit 1: `vfx_shadow_draws` 4 → 12 and 0-diff shadow atlas; the shared manifest needed temporary color-pyramid placeholders due to a parallel edit. |
| Limits | Unity supports property/texture splits, variable-size prefix sums, and GPU compaction. | This step only slices shadow by total instance count. | No shadow active list, GPU cull compaction, property/texture split, prefix-sum batch, or active→batch buffer. `instanceBatchLimit` is creation-time only. |

## 44. V3.24 VFX transparent per-batch insertion

| Topic | Unity/HDRP reference | Behavior | hdrp-bgfx landing |
|---|---|---|---|
| Transparent ordering | HDRP `Runtime/RenderPipeline/HDRenderPipeline.cs:3460-3475` builds transparent RendererLists with `SortingCriteria.CommonTransparent \| RendererPriority`; `HDRenderPipeline.RenderGraph.cs:1872,1902` submits all transparent forward categories through the same sorted list. | Transparent items are sorted at draw/submesh granularity, not whole materials/effects. | V3.24 keeps the V3.16 transparent key but emits one submission per active instance batch; `n=1` gives per-instance granularity. |
| Batch mapping | Unity `Shaders/VFXInstancing.hlsl:5,10-17,37-54` defines fixed-size current offset; V3.22/V3.23 use it for color/MV/shadow. | The transparent split must use the same active batch/args as the color pass. | `Effect::outputBatchCount/outputBatchSize` expose the V3.22 active batch; `Runtime::encodeOutputEffectBatch` reuses `prepareActiveIndirect()` and `.active.indirect[.N]`. |
| Batch bounds | Unity VFX culling/sorting uses per-instance bounds; `Shaders/VFXBoundsUtils.hlsl:150-168` writes per-instance results. | Each submission needs a representative transparent depth. | ForwardRenderer unions non-culled `instanceBounds()` per active batch and uses the union center as the transparent key; culled instances are skipped. |
| Visibility | Shadow keeps all instances; camera culling affects visible color/MV. | Batch insertion must not change shadow or MV semantics. | Only color output passes are per-batch; MV runs once after transparent segments and shadow still draws total instances. |
| Defaults | HDRP transparent sorting is pipeline behavior. | Existing effect-level behavior must remain stable. | Requires `--vfx-transparent-sort` plus non-zero `--vfx-instance-batch`; `0` keeps V3.16 effect-level key. No new setting/asset/shader. |
| Verification | HDRP transparent RendererList + Unity batch semantics. | Prove order changes and mapping survives cull. | Runtime 34/34 (843): limit-1 batch 1 uses `active.indirect.1` and `{3,1,97,1}`; cull middle → 2 batches over `[0,2]`. New D3D11/Vulkan `vfx_transparent_batch_order`: 3 grouped instances + panel, `batches=1/3`, `vfx_draws=2/6`, order diff 4,680 px/max81, cross-backend max1. |
| Limits | Unity has per-renderer transparent ordering and particle-internal sorting. | Batch granularity is still coarser than per-particle. | No per-particle insertion, pre-refraction/backface merge, or GPU-driven transparent merge; batch size >1 cannot interleave transparent objects inside the batch. |
