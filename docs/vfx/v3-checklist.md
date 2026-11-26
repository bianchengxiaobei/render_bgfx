# V3 VFX 渲染能力清单

> 状态：V3.0–V3.24 已完成（2026-11-26）。V3.0 粒子 motion vectors、V3.1 粒子阴影、V3.2 per-output motion-vector gate、V3.3 effect transform motion history、V3.4 automatic bounds v1、V3.5 automatic bounds padding v1、V3.6 culling flags v1、V3.7 output sort priority v1、V3.8 camera-depth/youngest sort criteria、V3.9 per-output sort direction、V3.10 per-output sort criteria、V3.11 output draw instancing、V3.12 per-output sorted indirection lists、V3.13 tight billboard automatic bounds v1、V3.14 per-output sort state、V3.15 effect simulation instancing v1、V3.16 VFX transparent effect-level ordering v1、V3.17 effect GPU instancing v2（instanced draw v1）、V3.18 effect GPU instancing v3（active instance indirection）、V3.19 effect GPU instancing v4（active-count indirect args）、V3.20 tight billboard automatic bounds v2（camera-aware AABB）、V3.21 cull-aware visible instance bounds、V3.22 fixed-size active instance split、V3.23 fixed-size shadow instance split 与 V3.24 VFX transparent per-batch insertion 均已落地。
> 原则：先读 Unity VFX Graph/HDRP 源码，再实现；与 Unity 的偏差写进本清单和 `source-alignment.md`。

---

## 1. V3.0 粒子 motion vectors

### 目标

- VFX transparent output 写 scene motion-vector buffer，使 TAA/motion blur 能重投影粒子。
- 保持 `.vfx.json`、`.vfxb` v6 与既有 `encodeOutput` 语义不变，只增加 additive runtime/renderer path。

### 参考

- `Shaders/VFXParticleCommon.template:199-220`：顶点阶段当前/上一帧 clip position。
- `Shaders/VFXCommonOutput.hlsl:311-323`；HDRP `Runtime/VFXGraph/Shaders/VFXCommon.hlsl:113-122`。

### 实现

- `vfx/shaders/vfx_motion_vectors.{vs,fs,def}.sc`：billboard MV program；读 `s_vfxAttributes` stage 12、`s_vfxIndirection` stage 13；`u_vfxMotionParams.w` 传 per-system attribute stride。
- `vfx/src/runtime/runtime.cpp`：`Runtime::encodeMotionVectors()` / `EffectImpl::encodeMotionVectors()`；复用 system indirect args，绑定 attributes/indirection。
- `vfx/src/compiler/codegen.cpp`：init/update 维护 `oldPosition`；VS 用 `u_global_nonJitteredViewProj` 与 `u_global_prevViewProj` 计算 current/previous clip。
- `FrameSettings::MotionVectors` 仍门控 pass；透明 output 用纹理 alpha cutoff 0.001，opaque 不裁。

### 验证与限制

- D3D11/Vulkan `--debug-view=motion-vectors` on/off 差异；静态粒子 on/off 0-diff；双后端 shader 编译。
- 上一帧 billboard offset 复用当前 size/angle，尺寸/旋转突变时 MV 是近似值；无 flipbook、逐顶点 strip/mesh 或 soft-particle 边缘裁剪。V3.2 增加 gate，V3.3 增加 effect transform 历史。

## 2. V3.1 粒子阴影

### 目标

- per-output `castShadows` 把粒子写入 directional/punctual/area shadow atlas；透明 output 按 alpha clip。

### 参考

- `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:132,149,600`。
- `Shaders/ParticlePlanarPrimitives/PassShadowCaster.template`、`PassDepthOrMV.template:91-106`。

### 实现

- JSON `castShadows` 缺省 false；`.vfxb` v6 OUTPUTS reserved byte 21；旧资产为 0。
- 固定 `vfx.shadow` program；VFX simulation 在 shadow pass 前执行；存在 VFX shadow 时禁用 static shadow cache。
- 透明 output 用 `s_vfxMain` alpha < 0.001 discard；opaque 不裁；depth write/LessEqual/无 back-face cull。

### 验证与限制

- 双后端 forward/deferred shadow atlas 差异验收；shadow on 时 `vfx_shadow_draws=4`，off 时 0；30 个资产。
- 未实现 normal bias、alpha-to-coverage、flipbook/逐顶点 shadow、per-output caster shader；punctual/area 光源共用 shader/matrix。

## 3. V3.2 per-output motion-vector gate

### 目标

- 允许单个 output 关闭 MV，而不改变 V3.0 默认行为。

### 实现

- `OutputDesc::generateMotionVector` 缺省 true；JSON false 时 `Runtime::encodeMotionVectors` 跳过该 output。
- `.vfxb` v6 OUTPUTS reserved byte 22：0/旧资产 = 启用，1 = 禁用；record 大小/version 不变。
- `Document::setOutputGenerateMotionVector()`；Inspector `Generate motion vectors`；graphHash 包含该字段。

### 验证与限制

- `shadow_visual`（shadow on + motion off）双后端 motion debug on/off 0-diff；既有 MV on golden 继续通过。
- 缺省 true 是 V3.0 兼容选择（Unity 缺省 false）；未实现 flipbook/逐顶点 MV 或 soft-particle 边缘裁剪。

## 4. V3.3 effect transform motion history

### 目标

- local-space system 的 effect transform 逐帧平移/旋转进入 motion vectors。

### 实现

- `EffectImpl` 保存 `m_transform` / `m_previousTransform`；每帧首次 `setTransform` 提交历史，首帧 previous=当前。
- MV pass 懒查找 `u_vfx_prevWorld`；local system 上传上一帧 transform，world system 上传 identity。
- `reset()` 将 previous 重置为当前，避免首帧/重置虚假运动；`update()` 清除本帧提交标记。

### 验证与限制

- `hbg_vfx_runtime_tests` 捕获 `u_vfx_world`/`u_vfx_prevWorld`，验证 1/1 → 2/1 → 3/2 和 reset 4/4；双后端 MV/gate 继续通过。
- 上一帧 billboard offset 仍用当前 size/angle；单帧多次 `setTransform` 只保留第一次历史。

## 5. V3.4 automatic bounds v1

### 目标

- system `bounds.mode:"automatic"` 用 GPU alive 粒子 AABB readback 更新 `Effect::bounds()`。

### 实现

- JSON `bounds.mode:"automatic"`；`SYSTEMS` reserved byte 60 保存 mode；旧资产 byte 0 = manual。
- compiler 增加 `bounds_init`/`bounds` kernel；alive + position/size/scale 保守半径，FloatFlip atomic min/max。
- runtime async readback + local transform；首帧前使用 fallback bounds；`EffectDesc::hasBoundsOverride` 仍优先。
- editor 支持 Manual/Automatic；automatic 隐藏 center/extents。

### 验证与限制

- `auto_bounds.vfx.json`/`.vfxb` 为第 30 个 checked-in 资产；mock readback、compiler kernel、D3D11/Vulkan reference 均通过。
- V3.4 使用保守 3D 球半径；V3.13 改为 flat billboard 的 2D 平面球半径；V3.20 进一步按 camera right/up、angle、scale 计算 tight billboard AABB，仍不构造 Unity axis/angle/pivot tight OBB；每 automatic system 多两个 dispatch；Unity automatic 强制 `CullNone`，本地自动仍 always-simulate。

## 6. V3.5 automatic bounds padding v1

### 目标

- 对齐 Unity `boundsPadding`，在 GPU readback/fallback AABB 上加 CPU padding。

### 实现

- JSON `bounds.padding`，仅 automatic 生效；`EffectImpl::refreshBounds()` 在 local transform 前扩展 AABB。
- 非零 padding 写可选 `.vfxb` v6 `BOUNDS` section（id 11，`u32 count + count * 3*f32`）；零 padding 不写，旧资产字节不变。
- editor automatic system 暴露 `Bounds padding`；`setSystemBounds()` 切回 manual 时保留 padding。

### 验证与限制

- runtime mock、compiler/parser、D3D11/Vulkan reference readback 验证；V3.5 时 padded bounds 约从 `z=±0.173` 扩到 `±0.923`，V3.13 收窄半径后为 `z=±0.1414` 扩到 `±0.8914`。
- V3.20 是 camera-aware tight AABB，不是 OBB；V3.13 的 2D 球半径仍作为 fallback，V3.20 收窄到 billboard 的 oriented extents。

## 7. V3.6 culling flags v1

### 目标

- asset-level culling flags + renderer camera-frustum culling，避免不可见 effect 的仿真/绘制。

### 实现

- JSON `culling` = `none` / `simulation` / `visible`，默认 none；非零写可选 `CULLING` section（id 12，`u32 flags`）。
- `CullSimulation` 暂停 `update()` 时间/spawn 与 `encodeSimulation()` dispatches；culled effect 跳过 output/MV draw。
- `ForwardRenderer::updateVfx()` 用 `Effect::bounds()` 与 frustum planes 设置 `Effect::setCulled()`。
- automatic bounds 拒绝非零 culling；editor 切 automatic 时清除 culling。

### 验证与限制

- runtime mock 覆盖 simulation/output suppression；parser/compiler/editor 覆盖 flags、automatic 拒绝与 BINARY round-trip；reference/motion 测试双后端通过。
- shadow request 独立于相机 culling；culling 是 asset 级，不是 per-output。

## 8. V3.7 output sort priority v1

### 目标

- 同一 asset 的多个 output 可按显式 priority 决定提交顺序。

### 实现

