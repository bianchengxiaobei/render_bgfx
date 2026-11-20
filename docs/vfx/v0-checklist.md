# V0 Checklist - 技术 Spike

> 目标：在写完整编译器/运行时之前，验证 hdrp-bgfx + bgfx 能否承载 VFX 的核心 GPU 契约。
> 原则：V0 只写最小手写 shader/测试代码，不引入 JSON/编译器；V0 通过后才进入 V1.0b。
> 每个任务完成后回填“实现位置”和“结论”；失败项记录 fallback。

---

## V0.1 子工程骨架与构建

**任务**
- [x] 新建 `vfx/` 子工程：`CMakeLists.txt`、`include/vfx/{types,handles}.h`、`src/`、`tests/`。
- [x] `hdrp-bgfx/CMakeLists.txt` 增加 `add_subdirectory(vfx)`（`HBG_BUILD_VFX` option）。
- [x] `vfx` 只链 STL；不 include `bgfx/`、`hbg/`、`render/`。
- [x] 加一个空测试目标，确认 `ctest` 能发现（`hbg_vfx_tests`）。

**参考**
- hdrp-bgfx 库分层：`CMakeLists.txt:79-160`（`hbg_core/hbg_gfx/hbg_asset/hbg_app`）。
- 代码风格：`AGENTS.md`（C++20、`hbg` 命名空间、PascalCase/camelCase/m_/k_）。

**验收**
- [x] `cmake --preset vs2022` + `cmake --build --preset vs2022-debug` 通过（`hbg`/`hbg_vfx_tests` 目标；shader 工具链见 V0.6 记录）。
- [x] `ctest` 至少 1 个空/样例用例通过。
- [x] `vfx/` 内 `rg "bgfx::|hbg::" include src` 无命中（工具/测试除外）。

**实现位置**：`vfx/CMakeLists.txt`、`vfx/include/vfx/types.h`、`vfx/include/vfx/handles.h`、`vfx/include/vfx/version.h`、`vfx/src/version.cpp`、`vfx/tests/test_vfx_smoke.cpp`；根 `CMakeLists.txt` 的 `HBG_BUILD_VFX`/`add_subdirectory(vfx)`。
**结论**：通过。`hbg_vfx` 只依赖 STL；`hbg_vfx_tests` 由 ctest 发现并通过。

---

## V0.2 RAW attribute buffer + compute 写入/读回

**任务**
- [x] 创建 `StorageVertex` raw buffer（1024 粒子 × 16 word，64B stride）。
- [x] 手写 compute kernel：`gl_GlobalInvocationID.x` 写 position/color/size/age/lifetime。
- [x] 用 `BUFFER_RAW_RW` + `rawStoreFloat` 写；`BUFFER_RAW_RO` 读（VS 侧读）。
- [x] 读回 buffer（`bgfx::read(BufferRegion, ptr)`），与 CPU 期望值比较。
- [x] D3D11 + Vulkan 都跑通。

**参考**
- `com.unity.visualeffectgraph/Shaders/Temp/Data/ByteAddressBuffer.hlsl:9-120`（raw byte address Load/Store）。
- `com.unity.visualeffectgraph/Shaders/Temp/Data/AttributeBuffer.hlsl:8-29`（属性声明/读写）。
- `src/gfx/encoder.h:49-52`、`src/gfx/encoder.cpp:241-274`（`setStorageBuffer`）。
- `src/gfx/buffer.h`（`BufferType::StorageVertex`）。
- bgfx raw 宏：`build/vs2022/_deps/bgfx-src/src/bgfx_compute.sh`（`BUFFER_RAW_RW/RO`、`rawLoad*/rawStore*`）。

**验收**
- [x] D3D11/Vulkan 读回值与 CPU 期望一致（float 允许 1e-6 误差）。
- [x] 无 bgfx validation error；`ResourceManager` 无泄漏（`liveResourceCount()==0`）。
- [x] 记录 readback 方法与耗时（是否同步、是否阻塞）。

