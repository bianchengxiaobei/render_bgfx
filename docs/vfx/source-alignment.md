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
| output update 注册排序 | `Shaders/Temp/Templates/OutputUpdate.hlsl:14-33` | alive 才注册 `SortingKVP(key,index)` | `vfx_init`/`vfx_output_update` codegen |
| output 顶点取粒子 | `Shaders/Temp/Templates/Output.hlsl:4-60`、`Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58` | 通过 `RenderingDataBinding.GetRenderingIndex` 取 indirection | `shaders/vfx/vfx_quad.vs.sc` + `s_indirection` |
| 渲染计数/indirect | `Editor/NewCompiler/Data/RenderingData.cs:7-26,79-90`；`Editor/NewCompiler/Data/DescriptionWriter/RenderingDataDescriptionWriter.cs:54-96` | `RenderingData` 含 KVP counter/keys；`DrawIndirectionData` 含 counter + index buffer | `.vfxb` `rendering` section；runtime buffers |

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
| 排序缓冲 | `Editor/Data/VFXDataParticle.cs:1185-1206,1489` | `VFXSortBufferA/B`、`VFXScratchSortBuffer`；KVP stride 8 | `.vfxb` system `sort` section |
| instancing 缓冲 | `Editor/Data/VFXDataParticle.cs:1505-1509` | instance indirection/prefix sum（V1 不做） | V2+ |

## 3. 事件、排序、前缀和

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| CPU 事件列表 | `Shaders/Temp/Data/EventListData.hlsl:8-35` | events prefix sum + spawn count prefix sum；二分查找属性索引 | `vfx/src/runtime/events.cpp`；`vfx_events.sc` |
| GPU 事件列表 | `Shaders/Temp/Data/EventListData.hlsl:37-84` | `elementCount`/`totalCount`/`indexBuffer` 原子 append；`baseEventCount` | `vfx/src/runtime/events.cpp`；`vfx_events.sc` |
| 事件生产者 | `Editor/NewCompiler/Builder/GpuEventBuilder.cs`（按需复核） | 死亡/更新事件写入事件缓冲 | `vfx/src/compiler/codegen/event_emit.cpp` |
| sort KVP | `Shaders/Sort.compute:65-120` | `{float key; uint value;}`；key 相等按 value 升序；bitonic + merge | `shaders/vfx/vfx_sort.sc`；`vfx/src/runtime/sort_plan.cpp` |
| bitonic kernel 尺寸 | `Shaders/Sort.compute:1-17` | 128/1024/2048/4096 + merge；不同线程数变体 | V1 先覆盖 128/1024/2048/4096；更大走 merge |
| prefix sum | `Shaders/VFXPrefixSum.compute:27-143` | Sklansky；1024/128 两种线程数；instancing 支持 | `shaders/vfx/vfx_prefix_sum.sc`；V1 固定单实例 |
| fill indirect args | `Shaders/VFXFillIndirectArgs.compute:31-120` | 每元素一个 uint 计数；quads/points/lines/meshes 分支 | `vfx/src/compiler/codegen.cpp`（fill_indirect kernel） |
| copy/zero/init dead list | `Shaders/VFXCopyBuffer.compute:40-86` | copy dword、zero init、dead list 初始化、bounds 初始化 | `vfx/src/runtime/runtime.cpp`（reset clear；deadListInit kernel） |
| 引擎内置 shader 绑定 | `Editor/Inspector/VFXManagerEditor.cs:43-47,184-188` | VFXManager 持有 Indirect/Copy/PrefixSum/Sort/StripUpdate compute | 适配层不再走 VFXManager；由 VFX runtime 直接持有 program |

## 4. 编译、资产、SRP