- JSON output `sortPriority`（int32，越小越先）；`.vfxb` v6 OUTPUTS offset 28；缺省 0，旧资产字节不变。
- `EffectImpl::m_outputOrder` 按 priority 升序稳定排序；color/MV/shadow encode 共用该列表。
- editor Output Inspector `Sort priority`；非零值节点摘要显示 `order N`；`Document::setOutputSortPriority()` 0 时删除字段。

### 验证与限制

- runtime patched `vfx_event.vfxb` 反转 Emitter/Receiver draw 顺序；parser/compiler/editor round-trip；双后端 order/force/off golden 通过。
- 只影响同一 effect 的 output 提交顺序，不改变多 effect 或 VFX/场景 transparent DrawList 的全局排序；particle-internal sort 独立。

## 9. V3.8 camera-depth / youngest sort criteria

### 目标

- 补齐 Unity `VFXSortingUtility.SortCriteria` 四种 criteria 中的 `cameraDepth` 与 `youngest`。

### 参考

- `Editor/Models/Contexts/VFXSortingUtility.cs:9-56`。
- `Editor/NewCompiler/Builder/OutputUpdateTaskBuilder.cs:95-125`；`Shaders/VFXParticleCommon.template:337-365`。

### 实现

- `SortMode` 新增 `CameraDepth` / `YoungestInFront`；JSON `sort.mode` 接受 `"cameraDepth"` / `"youngest"`；system/output 必须匹配。
- `.vfxb` v6 SYSTEMS/OUTPUTS `sortMode` 枚举值 3/4；旧 0/1/2 和旧资产字节不变。
- `output_update`：`cameraDepth` key = `dot(worldPosition - cameraPosition, cameraForward)`；`youngest` key = `age`；复用 ascending/descending KVP comparator。
- runtime `cameraDepth` 上传 inverse view/camera position/world；editor Sort combo 增加两项；CPU reference 支持两种 key。

### 验证与限制

- parser/compiler/BINARY round-trip；D3D11/Vulkan 1024 粒子 `--vfx-reference` 两种 mode 均 pass；distance/custom golden 不变。
- V3.8 时所有 output 共享 system sort/indirection；V3.10 起 output criterion 可由额外 pipeline 覆盖，但仍串行复用同一份 KVP/indirection。新增 mode 的资产需重烘焙，旧 runtime 拒绝新枚举值。

## 10. V3.9 per-output sort direction

### 目标

- 对齐 Unity output-level `revertSorting`：同一 system 的 criterion/indirection 共享，但每个 output 可独立选择 ascending/descending 绘制方向。
- 保持 `.vfx.json` output `sort` 字符串兼容；新增可选 `sortOrder`；`.vfxb` v6 继续使用 OUTPUTS offset 11，record 大小/version 不变。

### 参考

- `Editor/Models/Contexts/VFXAbstractParticleOutput.cs:117-122`（`sortMode`、`revertSorting`）。
- `Editor/Models/Contexts/VFXOutputUpdate.cs:207-218`（per-output `SORTING_SIGN`）；`Shaders/VFXParticleCommon.template:337-365`。

### 实现

- `OutputDesc::sortOrder` 可选覆盖；parser 接受 `"ascending"` / `"descending"`；缺省继承 system order；system 未排序时拒绝。
- compiler 将 effective order 写入 `OutputProgram::sortOrder`/OUTPUTS offset 11；反向 output VS 读 stage 14 `RenderCount` 并反转 `drawParticle`。
- runtime `RenderCount` 提升为 `StorageVertex`；MV/shadow 通过 `u_vfxMotionParams.z` / `u_vfxShadowParams.w` 反向。
- editor `Document::setOutputSortOrder()`；Output Inspector `Sort order`（Inherit/Ascending/Descending）；`setSystemSort()` 保留显式 override。

### 验证

- `hbg_vfx_tests` **96/96, 38,262 assertions**：`sortOrder` 解析/错误定位/默认继承、editor round-trip。
- `hbg_vfx_compiler_tests` **28/28, 575 assertions**：反序 VS 包含 `s_vfxRenderCount`/反向 index 与 stage 14 binding；OUTPUTS offset 11 round-trip。
- `hbg_vfx_runtime_tests` **25/25, 576 assertions**：`sort_visual` output order 与 system desc 相反时 MV `u_vfxMotionParams.z=1`。
- 临时 `reference_sorted_1024` 加 `sortOrder:"descending"` 后重烘焙，D3D11/Vulkan `--vfx-reference` 1024 粒子均 pass；`hbg_vfx_asset_check` **432.15s** 30 个既有资产字节不变；order/force/motion/shadow golden、editor compile/smoke 通过；CTest **137**。

### Known limits

- V3.9 只支持 per-output 方向；V3.10 通过串行重排补齐 per-output criterion，但所有 output 仍复用一份 KVP/indirection，不是并发独立列表。
- 反向绘制会反转相等 key 的 particle-index tie 顺序。
- 新 `sortOrder` 与 system order 不同的资产需要重新编译；旧 runtime 不反向绘制，旧资产缺少新字段时自动继承。
- VFX 与场景 transparent DrawList 的全局合并仍未完成（V1 D8）。

## 11. V3.10 per-output sort criteria

### 目标

- 对齐 Unity 的 per-output sorting criterion：同一 system 的不同 output 可选择 distance/custom/cameraDepth/youngest，而不是只能继承 system criterion。
- 保持 `.vfxb` v6、既有 buffer 数量和旧资产字节不变；不为每个 output 分配独立 sorted buffer。

### 参考

- `Editor/Data/VFXDataParticle.cs:614-666`：`NeedsGlobalSort` 与 `OutputNeedsOwnSort`，criterion/revert 不同时 output 需要 own sort。
- `Editor/Models/Contexts/VFXOutputUpdate.cs:41,215`：`sortCriterion = output.GetSortCriterion()` 与 `SORTING_SIGN`。
- `Editor/Models/Contexts/VFXSortingUtility.cs:73-82`：`OutputNeedsOwnSort` 比较完整 criterion。

### 实现

- compiler 对每个非 system criterion 生成额外 `SortReset` + `OutputUpdate` + sort pipeline；共享 KVP/Indirection/RenderCount，不新增 buffer/section。基础 pipeline 在存在 extra criterion 时也改用 `u_vfxSortWorld`/`u_vfxSortCameraPos`/`u_vfxSortInvView`，避免与 color/MV 的 `u_vfx_world`/`u_global_*` 同 encoder 重复设置。
- runtime 在 color/MV/shadow draw 前按 output criterion 串行重排：`SortReset` 清零 RenderCount，再 dispatch output update/sort/fillIndirect；相同 criterion 的连续 output 复用当前列表。`sortMode` 通过 KERNELS reserved byte 3 tag（旧资产为 0）；`SortReset` 使用 task 16。
- output binary/`AssetProgram.sortMode` 写 effective output criterion；`sortOrder` 仍单独控制方向；system criterion 是缺省和 base pipeline。
- editor Output Inspector 增加 `Sort criterion`；`Document::setOutputSortMode()` 写 `output.sort`，`setSystemSort()` 只更新仍跟随旧 system criterion 的 output，显式 override 保留。

### 验证

- `hbg_vfx_tests` **98/98, 38,286 assertions**：per-output criterion 解析、editor criterion/order round-trip、system sort change 保留 override。
- `hbg_vfx_compiler_tests` **29/29, 599 assertions**：extra pipeline 的 reset/task/tag、youngest key、sort uniform 隔离、`SortReset` task 16 BINARY round-trip。
- `hbg_vfx_runtime_tests` **26/26, 589 assertions**：`sort_outputs` 的 distance→youngest draw 前插入 Output Sort pass，额外 1 次 sortPass，2 次 output draw。
- `sort_outputs.vfxb` 为第 31 个 checked-in 资产；`vfxbake --check` 全量 31 个资产通过，既有 30 个资产的生成字节保持不变；D3D11/Vulkan `hbg_vfx_sort_outputs_{d3d11,vulkan}` render smoke 通过（无 duplicate uniform/fatal），CTest **139**。

### Known limits

- v1 用串行 re-sort 复用一个 KVP/indirection；V3.12 为额外 criterion 增加独立 sorted indirection，KVP/RenderCount 仍共享；并发 GPU 分区排序仍留待后续。
- 只有 system sort 开启时支持 output criterion；不允许单个 output 关闭排序。
- shadow 重排使用最近一次 simulation 的相机 frame；shadow 本身仍用独立 request/matrix。
- 含额外 pipeline 的资产带 task 16，旧 runtime 会拒绝加载；现有资产不含该 task，字节保持 v6 兼容。

## 12. V3.11 output draw instancing

### 目标

- 对齐 Unity quad output 的 2048 粒子/draw instance 分块：一次 indirect draw 用 `instanceID` 选择粒子切片，而不是把全部 alive 粒子放在一个超长顶点流中。
- 保持现有 6 顶点/粒子的 V1/V3 quad 几何、`.vfxb` v6 section/record 布局和 runtime API；本阶段只做 output draw 分块，不做多 effect 共享仿真。

### 参考

- `Editor/NewCompiler/Passes/VfxTemplateCodeGenerationPass.cs:12-15,546-576`：`k_QuadParticlesPerDrawInstance = 2048`，output `linearThreadIndex = (id >> 2) + input.instanceID * 2048`；注释要求与 native `VFXParticleSystem::RenderQuadCommand` 的 `kNbParticlePerInstance` 一致。
- Unity 的 output 每 instance 绘制一个 capacity 切片；本项目保留 6 顶点 quad，因此用 `vertexID / 6 + instanceID * 2048` 映射。

### 实现