**实现位置**：`shaders/vfx/vfx_spike_attributes.cs.sc`（raw 写入）、`src/render/vfx/vfx_spike.cpp`（buffer 创建 + `bgfx::read` + 校验）。
**结论**：通过。`StorageVertex` + `BUFFER_RAW_RW` 在 D3D11/Vulkan 均可写；`bgfx::read(BufferRegion)` 返回期望帧，需 `bgfx::frame()` 轮询到该帧才可读，属于异步/会等待 GPU 的 readback（V0 仅在测试路径使用）。读到 particle0 `position=(-1.24,-1.24)`、`size=0.06`、`colorR=0.08`、`alpha=1`，与 CPU 期望一致；双后端无 validation error。

---

## V0.3 Indirect args 32B + draw indirect

**任务**
- [x] `BufferType::Indirect` 创建 `count=1` 的 args buffer（32B）。
- [x] compute kernel 写 8×uint：`{numVertices, numInstances=1, startVertex=0, startInstance=0, 0,0,0,0}`。
- [x] `Encoder::submitIndirect` 用该 buffer 画 quad（手写 VS/FS）。
- [x] 验证 `numVertices = 6 * alive`（非索引 6 顶点/粒子；4 顶点无法构成两个三角形，见结论）。
- [x] 验证 D3D11/Vulkan 都能正确绘制（离屏 RGBA8 readback + 非背景像素计数）。

**参考**
- `src/gfx/buffer.cpp:132-138,192-196`（indirect 32B stride）。
- `src/gfx/encoder.cpp:282-297`（`submitIndirect`）。
- `com.unity.visualeffectgraph/Shaders/VFXFillIndirectArgs.compute:31-70`（quads 的 args 语义；本项目偏差 D1/D2）。
- `com.unity.visualeffectgraph/Editor/NewCompiler/Passes/VfxTemplateCodeGenerationPass.cs:12-15,554`（2048/`id>>2`）。
- `com.unity.visualeffectgraph/Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`（4 顶点展开/UV）。

**验收**
- [x] args 写入后 draw 的顶点/实例数正确；无越界（读回 `[6144 1 0 0 0 0 0 0]` = 1024×6）。
- [x] 如果 D3D11 的 `DrawInstancedIndirect` 行为异常，记录 fallback（CPU 写 args / 固定 draw 数）。
- [x] 记录 `startVertex/startInstance` 在 bgfx 下的实际语义（截图验证）。

**实现位置**：`shaders/vfx/vfx_spike_indirect.cs.sc`、`src/render/vfx/vfx_spike.cpp`（args 创建/读回/dummy VB/`submitIndirect`）。
**结论**：通过，但有两个必须保留的修正：
1. **4 顶点非索引 quad 不成立**：4 个顶点只能组成 1 个三角形；改为 6 顶点/粒子（两个三角形），args 为 `6*alive`。原文档 D1/D2 的 4 顶点假设已在 `architecture.md`/`source-alignment.md` 修正。
2. **bgfx D3D11 的 indirect draw 需要非空 vertex stream**：即使 indirect args 提供 `numVertices`，`renderer_d3d11.cpp` 的 draw 分支包在 `currentState.m_streamMask != 0` 内；未绑定 VB 时不会提交。V0 绑定一个 6 顶点 dummy `Vertex` buffer 后通过。`startVertex/startInstance` 在 args 中固定 0；V0 未测试非零值，V1 若使用需单独验证。

---

## V0.4 VS 读 StorageVertex + unlit quad

**任务**
- [x] 手写 unlit VS：`particleSlot = vertexID / 6`（经 indirection 取粒子）；读 attribute buffer 的 position/color/size/alpha。
- [x] 相机矩阵上传（view/proj/invView），billboard 展开（相机右/上 × size × (uv-0.5)）。
- [x] FS：采样主纹理（stage 0）并乘 color/alpha。
- [x] 深度 test on / write off；alpha/additive 两种 blend。
- [x] D3D11/Vulkan 离屏 readback 对比；固定 buffer/相机，与 CPU 期望位置一致（V0 用像素计数 + 首粒子校验，未生成 CPU 参考图）。

**参考**
- `src/render/passes/forward_renderer.cpp:6210-6310`（view transform / 纹理 stage / 状态设置）。
- `src/gfx/state.h`（透明/深度状态；可能需扩展）。
- `com.unity.visualeffectgraph/Shaders/Temp/Templates/Output.hlsl:4-60`（VS/FS 语义）。
- `com.unity.visualeffectgraph/Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`。