| 主题 | 原项目参考 | 行为 | 本项目落点 |
|---|---|---|---|
| 资产编译入口 | `Editor/Models/VFXGraph.cs:156-206`；`Editor/NewCompiler/VfxGraphCompiler.cs:30-48` | 引擎 importer 回调 `OnCompileResource` → graph 编译 → `VisualEffectAssetDesc`；编译 pass 顺序为 Output/AttributeLayout/DataLayout/... | `vfx/src/core/asset_json.cpp`（JSON → `vfx::Graph`）；`vfx/src/compiler/{codegen,compiler,binary}.cpp`（layout → kernel → shaderc → `.vfxb`）；`vfxbake` CLI 已实现 |
| 表达式 DAG/类型/折叠 | `VFXExpressionGraph.cs:59-113`；`VFXExpressionAbstract.cs:389-510`；`VFXExpressionMath.cs:9-604`；`VFXExpressionRandom.cs:38-104`；`VFXExpressionNoise.cs:9-203`；`VFXExpressionSampleCurve.cs:9-33` | 表达式图归约、逐通道运算、常量折叠、`Rand(attributes.seed)`/`SampleCurve`/noise codegen；类型含 float/floatN/int/uint/bool | `vfx/src/compiler/expression.cpp`（`compileExpression`/折叠/DAG 检测）；`vfx/include/vfx/expression.h`；parser 在 `asset_json.cpp:189-258` 校验；V1.0b.3 只生成宏表达式 |
| shader 创建 | `Editor/Models/VFXGraph.cs:1529-1535` | `ShaderUtil.CreateComputeShaderAsset/CreateShaderAsset` | `vfx/src/compiler/compiler.cpp` 调 `shaderc` 生成 dxbc/spirv blob；同源缓存；失败带 shaderc 输出 |
| 新编译器 Pass 顺序 | `Editor/NewCompiler/VfxGraphCompiler.cs:33-48` | Output/AttributeLayout/DataLayout/ParticleSystem/Spawner/BufferConversion/SystemTaskBindings/CodeGen/Lowering/Instancing | `vfx/src/compiler/codegen.cpp`（V1.0b.5 裁剪：layout→7 program，拒绝 subgraph/custom/lit/sort）；`vfx/src/compiler/binary.cpp` 序列化 |
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
| D7 | 自定义 HLSL | Unity 允许较完整 HLSL/子图 | 受限 `.sc` 子集；不可声明 buffer/uniform | 保证双后端可编译与可移植性 |
| D8 | 渲染 pass 排序 | VFX 透明与场景透明物体统一排序 | VFX 输出 pass 位于 `Forward Transparent` 之前 | V1 先保证顺序稳定；后续并入透明 DrawList |
| D9 | random seed 状态 | `Rand(attributes.seed)` 每求值推进粒子 seed（`VFXCommon.hlsl:465`） | V1.0b.3 固定 seed 的确定性 `VFX_RAND`；暂不推进 seed | V1.0b.5 仍使用固定 seed 的确定性 code；seed 推进/每节点 hash 留到 V1.1 生成器完善时关闭 |
| D10 | 粒子质量 | Drag/Force/Turbulence 使用 `mass` 属性并除以 mass | V1.1.2 固定 `mass=1.0`；没有 mass 属性/Set Mass | 避免为未使用的属性扩大全部 asset layout；增加 mass block 后关闭 |
| D11 | Turbulence 噪声 | `VFXNoise.hlsl` 的 Value/Perlin/Cellular curl + float cell hash；`FieldTransform` 变换 | 仅 Value curl；整数 Wang cell hash；无 FieldTransform；`scroll` 为扩展的 field 时间偏移 | 原 float hash 在 CPU/GPU 乘加重排下会超过 1e-3；整数 hash 保证双后端确定性。后续需要时再补 Perlin/Cellular/FieldTransform |
| D12 | Force space | Unity 力向量在 system 空间求值（没有显式 space slot） | 保留 `space` 字段但当前忽略，统一按 system 空间处理 | 避免在 compute 阶段引入未上传的 local→world 矩阵；后续加入 world transform 时关闭 |

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
| effect 资源 | `ParticleSystemDataDescriptionWriter.cs:61-82` | 每 effect 独立 attribute/deadList/indirection/renderCount/spawnCount/indirect/bounds buffer | `vfx/src/runtime/runtime.cpp:310` |
| dead list 初始化 | `Shaders/Temp/Data/DeadListData.hlsl:6-31` | 初始化 counter=capacity、free list 逆序；reset 时重新 dispatch | generated `dead_list_init`; `runtime.cpp:781` |
| 常量/Burst spawn | `Shaders/Temp/Templates/Init.hlsl:10-42`；`VFXSpawnerState` 行为；`VFXSpawnerConstantRate.cs`/`VFXSpawnerBurst.cs` | double 累积 rate + 单次/周期 Burst（period/repeat、同帧合并、1e-6 边界），GPU spawner 写 spawnCount；CPU reference 同步语义 | `runtime.cpp` `burstOccurrences`/`update`；`reference.cpp` `updateSpawner` |
| 力场 | `Gravity.cs:39-45`；`Drag.cs:50-64`；`Force.cs:61-67`；`ForceHelper.cs:32-40`；`Turbulence.cs:67-78`；`VFXNoise.hlsl:44-76` | Gravity/Drag（含 useSize）/Force（absolute/relative）/Attractor/Turbulence；`mass=1`；Turbulence 默认 Relative、Value curl noise、scroll=VFX_TIME 偏移；CPU reference 与 GPU 相同顺序和参数 | `codegen.cpp` `compileUpdateBlock`；`cpu_reference.cpp` `applyUpdateBlock`；`tests/assets/forces_1024.vfx.json` | 
| simulation 顺序 | `Init.hlsl`/`Update.hlsl`/`OutputUpdate.hlsl`/`RenderingData` | spawner→init→update→outputUpdate→fillIndirect，reset 时加 deadListInit | `runtime.cpp:781` |
| output | `Output.hlsl:4-60`；`VFXConfigPlanarPrimitive.template.hlsl:13-58` | 绑定 attribute/indirection/texture，按 output blend 状态 indirect draw | `runtime.cpp:861` |
| local transform | Unity local-space system | output VS `u_vfx_world`；local 用 effect transform，world 用 identity | `codegen.cpp:1059,1086` |
| Curve/Gradient | `VFXExpressionSampleCurve.cs`；RenderingData LUT | 默认 LUT 与 setCurve/setGradient 上传 RGBA16F；unlit shader 当前仍编译期采样 | `runtime.cpp:898`；`runtime_asset.cpp` |
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
| GPU readback | Unity `VFXManager`/buffer readback 仅用于工具/测试 | `bgfx::read` attributes/deadList/indirection/renderCount/indirect；不进入交互循环 | `src/render/vfx/vfx_reference.cpp` |
| golden | 固定相机/种子/帧数截图 | D3D11/Vulkan 640×360、3 帧、逐字节比较 | `vfx/tests/golden/vfx_scene.png`、`vfx/tests/run_vfx_golden.cmake` |
| 资产名 roundtrip | description writer name | header `assetNameOffset` 指回 strings；兼容旧 0 值 | `vfx/src/compiler/binary.cpp`、`vfx/src/runtime/runtime_asset.cpp` |

## 11. 实现时必须重新确认的源码点

- `HDRenderPipeline.RenderGraph.cs` 中 VFX camera buffer 的绑定时机（与我们的 output pass 前后关系）。
- `Sort.compute` 的 merge pass uniform 与 dispatch 组合（V1.2 迁移时逐段对照）。
- `VFXPrefixSum.compute` 的 instancing 宏（V1 先单实例，V1.2 事件多 system 时复核）。
- `VFXConfigPlanarPrimitive.template.hlsl` 的 `ShouldCullElement` / indirection 索引规则（输出 VS 必须一致）。
- `forward_renderer.cpp` 的 `bindShadowImsParams`/`bindShadowMomentSat` stage（Lit 可选路径）。
- `config/uniforms/*.idl` 的生成命名/前缀规则（新增 `vfx.idl` 时）。