- `fill_indirect` 写 `numVertices = 6 * min(count, 2048)`、`numInstances = ceil(count / 2048)`，`startVertex/startInstance=0`；count 小于一个 chunk 时仍只有一个 instance，不额外 overdraw。
- output VS 与共享 MV/shadow VS 始终绑定 stage 14 `RenderCount`，用 `gl_InstanceID` 计算 `linearParticle`；`linearParticle >= renderCount` 时输出退化顶点，避免最后 partial instance 越界读取。反向排序在 guard 之后对 `linearParticle` 取反。
- runtime `drawIndirect` 不增加 API；D3D11/Vulkan 由 indirect args 的 instanceCount 驱动。所有 output 的 `AssetProgram.vertexBindings` 固定包含 stage 14。
- 本阶段没有新增 buffer/section，也不改变 v6；但 all outputs 的生成 shader 和 `fill_indirect` 变化，所有 checked-in `.vfxb` 必须重烘焙。

### 验证

- `hbg_vfx_tests` **98/98, 38,286 assertions**。
- `hbg_vfx_compiler_tests` **30/30, 614 assertions**：output VS 的 `gl_InstanceID`/2048 映射、stage 14 binding、`fill_indirect` 的 chunk 参数。
- `hbg_vfx_runtime_tests` **26/26, 592 assertions**：output 命令序列新增 stage 14 RenderCount binding，draw 仍为 1 次 indirect。
- `reference_sorted_8192` D3D11/Vulkan `--vfx-reference` 通过，indirect args `[12288, 4]`；`reference_sorted_262144` 双后端通过（128 instances）；临时 `reference_2560` partial chunk D3D11/Vulkan 通过，indirect `[12288, 2]`。
- `sort_outputs`、MV、forward/deferred shadow 双后端 smoke 通过；同一 8192 粒子场景用 HEAD 旧 `.vfxb`（1 instance 旧映射）与当前 `.vfxb`（4 instances）在 D3D11/Vulkan 截图逐字节相同；`hbg_vfx_asset_check` **292.82s** 通过，31 个资产；`hbg_vfx_editor_roundtrip` **553.02s** 通过，31 个资产；CTest **139**。

### Known limits

- 仅做 output draw 分块，不等同于 effect GPU instancing：多个 scene VFX instance 仍各自创建 simulation/buffer，未共享仿真；instance split/property split 未实现。
- Unity 的 indexed fill indirect 只在 count > 16384 时按 2048 分 instance；本项目 v1 固定按 2048 分块，输出像素一致但 draw instance 数和少量空 tail 顶点不同，这是有意的简化。
- 保留 6 顶点非索引 quad，不是 Unity 的 4 顶点 + 6 index；最后 partial instance 会提交一个 chunk 的顶点并由 VS guard 丢弃无效粒子。
- 所有资产因 shader/fillIndirect 变化需要重烘焙；旧 runtime 不会理解新的 instanceCount 语义，需使用当前 runtime。
- VFX 与场景 transparent DrawList 的全局合并仍未完成（V1 D8）。

## 13. V3.12 per-output sorted indirection lists

### 目标

- 让不同 per-output criterion 保留各自的 sorted indirection，避免 color/MV/shadow 在 criterion 切换时反复重排同一份列表。
- 保持 `.vfxb` v6、compiler、公开 API 与既有 buffer/section 不变；本阶段只在 runtime 增加额外 sorted indirection。

### 参考

- `Editor/Data/VFXDataParticle.cs:1470-1500`：每个 output sort task 使用自己的 `srcBuffer`，并以 `dstBuffer = sortedIndirect + j` 接收结果。
- `Editor/Data/VFXDataParticle.cs:614-666`：`NeedsGlobalSort` 与 `OutputNeedsOwnSort`；不同 criterion 的 output 需要 own sort。
- `Editor/Models/Contexts/VFXSortingUtility.cs:73-82`：比较完整 criterion；`Shaders/Sort.compute` 只负责重排 KVP/indirection。

### 实现

- `SystemRuntime` 为每个额外 criterion 创建一个 `SortIndirectionRuntime`（`StorageVertex`、`capacity * 4`）；名称为 `vfx.<system>.sorted.<mode>`，仅在资产含 extra sort pipeline 时分配。
- KVP、`RenderCount`、indirect args 仍共享；额外 pipeline 的 `SortReset`/`OutputUpdate`/sort 写入该 criterion 的 indirection，`fill_indirect` 仍写共享 indirect args。
- `m_sortGeneration` 随实际 simulation 递增；基础 indirection 由 simulation 的 base pipeline 产生，额外列表在首次 color/MV/shadow 使用时排序一次并记录 generation。`outputIndirection()` 为每个 output 选择 stage 13 buffer，color/MV/shadow 都绑定对应列表。
- `sort_outputs` 增加第三个 distance output（distance → youngest → distance），验证额外列表排序后基础列表仍可直接复用；runtime 单测检查 color/MV 的 stage 13 绑定顺序为 base → youngest → base，且 MV 不再触发额外 sort。
- 没有修改 codegen 或二进制格式；`sort_outputs.vfxb` 因 fixture 新增 output 而重烘焙。

### 验证

- `hbg_vfx_runtime_tests` **26/26, 605 assertions**：额外 sorted indirection 生命周期、3 个 output 的 base/extra/base 绑定、MV 复用列表且 `Output Sort` pass 为 0。
- `hbg_vfx_tests` **98/98, 38,286 assertions**；`hbg_vfx_compiler_tests` **30/30, 614 assertions**；`hbg_vfx_adapter_tests` **8/8, 328 assertions**。
- `hbg_vfx_sort_outputs_d3d11` / `_vulkan` smoke 通过；`hbg_vfx_shadows*`、`hbg_vfx_motion_vectors*` 双后端 smoke 通过；32 个 feature golden serial 全通过。
- `hbg_vfx_asset_check` **472.10s** 通过 31 个资产；`hbg_vfx_editor_roundtrip` **636.04s** 通过 31 个资产；CTest **139**。

### Known limits

- 列表按 unique criterion 共享，不是每个 output 一份；同 criterion 的多个 output 复用同一 indirection。
- 基础列表仍由 simulation 的相机 frame 产生；暂停且相机移动时 distance/cameraDepth 列表不会重新排序（与既有基础排序一致）。
- V3.12 时不同 criterion 仍按首次使用串行排序一次；V3.14 为每个 extra criterion 增加独立 KVP/RenderCount/indirect args，仍没有并发 GPU sort 或分区 sort。
- V3.12 的额外 indirection 和 V3.14 的额外 sort state 都是 runtime-only buffer，不改变 `.vfxb`；含 extra pipeline 的资产仍要求 V3.10+ runtime（task 16）。
- VFX 与场景 transparent DrawList 的全局合并仍未完成（V1 D8）。

## 14. V3.13 tight billboard automatic bounds v1

### 目标

- 将 automatic bounds 的 per-particle 半径从 3D scale 球收窄为 flat billboard 的 2D 平面球；不改变 `.vfxb` section/version、bounds mode 或 runtime API。
- 明确本阶段仍不是 OBB：带 axis/angle/pivot 的 tight OBB/extents 继续留待后续。

### 参考

- `VFXBoundsUtils.hlsl:52-59`：`ComputeBoundingRadius2DFromMatrix()` 对平面元素使用 `0.5 * sqrt(xAxisSqrLength + yAxisSqrLength)`，不包含 z 轴长度。
- `VFXBoundsUtils.hlsl:61-65`：3D 元素使用 `GetBoundingRadius3D_Local()`；本地 quad output 是 flat camera billboard，输出偏移只乘 `scale.x`/`scale.y`，因此 `scale.z` 不应扩大半径。

### 实现

- `makeBoundsSource()` 的 per-particle 半径改为 `0.5 * abs(size) * length(scale.xy)`；`scale.z` 仍保留在 attribute layout，但不参与 bounds。
- `auto_bounds` fixture 的 init 增加 `set_scale [1,1,4]`，用真实 GPU readback 验证 `scale.z` 不进入半径；只有 automatic bounds 的生成 shader/`auto_bounds.vfxb` 改变，其他 checked-in 资产保持字节一致，没有新增 section 或 binary version。
- `hbg_vfx_compiler_tests` 检查 bounds shader 含 `length(scale.xy)` 且不含 `length(scale)`。

### 验证

- `hbg_vfx_compiler_tests` **30/30, 616 assertions**；`hbg_vfx_runtime_tests` **26/26, 605 assertions**；`hbg_vfx_tests` **98/98, 38,286 assertions**。
- `hbg_vfx_reference_auto_bounds_d3d11` / `_vulkan` 通过；D3D11 日志 readback 为 `min=(-1.3821943,-0.6414214,-0.8914214)`、`max=(1.3291512,0.6414214,0.8914214)`；fixture 的 `scale.z=4` 未扩大 z 范围，确认只使用 XY 平面半径 `0.1414` 加 padding `[0.25,0.5,0.75]`。
- `vfxbake --check vfx/tests/assets` 通过全部 31 个资产（357.52s）；`hbg_vfx_editor_roundtrip` 通过 31 个资产（312.85s）；CTest **139**。

### Known limits

- 仅对 flat billboard 收窄；mesh/3D 元素、带 axis/angle/pivot 的 tight OBB 仍未实现。
- bounds 仍是保守 AABB readback；没有逐粒子 tight AABB，也不使用 camera-dependent bounds。
- V3.5 padding 仍是 AABB 轴对齐扩展，不是 OBB padding。

## 15. V3.14 per-output sort state

### 目标

- 为每个额外 sort criterion 保存独立的 KVP、RenderCount 和 indirect args，避免 extra sort 覆写 base 的 render state；base 与 extra output 可以在同一帧直接复用各自的列表和 args。
- 保持 `.vfxb` v6、compiler、公开 API 和既有 asset 不变；只在 runtime 分配额外 buffer。