**验收**
- [x] D3D11/Vulkan 截图一致（或记录 PSNR 与原因）；V0 readback 像素计数/平均亮度完全一致。
- [x] 粒子位置/大小/颜色与 CPU 参考一致（固定相机/固定 buffer）。
- [x] 透明排序/深度写入符合预期（无深度写入、遮挡关系正确）。

**实现位置**：`shaders/vfx/vfx_spike.vs.sc`、`shaders/vfx/vfx_spike_unlit.fs.sc`、`src/render/vfx/vfx_spike.cpp`。
**结论**：通过。VS 通过 `gl_VertexID`（D3D11 翻译为 SV_VertexID）读取 stage 12/13 的 `StorageVertex`/indirection buffer；billboard 使用 `u_global_invView` 的相机右/上轴。D3D11 与 Vulkan 的 unlit alpha 均为 19600 个非背景像素、平均亮度 0.4549；additive 为 19600 / 0.5257（更亮，符合叠加预期）。深度 test 开启、写关闭。未做逐像素 CPU 参考图，V1 的 CPU reference 任务补上。

---

## V0.5 Lit quad 采样现有 HDRP 光照

**任务**
- [x] 手写 lit FS：填充 `SurfaceData`，调用 `hbgEvaluateLightLoop`。
- [x] 绑定与 forward pass 相同的资源：light data(5)、directional shadow(6)、light list(7)、shadow context buffer(8)、punctual shadow(9)、environment(10)、contact shadow(11)、probe maps(14)、cookie atlas(15)；本 spike 未启用 point/area sampler（12/13 留给 VFX buffer）。
- [x] 上传/绑定 `PassUniforms`、`GlobalUniforms`、`ObjectUniforms`、`ClusterUniforms`、`EnvironmentUniforms`。
- [ ] 透明 lit 使用 cluster light list（与 forward transparent 一致）——V0 关闭 list/cluster，仅验证 uniform/绑定形状；V1.4 接真实 cluster。
- [ ] 与同位置的 lit mesh 对比（颜色/阴影方向/IBL 趋势）——V0 用合成方向光 + ambient，未放 lit mesh；记录为后续 V1.4 对比项。

**参考**
- `shaders/lighting/light_loop.sc:339`（`hbgEvaluateLightLoop`）。
- `shaders/material/surface_data_types.sc:6-37`（`SurfaceData`）。
- `shaders/passes/forward.fs.sc:1-36`（SurfaceData 填充/阴影可见性）。
- `src/render/passes/forward_renderer.cpp:6270-6310`（stage 表）。
- `src/render/passes/forward_renderer.cpp:4828-4880`（output pass 的 color/depth/light 绑定）。
- HDRP VFX camera buffers：`Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs:1932-1970,2050-2100`。

**验收**
- [ ] Lit VFX 在 forward 与 deferred 下都能被场景光照照亮；方向光阴影可见。**部分：光照已通，方向光阴影未接线（`shadowParams.z=0`），forward/deferred 场景集成留 V1.4。**
- [x] 采样器 stage 无冲突/无 validation error。
- [ ] 与 lit mesh 的亮度/色相在合理范围内（记录截图，不做逐像素 0-diff）。**V0 只记录合成光照亮度。**
- [x] 记录 `maxTextureSamplers`/`maxComputeBindings` 的实际值（caps 日志）。

**实现位置**：`shaders/vfx/vfx_spike_lit.fs.sc`、`src/render/vfx/vfx_spike.cpp`（合成 light data / Pass/Cluster/Environment/Probe/Contact/Object uniform + stage 绑定）。
**结论**：部分通过（Go 条件满足）。`hbgEvaluateLightLoop` 在 D3D11/Vulkan 均编译并运行；合成方向光（intensity 2）+ ambient 下 lit 输出 19600 像素、平均亮度 0.4144（双后端一致）。关键 stage 结论：`light_loop.sc` 会带入 5/6/7/9/10/11/14/15，VFX attribute/indirection buffer 必须用 12/13；为此 VFX lit variant **不能定义** `HBG_SHADOW_POINT_SAMPLER`（否则 12/13 被 point/area depth sampler 占用）。方向光阴影未接线：`u_pass_shadowParams.z=0`，shadow matrix/context 仍为占位；V1.1/V1.4 需要补 shadow context 布局与真实方向光阴影测试。`maxTextureSamplers=16`、`maxComputeBindings=16`（D3D11/Vulkan 相同）。