### 参考

- `Shaders/VFXOutputUpdate.template:31-45`：`Kvp`/`IndirectOutputType` 与 `outputBuffer0..N`；每个 output 的 output-update/indirect 状态独立。
- `Editor/Data/VFXDataParticle.cs:1478-1493`：per-output sort task 使用自己的 `srcBuffer`，写入自己的 `dstBuffer`。
- `Shaders/VFXFillIndirectArgs.compute:60-68`：fill indirect 根据对应 output 的 RenderCount 写 args。

### 实现

- `SortOutputRuntime` 除 indirection 外新增 `kvp`、`renderCount`、`indirect`；名称分别为 `vfx.<system>.sorted.<mode>`、`.kvp`、`.count`、`.indirect`。
- `SortBufferSet` 让 compute/output binding 按 output criterion 选择 buffer；`ensureOutputSort` 的 reset/output-update/sort/fillIndirect 全部写入该 criterion 的 set。`fillIndirect` 现在写 extra args/count/KVP，而不是共享状态。
- color/MV/shadow 分别绑定对应 stage 13 indirection、stage 14 RenderCount，并从对应 set 的 indirect args draw；base output 继续使用 system 的 base buffers。
- 没有 compiler/binary/shader 改动，也没有资产重烘焙。

### 验证

- `hbg_vfx_runtime_tests` **26/26, 628 assertions**：`sort_outputs` 的 color/MV draw 对 stage 13、stage 14、indirect args 都是 base → youngest → base；额外 sort 后 base 状态不变，MV 不再触发额外 sort；新增 buffer 全部回收。
- `hbg_vfx_tests` **98/98, 38,286 assertions**；compiler **30/30, 616**；adapter **8/8, 328**。
- `hbg_vfx_sort_outputs_d3d11`/`_vulkan`、`reference_sorted_8192`、forward/deferred shadows、motion vectors/gate 共 12 个双后端 smoke 通过；32 个 feature golden serial 全通过；CTest **139**。

### Known limits

- sort state 仍按 unique criterion 共享，不是每个 output 一份；同 criterion 的多个 output 复用同一 set。
- 额外 criterion 仍在首次使用时串行排序；scratch buffer 共享，没有并发 GPU sort 或分区 sort。
- `.vfxb` 仍不保存 per-output sort state；重启 runtime 后额外 buffer 需要重新创建。

## 16. V3.15 effect simulation instancing v1

### 目标

- 让同一 asset、相同 seed/property/playback 设置的多个 scene VFX instance 共享一次 GPU simulation；每个 instance 保留自己的 transform、bounds、frustum culling、color/MV/shadow draw。
- 作为 opt-in：`--vfx-instancing` / `PipelineSettings::supportVFXInstancing` 默认关闭；关闭时保持 V3.14 的一 effect 一 simulation 行为。

### 参考

- `Shaders/VFXInstancing.hlsl:1-115`：`instancingCurrentCount`/`ActiveCount`/`BatchSize`、current → active → batch indirection，以及 `VFXInitInstancing()` 的 instance/active/batch index。
- `Shaders/VFXInit.template:74-112`、`Shaders/VFXUpdate.template:58`、`Shaders/VFXParticleCommon.template:313-325`：init/update/output update 通过 `VFXInitInstancing` 按 instance 取 spawn/count。
- `Editor/Data/VFXDataParticle.cs:918-920,1505-1509`：`instancingIndirectAndActiveIndirect` 与 `VFXInstancesPrefixSumBuffer` 的 per-system 布局；`AddInstanceSplitDesc` 去重 instance split。
- `Editor/NewCompiler/Passes/OutputPass.cs:300-312`：`SystemUsesInstancedRendering` 和 `instanceSplitDescs`。

### 实现

- Runtime `Effect` 增加 additive API：`setInstanceTransforms`、`setInstanceCulled`、`instanceBounds`、`instanceCount`、`supportsEffectInstancing`。`EffectImpl` 保存每个 instance 的 current/previous world、local bounds 和 cull 状态；simulation 只执行一次。
- `encodeOutput`/`encodeMotionVectors`/`encodeShadow` 在 instance 外层循环，逐 instance 写入 `u_vfx_world`/`u_vfx_prevWorld` 并 draw；MV 的 previous transform 逐 instance 维护，shadow 不受 camera cull 影响。
- `supportsEffectInstancing` 只接受 **全部 local-space system** 且所有 output sort 为 `none`/`youngest` 的 asset；`distance`/`cameraDepth`/`custom` 仍逐 effect 独立 simulation。
- ForwardRenderer 在 scene 初始化时按连续 instance 的 asset path、seed、playRate、playOnAwake 和 property 列表分组；组内复用同一个 `Effect`。新增 `supportVFXInstancing` pipeline 字段和 `--vfx-instancing`；日志输出 `active effects/instances/instancing`。
- 不修改 `.vfxb`、compiler、shader 或 checked-in 资产；不改变 V3.14 的 per-output sort state。

### 验证

- `hbg_vfx_runtime_tests` **29/29, 671 assertions**：新增 2-instance transform/bounds/draw、per-instance previous transform MV、world-space 拒绝测试。
- `hbg_vfx_instancing_d3d11`/`_vulkan`：关闭时 `active effects=2 instances=2`，开启时 `active effects=1 instances=2`；两张截图逐字节相同。
- `hbg_vfx_golden_*` **32/32**、sort/reference/shadow/motion smoke **12/12**、auto-bounds reference 双后端通过；`hbg_vfx_tests` **98/98, 38,286**；adapter **8/8, 328**；`hbg_tests` **309/33,433**；compiler 30/30（616，未改 compiler）；CTest **141**。

### Known limits

- v1 只共享 simulation 和 GPU buffer，不共享 draw：每个 instance/output 仍单独 `drawIndirect`；没有 Unity 的 instance split、prefix-sum batch、active/batch indirection 或 instanced output draw。
- 只支持 local-space 且 output sort 为 `none`/`youngest`；不同 property/seed/playRate/playback state 不分组；simulation clock 必须一致。
- 分组在 scene 初始化时决定；hot reload 不重新评估 grouping。per-instance cull 是 CPU frustum 结果，未做 GPU compaction。
- VFX 与场景 transparent DrawList 的完整逐对象排序仍未实现；V3.16 只做 effect-level（V1 D8 部分收口）。

## 17. V3.16 VFX transparent effect-level ordering v1

### 目标

- 在保持 VFX 内部粒子排序不变的前提下，把每个 effect 作为一个透明批次插入场景 transparent DrawList 的 effect-level 深度位置；同一 effect 不再固定位于所有透明物体之前或之后。
- 作为 opt-in：`--vfx-transparent-sort` / `PipelineSettings::supportVFXTransparentSort` 默认关闭；关闭时保持 V1 D8 的“VFX output 先于 transparent”行为。

### 参考

- `Runtime/RenderPipeline/HDRenderPipeline.cs:3460-3499`：`CreateTransparentRendererListDesc()` 对透明 forward 使用 `SortingCriteria.CommonTransparent | SortingCriteria.RendererPriority`，由 RendererList 按透明物体键排序。
- `Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs:955-965`、`:1899-1911`：forward transparent RendererList 的创建与绘制入口；HDRP 的透明物体共享 renderer list 排序。
- `Editor/Models/Contexts/VFXSortingUtility.cs:9-56`、`Shaders/VFXParticleCommon.template:337-365`：VFX 内部粒子仍按 distance/camera depth/youngest/custom 的 KVP 排序；本阶段不改变这部分。
- hdrp-bgfx `DrawList` 已使用 `kTransparentSortingCriteria`（sorting layer、render queue、renderer priority、sorting order、back-to-front、state）；V3.16 复用同一键比较 effect-level submission。

### 实现

- Runtime 新增 additive `Runtime::beginOutputFrame()` / `Runtime::encodeOutputEffect(sink, frame, effect)`；前者每帧清零 VFX draw timing/diagnostics，后者只编码指定 effect。`encodeOutput()` 内部改为 begin + 逐 effect 编码，保持旧单 pass 行为。
- `Effect` 分组保持不变；renderer 遍历 `m_vfxInstanceRuntimes`，以 `Effect::bounds()` 中心到相机距离生成透明 effect key（透明 render queue 3000），并按 `kTransparentSortingCriteria` 找到插入位置。多个 effect 按同一键稳定排序；effect 内部仍按 output priority/粒子排序绘制。
- `DrawList::buildRange()` 从已排序的 `m_transparentDrawList` 复制一段 items 并重建 batch；`m_vfxTransparentSegments` 持有这些子列表的生命周期。Forward/Deferred 的透明段通过现有 `addForwardPass(..., listOverride, ...)` 绘制，VFX effect pass 用 `addVfxOutputPass(..., effect, ...)` 插入段之间。
- Forward 与 Deferred 的透明尾部统一走 `addTransparentFramePasses()`；关闭设置、存在 pre-refraction、transparent backface、或没有透明物品时回退旧路径。排序路径只跑主 transparent pass，不合并 refraction/backface；GPU-driven transparent 路径会退化为 CPU 透明段绘制，保证 effect-level 顺序。
- 新增 `supportVFXTransparentSort` pipeline 字段、JSON 序列化、`--vfx-transparent-sort` CLI 和 `tests/render/test_frame_settings.cpp` round-trip；不修改 `.vfxb`、compiler shader、asset 或 V3.15 的 simulation 分组。

### 验证

- 新 fixture `vfx_transparent_order.gltf` / `.hdrp.json`：透明面板位于 `z=1.4`，两个 `curve_unlit` effect 分别位于 `z=0.5`（远）和 `z=2`（近）；关闭设置时单个 VFX Output pass 在面板前，开启时 pass order 为“远 VFX → 面板 → 近 VFX”。
- `hbg_vfx_transparent_sort_d3d11` / `_vulkan`：检查 `effects=2`/日志，off/on 截图必须不同（D3D11 实测 4,680 diff pixels、max delta 81），on 截图与 checked-in golden 在 4 pixels/1 channel 容差内；双后端 on golden 互相 0 diff。
- 既有 `hbg_vfx_golden_*` 32/32、16 个 reference、11 个 shadow/motion/sort smoke、runtime 29/29（671）、`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、compiler 30/30（616）、`hbg_tests` 309/33,443 通过；CTest **143**。
- `git diff --check` 通过。

### Known limits

- 只做 **effect-level** 排序：一个 effect 的所有粒子仍在同一个 VFX pass 内按内部 KVP 排序，不能与透明 mesh 或粒子逐对象交错；完整 D8 仍需 indirect batch/逐粒子深度键。
- effect key 使用 bounds 中心距离，不是逐粒子或逐 output 的精确深度；透明队列按 `kRenderQueueTransparent` 处理，不复制 Unity RendererList 的完整逐 renderer 队列语义。
- pre-refraction、transparent backface、GPU-driven transparent 路径不参与 effect-level 合并，分别回退旧 pass；透明段会按 VFX effect 数量拆分，可能增加 pass 数。
- VFX motion vectors、阴影和仿真顺序保持不变；排序只影响 color transparent pass。
- 分组/effect key 在每帧 recording 时计算，但 VFX effect 的 simulation grouping 仍固定于 scene 初始化（V3.15 限制）。

## 18. V3.17 effect GPU instancing v2：instanced draw v1

### 目标

- 在 V3.15 共享 simulation 的基础上，把同一 asset 的多个兼容 scene instance 的 color/MV/shadow draw 合并为每个 output 一次 indirect draw；每个 instance 仍使用自己的 current/previous transform 和 cull 状态。
- 继续作为 opt-in：使用现有 `--vfx-instancing` / `PipelineSettings::supportVFXInstancing`，默认关闭；关闭或任一 instance 被 cull 时保持 V3.15 的逐 instance draw。

### 参考

- Unity `Shaders/VFXInstancing.hlsl:1-115`：fixed/variable-size instance index、`instancingCurrentCount/ActiveCount/BatchSize` 与 current → active → batch indirection；`VFXInitInstancing()` 在 compute/VS 中按 instance 取索引。
- Unity `Shaders/VFXOutputUpdate.template:147-152`：为每个 instance 构造 `elementToVFX`；`Shaders/ParticlePlanarPrimitives/PassVS.template:113-142` 用该矩阵生成 planar 输出；`Editor/NewCompiler/Passes/OutputPass.cs:300-312` 的 `SystemUsesInstancedRendering`/`instanceSplitDescs` 控制拆分。
- hdrp-bgfx V3.11 已有 `fill_indirect` 的 particle-chunk instancing；V3.17 复用同一 indirect 机制，把 `numInstances` 扩展为 `particleChunks * effectInstances`，并用 per-effect transform buffer 取代 Unity 的 per-instance 属性/索引 buffer。

### 实现

- `EffectImpl` 在 instance 数大于 1 时创建 `vfx.instance.transforms`（`StorageVertex`，每个 instance 32 floats：current mat4 + previous mat4），在 transform/cull/reset 时上传；N=1 不分配该 buffer。
- `fill_indirect` 增加 `u_vfxInstanceParams.x`（effect instance 数），写入 `indirect[1] = particleChunks * effectInstances`；`u_vfxInstanceParams` 由 runtime 在 simulation/output/MV/shadow 中统一设置。
- 生成 output VS 与 `vfx_motion_vectors.vs.sc`/`vfx_shadow.vs.sc` 将 `gl_InstanceID` 分解为 effect instance 与 particle chunk；effect instance 从 stage 15 raw buffer 读取 current/previous 矩阵列，按 CPU `Mat4` 的列布局计算 world position；N=1 或 fallback 时仍走 `u_vfx_world`/`u_vfx_prevWorld`。
- Runtime 仅在 supportsEffectInstancing、instanceCount>1、全部 instance 可见且 buffer 有效时 batch；每个 output 一次 `drawIndirect`。任一 instance culled 时回退逐 instance draw，shadow 继续绘制全部 instance。N=1 时在 stage 15 绑定 attributes buffer 作为 descriptor fallback，避免 Vulkan 未绑定 storage buffer。
- 不新增 pipeline setting/CLI；不修改 `.vfx.json` 语义。由于 output VS 源变化，checked-in `.vfxb` 全部重烘焙；external MV/shadow shader pack 同步重建。

### 验证

- runtime `hbg_vfx_runtime_tests` **30/30, 699 assertions**：新增两 instance shadow batch（1 draw、instance params=2、cull 后 2 draws）；原有多 instance color/MV 测试改为验证 1 次 batched draw、stage 15 binding 和 transform buffer 的 current/previous 值。
- compiler `hbg_vfx_compiler_tests` **30/30, 622 assertions**：检查 `fill_indirect` 的 `particleChunks * effectInstances`、output VS 的 instance chunk 映射和 `u_vfxInstanceParams`。
- `vfx_instancing` fixture 的右侧实例增加 rotation `[0,0,1.57]` 与 scale `[1.5,0.5,1.0]`；D3D11/Vulkan off/on color 截图 **0 diff**，`vfx_draws` 从 **4 → 2**；motion-vector 截图 **0 diff**。
- 既有 62 个 VFX CTest（goldens/references/shadows/MV/transparent/instancing）双后端通过；`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、`hbg_tests` 315/33,505；`hbg_vfx_asset_check` 全量 31 个资产通过；CTest **144**。
- `git diff --check` 通过。

### Known limits

- V3.17 只有全部 instance 可见时才 batch；任一 cull 会整 effect 回退逐 instance draw（shadow 也回退）。V3.18 已补 active instance indirection；仍没有 GPU cull compaction 或 batch indirection。
- 没有 Unity 的 instance split、property/texture split、prefix-sum variable-size batch；一个 effect 只维护一个 instance transform buffer。
- 仅支持 V3.15 已有的 local-space、output sort `none`/`youngest` 资产；`distance`/`cameraDepth`/`custom` 仍不分组。
- `u_vfxInstanceParams`/indirect args 在 simulation 时确定；renderer 在 simulation 前设置 cull，运行时暂停期间改变 instance cull 不会重新生成 args。

## 19. V3.18 effect GPU instancing v3：active instance indirection

### 目标

- 在 V3.17 的固定 size batch 上增加 active instance 列表：同一 effect 的多个 scene instance 只有部分被相机 frustum cull 时，color/MV 仍合并为每个 output 一次 indirect draw，只让可见 instance 产生顶点。
- shadow 不读取相机 cull 状态，继续把该 effect 的全部 instance 合并为一次 draw；N=1 或只有 1 个可见 instance 时保留旧的逐 instance draw。
- 不新增 CLI/pipeline setting，继续由 `--vfx-instancing` / `PipelineSettings::supportVFXInstancing` 控制；默认关闭。

### 参考

- Unity `Shaders/VFXInstancing.hlsl:37-54` 的 fixed-size `VFXGetFixedSizeInstanceIndex()`：由 draw/dispatch 内 index 反推 effect instance；`:69-80` 的 `VFXGetInstanceActiveIndex()` 通过 `instancingIndirectAndActiveIndirect[instancingActiveIndirectOffset + instanceCurrentIndex]` 把当前索引映射到 active 实例。
- Unity `Editor/Compiler/VFXCodeGenerator.cs:962-988` 为 compute/output 分别定义 `VFX_INSTANCING_FIXED_SIZE`/`VFX_INSTANCING_VARIABLE_SIZE` 与 active/batch indirection；`Editor/Data/VFXDataParticle.cs:1505` 分配 `VFXInstancesIndirectionBuffer`。
- Unity output VS 通过 `VFXConfig.template.hlsl` 的 `_InstanceActiveIndex`/`_InstanceIndex` per-instance 属性取得 active/batch 索引；hdrp-bgfx effect-level 版本把等价映射放在 stage 15 buffer 的 active 区域，并用 `u_vfxInstanceParams.yzw` 传 active count/base/flag。

### 实现

- `vfx.instance.transforms` 扩展为 `N + ceil(N/32)` 个 32-float slot：前 N 个保存 instance current/previous `Mat4`，尾部 slot 保存 active instance 索引列表；active base = `N * 32` words。
- `EffectImpl` 复用 V3.17 的 `u_vfxInstanceParams`：`x = totalInstances`（用于 `fill_indirect` 和 transform 分支）、`y = activeInstances`、`z = activeBase`、`w = activeIndirection`。partial cull 时 `fill_indirect` 仍写 `particleChunks * totalInstances`，shader 对 `instanceIndex >= activeCount` 的 slot 输出退化顶点，避免额外 args buffer。
- 生成 output VS 与 `vfx_motion_vectors.vs.sc` 在读取 attributes 前先做 active 映射：`instanceActive = instanceIndex < activeCount`，active 时 `effectInstance = rawLoadUint(s_vfxInstances, activeBase + instanceIndex)`；`linearParticle < renderCount && instanceActive` 才生成输出，transform 使用 `effectInstance * 32`。
- `EffectImpl::refreshDrawInstancing()` 每帧按 `InstanceRuntime::culled` 重建 active 列表；可见数 >1 时 `m_outputBatch = true`，可见数 < 总数时启用 active indirection。可见数 =1 仍走逐 instance fallback；可见数 =0 时 color/MV 不提交。
- `encodeShadow()` 使用 `x = totalInstances`、`w = 0`，始终对全部 instance 做一次 batched draw；`bindInstanceTransforms` 仍绑定同一个 stage 15 buffer。
- N=1 仍绑定 attributes buffer 到 stage 15 作为 descriptor fallback；buffer 只在 instance 数变化/销毁时重建，active 列表在 cull 变化后按 offset 增量上传。