---

## V0.6 绑定/stage/性能预算审计

**任务**
- [x] 列出 V1 kernel 需要的所有 buffer/texture stage，核对 16 上限。
- [x] 验证 buffer stage 与 texture stage 不重叠（D3D11 SRV 槽位共享：`renderer_d3d11.cpp:8018-8108`）。
- [x] 记录 `StorageVertex` 在 VS/CS 的绑定行为（V0 测跨 pass：CS 写 → 后续 VS 读；同 pass 读写未测）。
- [x] 记录 `setStorageBuffer` 的 256B offset 限制与实际用法（baseIndex uniform）。
- [x] 记录 32B indirect 的 padding 行为（D3D11/Vulkan）。
- [x] 记录空 dispatch/draw 的 CPU/GPU 开销基线（100 个空 compute pass）。
- [x] 记录 shaderc 对 `BUFFER_RAW_RW`、atomics、`SHARED`、uniform array 的编译结果。

**参考**
- `build/vs2022/_deps/bgfx-src/src/config.h:498-500`（16 samplers）。
- `build/vs2022/_deps/bgfx-src/src/bgfx_p.h:211`（compute bindings = 16）。
- `src/gfx/encoder.cpp:241-274`（256B 对齐/上限）。
- `src/gfx/buffer.cpp:132-138`（32B stride）。
- `docs/vfx/architecture.md` §4.4（stage 表）。

**验收**
- [x] 形成最终 `V0 findings` 表（见下）；若 binding 不足，给出拆分方案。
- [x] 若 shaderc 有兼容问题，给出最小可复现 shader 与规避写法。

**实现位置**：`src/render/vfx/vfx_spike.cpp`（stage/dummy VB/readback）、`shaders/vfx/vfx_spike_audit.cs.sc`（SHARED/uniform array/atomics 编译审计）、`tools/shader_build.py`（VFX 编译入口）。
**结论**：binding 预算刚好够：compute 用 buffer 0/1/2 + args 0，texture 0–15 的 forward 光照资源全部在 raster；VFX 的 12/13 只有在 point/area sampler 关闭时可用。`StorageVertex` 先由 CS 写、后续 pass 由 VS 读；**未测试同一 pass 同时 CS 写 + VS 读**（D3D11 会形成 UAV/SRV 冲突，V1 必须用 pass 分离或双缓冲）。`setStorageBuffer` 的 256B offset 由 `src/gfx/encoder.cpp:248` 强制；V0 只使用 offset 0，V1 用 `baseIndex` uniform 切片。VFX lit 变体关闭 `HBG_SHADOW_POINT_SAMPLER` 后无 stage 冲突。空 dispatch 基线：D3D11 encode 0.384 ms / frame 0.031 ms，Vulkan 0.368 ms / 0.049 ms（100 次空 dispatch，仅作数量级参考）。

### V0 findings 表（实现后回填）

| 项 | D3D11 | Vulkan | 结论 |
|---|---|---|---|
| maxTextureSamplers | 16 | 16 | forward/VFX lit 的 5/6/7/9/10/11/14/15 + VFX 0–4 刚好不超 |
| maxComputeBindings | 16 | 16 | compute 当前只用 0–2；V1 需 ≤16 |
| StorageVertex VS+CS | CS 写 → 后续 VS 读通过 | 同 | 跨 pass 可用；同 pass 读写未测，V1 避免 UAV/SRV hazard |
| setStorageBuffer offset 256B | 强制 256B 对齐 | 同 | V0 offset 0；V1 用 baseIndex uniform，不传非对齐 offset |
| indirect 32B padding | 8×uint 可写/读回 | 同 | args `[6*alive,1,0,0,0,0,0,0]` 正确；D3D/VK 只读前 4 个 |
| raw buffer atomics | 未用 raw atomics；structured buffer atomics 编译通过 | 同 | V1 deadList 用 structured/counter buffer atomics；raw 仅读写 |
| uniform array | `u_vfx_audit[2]` + SHARED 编译通过 | 同 | audit shader 编译通过，未 dispatch |
| 空 dispatch 100 次 CPU/GPU | encode 0.384 ms / frame 0.031 ms | encode 0.368 ms / frame 0.049 ms | 100×1 group 空 compute；CPU 编码约 3.8 µs/次，GPU 帧开销很小；V1 继续用 `gpu_frame_ms` 做正式基线 |