### 验证

- runtime `hbg_vfx_runtime_tests` **31/31, 725 assertions**：新增 3 instance、cull 中间 instance 的 partial batch（1 draw，params total=3/active=2/base=96/flag=1，buffer active=[0,2]）；shadow cull 后仍为 1 draw。
- compiler `hbg_vfx_compiler_tests` **30/30, 626 assertions**：检查 output VS 的 active guard、stage 15 active load、`effectInstance * 32u`。
- 新增 `vfx_instancing_cull.gltf/.hdrp.json`：3 个 scene instance，第三个在视锥外；D3D11/Vulkan off/on color 截图 **0 diff**，off `vfx_draws=4`（两个可见 effect × color+MV）、on `vfx_draws=2`（一个 partial batch）；motion-vector 截图 **0 diff**。
- 全部 VFX 相关 CTest（goldens/references/shadows/MV/transparent/instancing，不含 perf/editor）**90/90** 通过；`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、`hbg_tests` 315/33,515；`hbg_vfx_asset_check` 31 个资产通过；CTest **146**。
- `git diff --check` 通过。

### Known limits

- active 列表由 CPU 每帧重建；V3.18 partial batch 仍按 total instance 数提交 draw，inactive slot 只做退化顶点；V3.19 已补 active-count indirect args，仍没有 GPU cull compaction。
- 没有 Unity 的 batch indirection（batch 小于 active 时的 active→batch 映射）、instance split、property/texture split、prefix-sum variable-size batch；shadow 直接读取全量 transform，不读 active 列表。
- 仅支持 V3.15 已有的 local-space、output sort `none`/`youngest` 资产；`distance`/`cameraDepth`/`custom` 仍不分组。
- transform buffer 额外增加 1 个 slot；cull 变化只增量上传 active 列表，暂停期间改变 cull 仍需下一次 encode 前刷新。

## 20. V3.19 effect GPU instancing v4：active-count indirect args

### 目标

- 在 V3.18 active instance list 的基础上，把 color/MV indirect args 的 `numInstances` 改为 active instance 数：partial cull 时不再提交 total slot，inactive slot 不再产生 vertex shader 调用。
- full batch（active=total）继续使用原 args，不额外 dispatch；只剩一个可见 instance 时也使用 active-count=1 的 args，修复 V3.17/V3.18 fallback 仍按 total chunks 提交导致的重复 instance 调用。
- shadow 继续使用 total args 和全量 transform；opt-in 仍为 `--vfx-instancing` / `PipelineSettings::supportVFXInstancing`，默认关闭；不新增 CLI/setting。

### 参考

- Unity `Shaders/VFXInstancing.hlsl:10-12` 的 `instancingCurrentCount`/`instancingActiveCount`/`instancingBatchSize`，以及 `:29-31` 的 active→batch / current→active indirection buffer 布局；`:81-92` 的 `VFXGetInstanceBatchIndex()` 在 batch 小于 active 时读取 active→batch。
- Unity `Editor/Compiler/VFXCodeGenerator.cs:984-988` 为非 output 任务定义 `VFX_INSTANCING_ACTIVE_INDIRECTION`/`VFX_INSTANCING_BATCH_INDIRECTION`；`Editor/Data/VFXDataParticle.cs:1505` 分配 `VFXInstancesIndirectionBuffer`。
- Unity 没有 instance split 时 batch 顺序就是 active 顺序；hdrp-bgfx 沿用该语义：V3.18 active list 同时是 batch→current 映射，因此只需要把 draw args 从 total 收紧到 active，不需要额外 active→batch buffer。

### 实现

- `EffectImpl::prepareActiveIndirect()` 按 system 和 sort list 懒创建 `BufferUsage::Indirect` 的 `.active.indirect` buffer；仅当 `instancing && 0 < active < total` 时使用，首次使用分配、销毁时释放。
- 复用现成 `fill_indirect` kernel：在 `ensureOutputSort()` 之后，以 `u_vfxInstanceParams.x = activeInstances` dispatch 一次到 active args buffer；base 和每个额外 sort list 各自缓存 `activeArgsSerial`，每帧/每次 cull 变化最多一次。
- `m_activeArgsRequired` 为 true 时，color/MV 的 `drawIndirect` 使用 active args；full batch 使用原 total args；shadow 始终使用原 total args。输出/MV shader 不变：partial cull 仍以 active list 做 batch→current 映射，visible=1 继续走 `u_vfx_world` fallback。
- V3.19 不修改 codegen、shader 或 `.vfxb`；base 的 total args 仍由 simulation 生成，extra sort list 的 total args 由 `ensureOutputSort()` 生成，供 shadow 使用。

### 验证

- runtime `hbg_vfx_runtime_tests` **31/31, 743 assertions**：partial cull 断言 active args buffer 被创建、fill dispatch 使用 active=2、draw 使用 active args 而非 total args；visible=1 fallback 断言 fill=1 且 draw 使用 active args；full batch 和 shadow 断言仍使用原 total args 且不 dispatch active fill。
- 新增 `vfx_instancing_cull_gap.gltf/.hdrp.json`：中间 instance 在视锥外，active list 为不相邻的 `[0,2]`；D3D11/Vulkan off/on color 与 motion-vector 截图 **0 diff**，off `vfx_draws=4`、on `vfx_draws=2`，验证 batch index 经 active list 映射到正确 current instance。
- 既有 `vfx_instancing_cull`（第三个 instance 在视锥外）继续通过；runtime 31/31（743）、compiler 30/30（626）、`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、`hbg_tests` 315/33,525；VFX 相关 CTest **90/90**、31 个资产、CTest **146**。
- `git diff --check` 通过。

### Known limits

- 仍没有 instance split；batch 不能小于 active，所以没有 Unity 的 active→batch indirection、property/texture split 或 prefix-sum variable-size batch。
- active list 仍由 CPU 每帧重建；active args 在 `ensureOutputSort()` 后按 list dispatch，首次 partial/visible=1 使用时懒分配；没有 GPU cull compaction。
- shadow 不读 active list，继续按 total instance 绘制；暂停期间 cull 变化仍依赖下一次 encode 前 refresh。

## 21. V3.20 tight billboard automatic bounds v2：camera-aware AABB

### 目标

- 改进 V3.13 的 2D 球半径：对 flat billboard 使用当前相机 right/up、粒子 angle/scale 和 size 计算 oriented extents，减少 automatic bounds 的 x/y/z 保守量。
- 保持 automatic bounds 的 GPU AABB readback/padding 和 host culling 接口不变；无新 CLI/pipeline setting。
- V3.20 仍是 camera-aware AABB，不提供 Unity axis/angle/pivot 的 OBB。

### 参考

- Unity `Shaders/VFXBoundsUtils.hlsl:73-85` 的 `GetTightBoundingExtents_VFX()`：用 element matrix 的列计算 `extents.x = max(abs(m00), abs(m01))` 等；`:52-56` 保留 2D bounding radius。
- hdrp-bgfx 的 billboard 没有 Unity axis/angle/pivot 属性，而是使用 `u_global_invView[0/1]` 的 camera right/up 加 `angle` 旋转；V3.20 把同一矩阵思路用于该 billboard，并把结果作为 AABB extents。

### 实现

- `makeBoundsSource()` 增加 `u_vfxBoundsParams`/`u_vfxBoundsRight`/`u_vfxBoundsUp`，并读取 `angle`（缺失时为 0）；`u_vfxBoundsParams.x > 0.5` 时：
  `billboardRight = cameraRight*cos(angle) + cameraUp*sin(angle)`，`billboardUp = -cameraRight*sin(angle) + cameraUp*cos(angle)`，`extents = 0.5*|size|*(|billboardRight|*|scale.x| + |billboardUp|*|scale.y|)`。
- 未启用 tight 或旧资产仍使用 V3.13 的 `radius = 0.5*|size|*length(scale.xy)` fallback。
- Runtime 在 automatic-bounds system 的 simulation uniforms 中上传 `{1,0,0,0}` 和 `FrameContext::cameraRight/cameraUp`；非 automatic system 不额外设置。bounds buffer、readback、padding、host frustum culling 不变。
- 只重烘焙含 automatic bounds 的 `auto_bounds.vfxb`；bounds shader 内嵌在资产中，不需要改 external shader pack 或其他 `.vfxb`。

### 验证

- runtime `hbg_vfx_runtime_tests` **31/31, 751 assertions**：automatic bounds simulation 断言 bounds params=1、camera right=(1,0,0)、up=(0,1,0)，并保留 readback/padding 断言。
- compiler `hbg_vfx_compiler_tests` **30/30, 631 assertions**：检查 bounds shader 包含 `u_vfxBoundsParams/Right/Up`、`abs(billboardRight) * abs(scale.x)` 和 angle load，同时保留 2D radius fallback 断言。
- 新增 `run_vfx_auto_bounds.cmake`：D3D11/Vulkan `--vfx-reference` 断言 automatic bounds 有效，并检查 tight y/z 为 `min(...,-0.6,-0.75)` / `max(...,0.6,0.75)`；两种后端 **0 差异**。
- 既有 90 个 VFX CTest、`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、`hbg_tests` 318/33,587；31 个资产 asset check；CTest **146**。`git diff --check` 通过。

### Known limits

- bounds 仍是 AABB；没有 Unity axis/angle/pivot 的 tight OBB，也没有 mesh/3D 元素 extents。
- tight extents 依赖当前 camera right/up，暂停期间相机变化不会重新 dispatch bounds；automatic bounds 仍通过异步 readback 回传。
- padding 仍是轴对齐 AABB 扩展，不是 OBB padding；多 instance 共享 local bounds，但 extents 与输出 shader 的 local billboard 轴一致。

## 22. V3.21 cull-aware visible instance bounds

### 目标

- V3.16 的 VFX 透明 effect-level 排序键在 instance 分组后使用 `Effect::bounds()`；该 bounds 是所有 instance 的并集，即使部分 instance 已被视锥剔除，仍会把不可见 instance 计入排序中心。
- V3.21 增加 cull-aware 的 `Effect::visibleBounds()`，透明排序只使用未剔除 instance 的 bounds；`bounds()` 的全量语义保持不变，避免影响外部 culling/诊断。
- 不改 simulation、输出 shader、资产、buffer、CLI 或 pipeline setting。

### 参考

- Unity `Shaders/VFXBoundsUtils.hlsl:150-168` 的 bounds dispatch 以 `instanceIndex * 6` 写每个 instance 的 min/max；`Editor/Data/VFXDataParticle.cs:928-938` 把 automatic bounds 作为系统级 GPU 路径。Unity 的 renderer 对每个 VFXRenderer 使用自己的 bounds 做可见性/排序，而不是把已剔除实例混入可见集合。
- hdrp-bgfx 在 V3.15/V3.18 把多个 scene instance 合并为一个 effect，`instanceBounds(i)` 已是 per-instance 世界 AABB；V3.21 只在 effect-level 排序时取未 cull 子集的并集。

### 实现

- `vfx/include/vfx/runtime.h` 增加 `Effect::visibleBounds() const`；`EffectImpl` 增加缓存的 `m_visibleBounds` 和 `refreshVisibleBounds()`。
- `refreshBounds()` 每次实例变换/automatic bounds 更新后重算 visible bounds；`setCulled()`/`setInstanceCulled()` 也立即重算。只合并 `!culled` 且 `instanceBounds(i).valid` 的实例；全部被剔除或没有有效 bounds 时返回 invalid。
- `bounds()` 继续返回全量并集；`instanceBounds(i)`、headless culling、automatic bounds readback 和 shadow 全量路径不变。
- `ForwardRenderer::addTransparentFramePasses()` 的 effect-level 透明排序中心从 `bounds()` 改为 `visibleBounds()`；若 visible bounds 无效则回退到 primary instance position（与旧逻辑一致）。默认 VFX pass 顺序不受影响。

### 验证

- runtime `hbg_vfx_runtime_tests` **31/31, 761 assertions**：两个 instance 初始 visible bounds 等于全量；cull 左 instance 后 visible bounds 只含右侧 `[2,4]`；两个都 cull 后无效；`setCulled(false)` 后恢复全量。
- 既有 90 个 VFX CTest、`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、`hbg_tests` 318/33,587；31 个资产 asset check；CTest **147**。`git diff --check` 通过。

### Known limits

- 仍是 effect-level 排序键，不是逐 instance/逐粒子与场景 transparent DrawList 的完整交错；V3.16 的近似中心仍保留。
- cull 状态在 `ensureVfx`/`updateVfx` 更新；暂停期间相机 cull 变化仍依赖下一次 `updateVfx`。
- visible bounds 只排除 CPU cull 标记的 instance；没有 GPU cull compaction，也不改变 shadow 的全量提交语义。

## 23. V3.22 fixed-size active instance split

### 目标

- V3.19 的 active args 仍把全部 active instance 放进一个 batch；当 `active` 超过单个 indirect draw 希望提交的数量时，没有 batch 切分。
- V3.22 增加 opt-in fixed-size active instance split：把 active list 按 `instanceBatchLimit` 切成连续 batch，每个 batch 使用自己的 `.active.indirect[.N]` args；color/MV 每个 batch 一次 `drawIndirect`。
- 仍是同一个 effect simulation、同一个 active list、同一个 transform buffer；不引入 instance/property/texture split，也不改变默认路径。

### 参考

- Unity `Shaders/VFXInstancing.hlsl:10-12` 暴露 `instancingCurrentCount`/`instancingActiveCount`/`instancingBatchSize`；`:29-31` 描述 active→batch 与 current→active indirection；`:81-92` 在 batch 小于 active 时用 active index 映射 batch。
- Unity `Editor/Compiler/VFXCodeGenerator.cs:984-988` 生成 active/batch indirection 开关，`Editor/Data/VFXDataParticle.cs:1505` 分配 indirection buffer。
- hdrp-bgfx 没有 instance split，batch 顺序就是 active 顺序；因此 fixed-size batch 可以用 `activeBase + batchOffset` 直接映射，不需要额外 active→batch buffer。

### 实现

- `vfx/include/vfx/runtime.h` 的 `EffectDesc` 增加 `instanceBatchLimit`（0 = 不切分）；renderer 从 `PipelineSettings::vfxInstanceBatchSize` 传入。新增 JSON key `vfxInstanceBatchSize` 与 CLI `--vfx-instance-batch=<n>`（0–65535，0 = 不切分）。
- `EffectImpl::refreshDrawInstancing()` 计算 `outputBatchSize = min(limit, active)` 与 `outputBatchCount = ceil(active / outputBatchSize)`；只有 `active > 1` 且 batch 数大于 1 时才 split。
- `BatchIndirectRuntime` 按 system/sort list 保存 `std::vector<BufferRuntime>`；`prepareActiveIndirect()` 为每个 batch 懒创建 `.active.indirect`/`.active.indirect.1...`，用现有 `fill_indirect` 以该 batch 的 local instance count 写 args。batch 数缩小时保留多余 buffer 以便复用，销毁 effect 时统一释放。
- color/MV 每个 batch 设置 `u_vfxInstanceParams = {total, localInstances, activeBase + batchOffset, 1}` 并提交该 batch 的 args；shader 源码不变，仍通过 active list 把 batch 内 index 映射到 current instance。
- V3.22 本身只切分 color/MV；shadow 继续使用 total instance count 和 simulation args，不读取 active list。V3.23 再把同一 limit 应用到 shadow 的 total-sized 连续 batch。

### 验证

- runtime `hbg_vfx_runtime_tests` **32/32, 792 assertions**：3 个 active instance、limit=2 时产生 2 个 draw，args 分别为 `{3,2,96,1}` 与 `{3,1,98,1}`；cull 中间 instance 后 active=`[0,2]`，仍使用同一 active list 的 batch offset 映射，draw 数降为 1。
- `vfx_instancing.gltf` 的 split smoke：`--vfx-instancing --vfx-instance-batch=1` 仍为 1 个 simulation、2 个 scene instance，color/MV 均 4 个 draw（2 output × 2 batch），与未分组截图 **0 diff**；D3D11/Vulkan 均通过。
- 既有 `hbg_vfx_instancing_cull`/`_cull_gap` D3D11/Vulkan 通过；两者再加 `--vfx-instance-batch=1` 后仍为 0 diff，`vfx_draws=4`，验证 partial active list（包括 `[0,2]`）经 batch offset 正确映射。`hbg_vfx_tests` 98/98（38,286）、adapter 8/8（328）、compiler 30/30（507）、`hbg_tests` 323/33,687；CTest **148**。未改资产/shader，未重跑 asset check 与完整 90 项 VFX suite。

### Known limits

- 只做 fixed-size batch；没有 Unity 的 instance split、property/texture split、prefix-sum variable-size batch 或独立 active→batch indirection buffer。
- batch 是 active order 的连续切片；不支持 batch 内不同 property/texture 分组，也不支持 GPU cull compaction。
- V3.22 的 shadow 仍是一个 total-sized batch；V3.23 已让 shadow 也按 total instance 连续切分，但仍不读取 active list。
- 多余 batch args buffer 在 batch 数缩小时保留，销毁 effect 时释放；没有运行期 shrink 回收。
- `instanceBatchLimit` 在 effect 创建时读取，没有运行期 setter；修改后需重建 effect。
- 默认 `vfxInstanceBatchSize=0`，行为与 V3.21 完全一致。

## 24. V3.23 fixed-size shadow instance split

### 目标

- V3.22 只把 color/MV 的 active instance 按 `instanceBatchLimit` 切分，shadow 仍是单个 total-sized indirect draw；当 instance 数超过单 draw 上限时，shadow 仍会超限。
- V3.23 把同一 limit 应用到 shadow：按 total instance 的连续 batch 切分 shadow draw，每个 batch 使用自己的 `.shadow.indirect[.N]` args。
- shadow 继续绘制全部 instance，不读取 camera active list，也不把 camera cull 带到 shadow；默认 `0` 保持 V3.22 的单 batch 行为。

### 参考

- Unity `Shaders/VFXInstancing.hlsl:5,10-17` 同时暴露 current/active/batch count 与 `instancingCurrentOffset`；`:37-54` 的 fixed-size current index 在 draw/dispatch 内叠加该 offset；`:94-103` 的 `VFXInitInstancing()` 让输出/阴影使用同一组 offset 语义。
- Unity `Editor/Compiler/VFXCodeGenerator.cs:185-186` 在任务入口生成 `instanceIndex/instanceActiveIndex/instanceCurrentIndex`；shadow 作为另一种 instancing draw，只需要 current instance offset，不需要 active indirection。
- hdrp-bgfx 的 shadow 不读取 active list，所以这里的 batch 是 total instance 的连续切片；`u_vfxInstanceParams.z` 直接携带 shadow batch 的 total instance base。