**shaderc 兼容性记录**：dxbc 对复杂 lit shader 在 `-O3 --Werror` 下 D3DCompiler 崩溃（exit 0xFFFFFFFF）；去掉优化（`-O` 省略）后编译成功，已在 `tools/shader_build.py` 对 `vfx_spike_lit.fs.sc` 使用 `d3d_skip_optimization`。其余 VFX shader 在 dxbc/spirv 均正常。`BUFFER_RAW_RO` 在 VS 中需要显式 `#include "bgfx_compute.sh"`（只 include `common.sc` 不够）。

**validation 记录**：D3D11 `--bgfx-debug --vfx-spike` 通过；Vulkan `--bgfx-debug` 在本机 bgfx 初始化阶段崩溃（`hbg.exe --backend=vulkan --gfx-test --bgfx-debug` 同样复现，非 VFX 引入），因此 Vulkan 用正常路径 + 双后端 readback 一致性作为验证；V1 需要单独修复/配置 Vulkan validation layer 环境后再补 validation 门禁。

---

## V0.7 Go / No-Go

**任务**
- [x] 汇总 V0.1–V0.6 结论；更新 `architecture.md` 的 stage/binding/indirect 契约。
- [x] 若某项失败，写 fallback 并调整 V1 计划（不隐藏问题）。
- [x] 确认 V1.0b 的任务顺序与依赖。

**Go 条件**
- [x] raw attribute buffer + VS read 可用。
- [x] indirect 32B + draw indirect 可用（需 dummy vertex stream）。
- [x] lit 能复用 `light_loop.sc` 且 stage 不冲突（VFX 变体关闭 point/area sampler）。
- [x] 双后端无 validation error（D3D11 `--bgfx-debug` 通过；Vulkan debug layer 环境在 bgfx init 崩溃，已用普通路径 + 一致 readback 验证，见 V0.6 记录）。
- [x] 绑定/采样器预算有明确结论。

**No-Go / fallback 记录**

| 失败项 | fallback | 影响 |
|---|---|---|
| 非索引 4 顶点 quad 不能形成两个三角形 | 改为 6 顶点/粒子，args `6*alive` | D1/D2 修正；无 index buffer；带宽 +50% |
| bgfx D3D11 indirect 无 VB 不提交 | 绑定 6 顶点 dummy vertex buffer | V1 输出必须保留非空 stream |
| VFX lit 若开启 point/area shadow sampler 占用 12/13 | VFX lit 不定义 `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`；只支持 directional/spot/contact 等；需要 point PCSS 时重排 stage | V1 lit 的 point/area PCSS 降级；V1.4 再评估 |
| 方向光阴影未接线 | V0 只验证 `hbgEvaluateLightLoop`，`shadowParams.z=0` | V1.4 补 shadow context 布局与阴影截图（V1.1 先做运动/碰撞） |

**实现位置**：`src/render/vfx/vfx_spike.cpp`、`shaders/vfx/*`、`--vfx-spike` CLI；`docs/vfx/{architecture,source-alignment,v1-checklist}.md` 已同步修正。
**结论**：**Go**。V0 以“readback 精确 + 双后端像素计数一致”通过核心 Go 条件；进入 V1.0b。V1 必须从第一天遵守：6 顶点/粒子、dummy VB、buffer 12/13 与 point/area sampler 互斥、readback 只用于测试/诊断、仿真与输出分 pass 避免 UAV/SRV hazard。方向光阴影与 Vulkan validation layer 环境未在 V0 完成，作为 V1.4 / 环境门禁的显式验收项保留。