### 实现

- `EffectImpl::refreshDrawInstancing()` 增加 `shadowBatchSize/Count`、`shadowArgsRequired` 与 `shadowArgsSerial`：从 `m_instances.size()` 和 `instanceBatchLimit` 计算，只有 instancing 且 total > limit 时切分；结果与 color/MV 的 active batch 独立（例如 total=3、active=2、limit=2 时 color/MV 1 draw、shadow 2 draw）。
- `SystemRuntime` 增加 `shadowIndirect`。`prepareShadowIndirect()` 按 system 懒创建 `vfx.<system>.shadow.indirect`/`.shadow.indirect.1...`，用现有 `fill_indirect` 为每个 total batch 写入 `particleChunks * localInstances`；数组复用并随 effect 销毁释放。
- `encodeShadow()` 按 `shadowBatchCount` 循环，设置 `u_vfxInstanceParams = {totalInstances, localInstances, batchOffset, 0}`，每个 batch 使用自己的 args。非切分路径的 `batchOffset` 固定为 0。
- `vfx/shaders/vfx_shadow.vs.sc` 在拆分 `gl_InstanceID` 后加上 `u_vfxInstanceParams.z`，把 batch-local instance 映射回全局 transform slot；color/MV shader 不变。
- 既有单实例 shadow smoke 的 `u_vfxInstanceParams.z` 由原先未使用的 `activeBase` 改为 0；这只影响内部 uniform，不改变绘制。

### 验证

- runtime `hbg_vfx_runtime_tests` **33/33, 814 assertions**：3 个 instance、limit=2 时 shadow 产生 2 个 draw，`u_vfxInstanceParams` 分别为 `{3,2,0,0}` 与 `{3,1,2,0}`，args buffer 为 `vfx.CurveVisual.shadow.indirect[.1]`；cull 第 0 个 instance 后 shadow 仍为 2 个 total batch；销毁后 `liveBufferCount()==0`。既有 shadow test 的 `z` 断言更新为 0。
- `vfx_shadow.vs.sc` 已用 shaderc 重新编译 DXBC/SPIR-V 变体。手工 D3D11/Vulkan split smoke：3 个 `curve_shadow` local instance、limit=1、directional shadow atlas，未切分 `vfx_shadow_draws=4`、切分 `vfx_shadow_draws=12`（4 个 shadow pass × 3 batch），两后端截图 **0 diff**。共享 `shaders/manifest.json` 当时被并行的 color-pyramid 工作修改且缺少 `colorPyramid*` 条目；smoke 时临时补齐并恢复，未把 fixture/CTest 加入仓库。manifest 已恢复，V3.24 已补跑 106 项 VFX suite。
- 当时通过：`hbg_vfx_tests` 98/98（38,286）、`hbg_vfx_compiler_tests` 30/30（507）、adapter 8/8（328）、runtime 33/33（814）；CTest 列表 148。V3.24 时 manifest/config 均已恢复。

### Known limits

- shadow batch 只按 total instance 连续切分；不读取 active list，也不把 camera cull 应用到 shadow。
- 没有 GPU cull compaction、shadow active list、property/texture split、prefix-sum variable-size batch 或独立 active→batch indirection。
- `instanceBatchLimit` 仍是 creation-time 配置；多余 shadow args buffer 在 batch 数缩小时保留，销毁 effect 时释放。
- shadow split 仍只有手工 smoke，尚未单独加入 D3D11/Vulkan CTest；V3.24 的 per-batch transparent CTest 已覆盖 manifest 正常路径。

## 25. V3.24 VFX transparent per-batch insertion

### 目标

- V3.16 只把整个 VFX effect 作为一个透明 key 插入场景 `DrawList`；一个 grouped effect 内所有 instance 必须同进同出，无法让场景透明物体穿过这些 instance。
- V3.24 在启用 `--vfx-transparent-sort` 且 `--vfx-instance-batch=<n>` 时，按 V3.22 的 active instance batch 再切一层透明提交：每个 batch 用自己的 active args、union bounds 中心和独立的 VFX output pass 插入透明段。
- `n=1` 时退化为逐 instance 插入；`n>1` 时为逐 batch 插入；`0`（默认）仍保持 V3.16 的 effect-level 单 key，不改变既有像素。

### 参考

- HDRP `Runtime/RenderPipeline/HDRenderPipeline.cs:3460-3475` 用 `SortingCriteria.CommonTransparent | RendererPriority` 创建 transparent RendererList；`HDRenderPipeline.RenderGraph.cs:1872,1902` 把多类 transparent forward 放入同一排序列表。V3.24 沿用同一 transparent key 语义，只把提交粒度从 effect 改为 active batch。
- Unity `Shaders/VFXInstancing.hlsl:5,10-17,37-54` 的 `instancingCurrentOffset` 与 fixed-size current index；V3.22/V3.23 已把该 batch offset 用于 color/MV/shadow。本步复用相同 active batch，不新增 instance 映射。
- hdrp-bgfx 的 batch 是 active list 连续切片，因此每个 batch 的 bounds 是这批未 cull instance bounds 的并集；batch 内部粒子排序仍由 V1.2/V3.10 的 KVP/indirection 决定。

### 实现

- `vfx/include/vfx/runtime.h` 的 `Effect` 增加只读 `isInstanceCulled(index)`、`outputBatchCount()`、`outputBatchSize()`；`Runtime` 增加 `encodeOutputEffectBatch(sink, frame, effect, batchIndex)`。
- `EffectImpl::encodeOutput()` 抽出共享的 `encodeOutputDraws()`；完整输出和单 batch 输出都走同一套 output/soft/lit/buffer 绑定逻辑。`encodeOutputBatch()` 使用当前 `m_outputBatchCount`、`m_outputBatchSize` 和 `prepareActiveIndirect()` 已生成的 `.active.indirect[.N]`，设置 `u_vfxInstanceParams = {total, localInstances, activeBase + batchOffset, activeIndirection}` 后只提交指定 batch。
- `ForwardRenderer::addTransparentFramePasses()`：当 effect 的 `outputBatchCount() > 1` 时，按 active 顺序按 `outputBatchSize()` 分组，求每个 batch 的 instance bounds 并集中心作为 `DrawSortKey`；每个 batch 生成一个 `VFX Output N Batch B` pass。逐 instance cull 的 instance 不进入 active 分组。`effectCount` 与 `submissions` 分开统计，日志为 `effects=<unique> batches=<提交数> segments=<段数>`。
- 只在 `--vfx-transparent-sort` + 非零 `--vfx-instance-batch` 组合下生效；不新增 CLI/JSON/asset 字段，不修改 shader、simulation、motion-vector 或 shadow 提交。MV 仍在所有透明段之后一次性绘制，shadow 仍绘制全部 instance。

### 验证

- runtime **34/34, 843 assertions**：3 个 instance、limit=1 时 `outputBatchCount()==3`、`outputBatchSize()==1`；单独编码 batch 1 得到 1 个 draw、args `vfx.CurveVisual.active.indirect.1`、params `{3,1,97,1}`；cull 中间 instance 后 batchCount=2，batch 1 仍映射 active list `[0,2]` 的第二个 entry（activeBase=97），销毁后 `liveBufferCount()==0`。
- 新增 `hbg_vfx_transparent_batch_sort_d3d11/vulkan`：3 个 grouped `curve_unlit` instance（z=0.5/1.0/2.0）与 z=1.4 透明 panel；effect-level `effects=1 batches=1`、`vfx_draws=2`，per-batch `effects=1 batches=3`、`vfx_draws=6`；两种顺序 D3D11/Vulkan 均相差 **4,680 px / max delta 81**，per-batch golden 双后端通过，跨后端最大 1 px/delta1。
- 回归：既有 `hbg_vfx_transparent_sort` 双后端、`hbg_vfx_instancing`/`_cull` 双后端、`hbg_vfx_shadows` 双后端通过；VFX CTest 106/106（首次运行的两个非代码失败已重跑通过）；`hbg_vfx_tests` 98/98（38,286）、compiler 30/30（507）、adapter 8/8（328）、`hbg_tests` 325/33,719；CTest **150**。

### Known limits

- 只在 batch 粒度插入，batch 内 instance 仍不可被场景透明物体分开；`n=1` 才能逐 instance interleave。没有逐粒子插入。
- 每个 batch 仍绘制该 batch 的所有 outputs；batch 内粒子顺序不变。透明 key 使用 batch union bounds 中心，不是逐粒子深度。
- pre-refraction、transparent backface、GPU-driven transparent 路径仍走 V3.16 的 effect-level/旧顺序；没有 per-batch 的 refraction/backface 合并。
- 拆 batch 会增加 pass/draw 数和 CPU 提交；默认 `--vfx-instance-batch=0` 不启用。shadow 和 motion vectors 不随透明 batch 拆分。

## 26. 后续候选

- effect GPU instancing 后续：instance split、property/texture split、prefix-sum variable-size batch、独立 active→batch indirection 与 GPU cull compaction；V3.22 已补 color/MV fixed-size batch split，V3.23 已补 shadow total batch split。
- VFX 透明输出与场景 transparent DrawList 的进一步合并：V3.16 已完成 effect-level key，V3.24 已完成 active batch key；逐粒子插入、pre-refraction/backface 合并仍待实现。
- 带 UNITY axis/angle/pivot 的 tight OBB bounds；V3.20 目前只覆盖 camera-aware billboard AABB。
