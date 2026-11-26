# hdrp-bgfx

基于 **bgfx** 的 Windows 实时渲染器，用 C++20 复刻 Unity HDRP 的核心渲染概念和帧流程。项目以 glTF 场景为输入，当前重点验证 **D3D11 / Vulkan**、**Forward / Deferred**、GPU light list、阴影、后处理和体积雾。

> 这是一个学习/验证型渲染器，不是 Unity HDRP 的运行时替代品。HDRP 源码用作行为、数据布局和采样语义的参考。

## 功能

- **渲染路径**
  - Forward-only、Deferred-only、Forward + Deferred mixed
  - RenderGraph：transient texture/buffer、pass culling、view id、FBO 复用
  - HDRP 风格 FrameSettings / PipelineSettings JSON
  - Forward depth prepass、GBuffer、stencil 标记、Deferred Lighting
  - Depth pyramid：`R32F` min-only compute（mip0 copy + 逐 mip 2×2 min，11 mips），R32F 不可用时回退 raster；`--depth-pyramid-compute/--no-depth-pyramid-compute`
  - GPU-driven geometry：opt-in `--gpu-driven`，forward opaque、deferred GBuffer、opaque depth prepass 与 forward transparent 使用 frustum + 独立 R32F max Hi-Z pyramid culling；depth prepass 按 HDRP 两阶段语义用 previous-frame max 先 cull，再以 current intermediate max 对 culled 集合 retest；标准 Lit Blend 透明使用 bindless GPU sorting（drawRecords + rank sort + sorted indirect），D3D11 按 sorted record 间接 draw、Vulkan 用 `submitIndirectCount` multi-draw；D3D11 固定 draw 数 + GPU instanceCount
  - Transparent pre-color depth prepass v2：opt-in `--transparent-prepass`，在 transparent color 前写最近透明层 main depth；transparent color 按该 depth 做 LEQUAL，透明 MV 与依赖 depth 的后处理复用同一 depth；on/off 有预期的透明遮挡差异（HDRP pre-color 语义）
- Transparent pre-refraction/refraction queues v2 + refraction sampling v2（depth rejection）：opt-in `--transparent-refraction`，按 renderQueue 拆 pre-refraction/refraction 透明队列；pre-refraction 队列先写 main depth 并复制到独立 depth，refraction 队列继续写 main depth；pre-refraction color 画到独立 RGBA16F target 后以 premultiplied-alpha composite 叠加（coverage alpha，v2）；refraction sampling v2 在 refraction 队列前用同一 full-screen copy 合并 opaque+pre-refraction RGB，并把 pre-refraction depth 写入 source alpha，按 planar/box refraction ray 的屏幕空间偏移采样 source；当 offset texel 位于折射表面前方时回退 center texel（HDRP refractionOffsetMultiplier 语义，`--refraction-depth-rejection/--no-refraction-depth-rejection`，默认开启）；D3D11/Vulkan、forward/deferred split on/off 均 0 diff；depth-rejection fixture 33.45 dB/39,894 px/max175，depth rejection on/off 45.78 dB/11,252 px/max20；rough refraction v1（HDRP ColorPyramidPS 权重、screen-space 5-tap 近似，`--refraction-roughness`，默认关闭）已接；真实 color pyramid mip chain、water、distortion、proxy raycast 未接
- Dynamic resolution v1：opt-in `--dynamic-resolution`，按固定帧间隔统计实际帧时间，在 min/max scale 之间以 step 调整 render scale；每次变更调用 `ForwardRenderer::setRenderScale` 并 reset TAA/exposure/SSAO/SSR/SSGI/volumetric fog history；`--dynamic-resolution-target-fps`、`--dynamic-resolution-min-scale`、`--dynamic-resolution-max-scale`、`--dynamic-resolution-step`、`--dynamic-resolution-interval`；render-test 与交互循环均支持
- TAAU v2（实验）：opt-in `--taa-upscale`，TAA history 和输出使用 target resolution；支持 HDRP `UpsamplerScheduleType` 的 `--taa-upscale-injection=before-post|after-dof|after-post`（默认 `before-post`）；BeforePost 在 exposure 后/DOF 前，AfterDepthOfField 在 DOF 后，AfterPost 在 Uber 后（需要 Uber；缺失时回退 BeforePost，并用 final copy 输出 target）；DOF/Motion Blur/Bloom 消费当前 resolution group，场景 motion/depth 保持 render resolution；HDRP 风格 jitter 对齐、9-tap UPSAMPLE central filter、variance AABB + direct clip、5-tap bicubic history + anti-ringing、depth-dilated MV、R16F velocity magnitude history + HDRP velocity rejection（`--taa-velocity-rejection`，默认 0）、upsample confidence 与 MV-dependent blend；`--taa-upscale` 自动启用 TAA 并关闭 FXAA，render scale=1 回退普通 TAA；static 43.56 dB，orbit 40.87 dB，dynamic scale-only 变化保留 target-res TAAU history，跨后端 history 仍有差异（static 481 px/max1，orbit 4,657 px/max26；velocity rejection 0.5 时 4,392 px/max23；quality bloom 28 px/max1）
- **光照**
  - 1 个主方向光；总 light slot 支持 32/64/128（有方向光时 punctual 最多 31/63/127）
  - CPU / texture / storage-buffer 三种 light-list 路径，buffer path 默认开启
  - FPTL 16×16 tile、64×64 big-tile、64×64×32 cluster light list（M9：packed 16-bit + layered offset/global index）
  - GPU light-list producer/consumer；失败自动回退 CPU
  - `lightListCapacity=32/64/128` 容量契约，默认 64（32 为安全回退，128 为 opt-in/experimental）；buffer 路径支持 128，texture 路径受 `maxTextureSize` 限制
- **阴影**
  - 方向光级联阴影、point/spot atlas、PCF / PCSS
  - Per-light shadow settings、shadow cache、cascade fade、normal/peter-panning bias
  - Contact shadows（full/half resolution）
  - GPU shadow culling / indirect draw 为 opt-in 实验路径
- **屏幕空间效果**
  - SSAO、SSR、SSGI、DBuffer decals
  - 静态 reflection probes、sky/IBL、HDR/EXR equirectangular sky
- **体积雾**
  - Screen-space volumetric fog，低分辨率 raymarch + depth-aware upsample
  - Temporal reprojection、punctual light scattering/shadows、local fog volumes
- **后处理**
  - Fixed / Auto / Histogram exposure
  - ACES / Neutral / None tonemapping
  - Uber color grading、bloom、TAA、DOF、motion blur、FXAA
- **资产**
  - glTF 静态网格/材质/贴图/相机/灯光、TRS 动画、morph target、skin、motion vectors
  - Skin：CPU LBS 与 compute GPU skinning（`--gpu-skinning`/`--no-gpu-skinning`，默认开启）；任意 `JOINTS_n/WEIGHTS_n`、权重归一化、上一帧 joint palette motion vectors；GPU 不可用时回退 CPU
  - Draco v1、KTX2/BC1/BC3/BC5/BC7、BasisU ETC1S/UASTC 超压缩转码
- **工具**
  - Debug UI、light cluster / shadow atlas / GBuffer / depth / motion vector 调试视图
  - `--screenshot`、`--render-test`、`--gfx-test`、`--asset-test`、`--cluster-stats-readback`
  - IDL 生成 uniform block，shader variant 构建脚本
  - Shader Analysis：DXBC/SPIR-V 报告分析、diff/CSV/reference 与本地 Web Viewer（[`tools/shader_analysis/README.md`](tools/shader_analysis/README.md)）

## 当前状态

- M0–M8 已完成并验证。
- VFX 运行时 V1 完成：V1.0b–V1.4.5 全部验收；D3D11/Vulkan feature golden、VFX off/empty effect 0 diff、CPU 1024×10 reference、28 个 asset-check、V1 门禁 CTest 111/111 通过。详见 `docs/vfx/README.md`。
- VFX 编辑器 V2.0–V2.5：ImGui 节点画布 + Inspector、typed JSON 编辑/非法回滚、`.vfx.editor.json` 布局 sidecar、system/block/output/property 增删与移动、共享 block catalog 默认参数、undo/redo、GLFW→ImGui 文本输入、Curve/Gradient key、Save/Compile（`compileGraphFile` → `.vfxb`）、ParseError 点击聚焦、匹配 scene instance 的 `Runtime::reloadEffect` hot reload、Playback/Step/Replay/Time scale、未保存图临时 `.vfxb` Preview、Runtime 诊断；V2.2b 增加 block/property clipboard、subgraph invocation 插入、raw JSON expression 编辑、optional block 参数添加、dirty recovery modal 与关闭前 Save/Cancel/Discard 确认；V2.5 完成 28 个 checked-in 资产 save→reload→`vfxbake --check`、D3D11/Vulkan editor 截图与 `--no-vfx` 0-diff。`--vfx-editor=<json> --scene=<gltf>` 打开，shaderc 路径可用 CLI/env 覆盖。
- VFX 渲染 V3.0：透明粒子输出写入场景 motion-vector buffer，TAA/motion blur 可按相机 + 粒子上一帧位置重投影；复用 `FrameSettings::MotionVectors`，`.vfx.json`/`.vfxb` 结构与 v6 不变。D3D11/Vulkan CTest 以 VFX on/off 的 motion-vector debug 图差异验收。
- VFX 渲染 V3.1：per-output `castShadows` 把粒子写入 directional/punctual/area shadow atlas；透明输出按纹理 alpha 裁剪，VFX shadow 存在时禁用 static shadow cache，VFX simulation 前移到 shadow pass 前。`.vfxb` v6 复用 OUTPUTS reserved byte，29 个 checked-in 资产；D3D11/Vulkan shadow atlas CTest 验收。
- VFX 渲染 V3.2：per-output `generateMotionVector` gate；缺省 `true` 保持 V3.0 行为，显式 false 时跳过固定 MV pass。`.vfxb` v6 使用 OUTPUTS reserved byte 22 的兼容编码；`shadow_visual` 作为 shadow on + motion off fixture，D3D11/Vulkan motion debug on/off 0 diff，CTest 135。
- VFX 渲染 V3.3：local-space system 的 effect transform 逐帧历史进入 motion vectors；首帧/重置将当前矩阵作为 previous，避免初始摆放产生虚假运动；`u_vfx_prevWorld` 由 runtime 逐 system 上传，world-space 仍用 identity。runtime 单测验证 current/previous 矩阵为 1/1→2/1→3/2，既有 motion vectors/gate 双后端测试继续通过。
- VFX 渲染 V3.4：system `bounds.mode:"automatic"` 由 GPU 统计 alive 粒子的保守 AABB，经 async readback 回写 `Effect::bounds()`；SYSTEMS reserved byte 60 保存 mode，旧资产仍为 manual；editor Inspector 支持 Manual/Automatic，新增 `auto_bounds.vfx.json` fixture（30 个 checked-in 资产）。runtime mock、compiler 双后端烘焙、D3D11/Vulkan `vfx-reference` readback 与既有 motion/shadow/determinism 测试通过；CTest 137。
- VFX 渲染 V3.5：automatic bounds 增加 Unity `boundsPadding` 对齐；JSON `bounds.padding` 由 CPU 在 GPU readback/fallback AABB 上加到 extents，仅 automatic 生效。非零 padding 写入可选 `.vfxb` v6 BOUNDS section（旧资产缺省 0），manual bounds 不变；editor 增加 padding 字段，`auto_bounds` fixture 验证 mock/local transform 和 D3D11/Vulkan 真实 readback。
- VFX 渲染 V3.6：asset-level `culling` flags（none/simulation/visible）与 renderer camera-frustum culling；`CullSimulation` 暂停不可见 effect 的仿真/bounds，output/MV draw 按 frustum 跳过，manual/recorded bounds 可选启用。非零 flags 写入可选 `.vfxb` v6 CULLING section（id 12，旧资产缺省 0），automatic bounds 强制 none；editor Inspector 增加 Effect culling。runtime mock/parser/compiler/editor 测试与双后端 reference/motion 测试通过，CTest 137。
- VFX 渲染 V3.7：per-output `sortPriority` 控制同一 asset 内 output 的提交顺序；runtime 按 priority 升序稳定排序，默认 0 保持旧 asset 的声明顺序。复用 `.vfxb` v6 OUTPUTS 记录偏移 28（旧资产为 0），不新增 section/版本；editor Inspector 增加 Sort priority。runtime/parser/compiler/editor 测试与双后端 order/force golden 通过，CTest 137。
- VFX 渲染 V3.8：补齐 `cameraDepth`/`youngest` 粒子排序 criteria；`cameraDepth` 使用相机前向距离，`youngest` 使用 `age`，复用 SYSTEMS/OUTPUTS `sortMode` 枚举值 3/4，不改变 `.vfxb` v6 记录布局。parser/compiler/BINARY round-trip 与 D3D11/Vulkan 1024 粒子 reference 通过，既有 distance/custom golden 不变，CTest 137。
- VFX 渲染 V3.9：per-output `sortOrder` 对齐 Unity `revertSorting`，复用 OUTPUTS 偏移 11；system 仍共享一份 KVP/indirection，反序 output 在 color/MV/shadow pass 反向读取。旧资产字节不变，editor Output Inspector 增加 Inherit/Ascending/Descending；parser/compiler/runtime 测试、反序临时资产双后端 reference、既有 golden 与 CTest 137 通过。
- VFX 渲染 V3.10：per-output `sort` criterion 对齐 Unity `OutputNeedsOwnSort`；compiler 对额外 criterion 生成 `SortReset`(task 16)/output-update/sort pipeline，runtime 在 color/MV/shadow draw 前串行重排共享 KVP/indirection。KERNELS offset 7 tag、OUTPUTS offset 10 effective criterion，不新增 buffer/section；新增 `sort_outputs` fixture（31 个资产）。parser/compiler/editor/runtime 测试、双后端 sort_outputs smoke 与 `vfxbake --check` 通过，CTest 139。
- VFX 渲染 V3.11：按 Unity quad output 的 2048 粒子/draw instance 上限固定分块（Unity indexed fill 只在 >16384 时分块，本项目 v1 固定分块）；`fill_indirect` 写 `numVertices=6*min(count,2048)`、`numInstances=ceil(count/2048)`，output/MV/shadow VS 用 `gl_InstanceID*2048` 选择切片并对 partial tail 做退化 guard。`.vfxb` v6 section/record 不变，但所有资产因 shader/fillIndirect 重烘焙；`reference_sorted_8192` 以 4 instances 通过双后端 reference，且与 HEAD 旧 1-instance `.vfxb` 的 8192 粒子截图逐字节相同；临时 2560 粒子 partial 资产通过 `[12288,2]`，motion/shadow/sort_outputs smoke 通过；compiler 30/30、runtime 26/26，CTest 139。
- VFX 渲染 V3.12：对齐 Unity per-output sort task 的独立 dstBuffer，runtime 为每个额外 sort criterion 分配 sorted indirection；color/MV/shadow 绑定各自 stage 13 列表，criterion 切换不再重排共享列表。KVP/RenderCount/indirect args 仍共享，.vfxb/compiler/API 不变；sort_outputs 扩展为 distance→youngest→distance 验证基础列表复用。runtime 26/26、asset check 31 资产、editor roundtrip 31 资产、D3D11/Vulkan sort/MV/shadow smokes 通过，CTest 139。
- VFX 渲染 V3.13：对齐 Unity `VFXBoundsUtils.hlsl:52-59` 的 flat element 2D bounding radius，automatic bounds 的 per-particle 半径从 `0.5*abs(size)*length(scale)` 收窄为 `0.5*abs(size)*length(scale.xy)`，忽略 quad 输出从不使用的 `scale.z`。`.vfxb` section/version/API 不变，`auto_bounds` fixture 增加 `scale.z=4` 并重烘焙；compiler 30/30（616）、runtime 26/26（605）、双后端 auto-bounds reference 与 31 资产 `vfxbake --check`/editor roundtrip 通过，CTest 139。
- VFX 渲染 V3.14：对齐 Unity `VFXOutputUpdate.template` 的 per-output indirect/KVP 状态，runtime 为每个额外 sort criterion 分配独立 KVP、RenderCount 和 indirect args；`ensureOutputSort`/`fillIndirect` 写对应 set，color/MV/shadow 使用对应 stage 13/14 与 args，base 状态不再被 extra sort 覆写。`.vfxb`/compiler/API/资产不变；runtime 26/26（628）、12 个双后端 sort/reference/shadow/motion smoke 通过，CTest 139。
- VFX 渲染 V3.15：参考 Unity `VFXInstancing.hlsl`/`VFXDataParticle` 的 per-instance simulation 语义，增加 opt-in `--vfx-instancing`；相同 asset/seed/property/playback 的 local-space scene instance 共享一次 simulation 与 GPU buffer，各自保留 transform、bounds、frustum cull 及 color/MV/shadow draw。v1 不实现 instance split、prefix-sum batch 或 instanced draw，`distance`/`cameraDepth`/`custom` 仍回落独立 effect。runtime 29/29（671）、hbg 98/98、adapter 8/8、hbg_tests 309/33,433；D3D11/Vulkan `hbg_vfx_instancing` 的 on/off 截图逐字节相同，32 goldens+12 smokes 通过，CTest 141。
- VFX 渲染 V3.16：新增 opt-in `--vfx-transparent-sort`，按 Unity/HDRP transparent RendererList 的 `CommonTransparent | RendererPriority` 思路，以 effect bounds 中心生成透明键，把每个 VFX effect 的 pass 插入 sliced 场景 transparent DrawList。VFX 内部 KVP 粒子排序不变；pre-refraction/backface/GPU-driven 路径回退旧顺序。新增 `vfx_transparent_order` 双后端 off/on CTest 与 golden；runtime 29/29（671）、hbg 98/98、adapter 8/8、hbg_tests 315/33,480、compiler 30/30，通过 32 goldens、16 reference、11 smokes，CTest 143。
- VFX 渲染 V3.17：在 V3.15 共享 simulation 的基础上增加 opt-in instanced draw。全部 instance 可见时，`fill_indirect` 写 `numInstances=particleChunks*effectInstances`，stage 15 `vfx.instance.transforms` 保存每 instance current/previous `Mat4`，生成 output/MV/shadow VS 将 `gl_InstanceID` 分解为 effect instance + particle chunk；每个 output 一次 indirect draw。任一 instance cull 时回退逐 instance draw，N=1 保留旧路径；无 property/texture split、prefix-sum batch 或 active/batch indirection。fixture 右侧增加 rotation/scale；D3D11/Vulkan color/MV 截图 0 diff，`vfx_draws` 4→2。runtime 30/30（699）、hbg 98/98、adapter 8/8、compiler 30/30（622）、hbg_tests 315/33,500，62 个 VFX CTest 通过，CTest 144。
- VFX 渲染 V3.18：参考 Unity `VFXInstancing.hlsl:69-80` 的 current→active 映射，在 V3.17 transform buffer 末尾追加 active instance 列表；`u_vfxInstanceParams={total,active,base,flag}`，partial cull 且可见数 >1 时 color/MV 仍一个 batch draw，inactive slot 只输出退化顶点；shadow 始终按 total 绘制全部 instance，可见数=1 保留逐 instance fallback。新增 3 instance、第三个视锥外的 `vfx_instancing_cull` fixture；D3D11/Vulkan color/MV 0 diff，off `vfx_draws=4`、on 2。
- VFX 渲染 V3.19：参考 Unity `VFXInstancing.hlsl:10-12,29-31,81-92` 的 active/batch 尺寸语义，为每个 system/sort list 懒创建 `.active.indirect`；`0 < active < total` 时用现有 `fill_indirect` 以 active count 写 args，color/MV 不再提交 total slot，shadow 继续 total args。visible=1 也改走 active=1 args，修复 fallback 按 total instance 重复提交。新增中间 instance 在视锥外、active list 为 `[0,2]` 的 `vfx_instancing_cull_gap`；D3D11/Vulkan color/MV 0 diff，off `vfx_draws=4`、on 2。runtime 31/31（743）、compiler 30/30（626）、hbg 98/98、adapter 8/8、hbg_tests 315/33,525，90 个 VFX CTest 通过，CTest 146。
- VFX 渲染 V3.20：参考 Unity `VFXBoundsUtils.hlsl:73-85` 的 tight extents，用 camera right/up、粒子 angle/scale 与 size 计算 flat billboard 的 oriented AABB extents，替代 V3.13 的 2D 球半径；保留球半径 fallback，bounds readback/padding/host culling 不变。只重烘焙 `auto_bounds.vfxb`；D3D11/Vulkan reference 均有效，tight y/z 为 `±0.6/±0.75`（旧球半径 `±0.6414/±0.8914`）。runtime 31/31（751）、compiler 30/30（631）、hbg 98/98、adapter 8/8、hbg_tests 321/33,638，90 个 VFX CTest 通过，CTest 146。
- VFX 渲染 V3.21：参考 Unity `VFXBoundsUtils.hlsl:150-168` 的 per-instance bounds 语义，新增 `Effect::visibleBounds()`，只合并未 cull 且有效的 instance bounds；V3.16 的透明 effect-level 排序中心改用该可见 bounds，全部 cull 时回退 primary instance position。`bounds()` 仍保留全量并集，simulation/shader/资产/buffer/CLI 不变。runtime 31/31（761）；D3D11/Vulkan transparent-sort smoke 与 90 个 VFX CTest 通过，CTest 147。
- VFX 渲染 V3.22：参考 Unity `VFXInstancing.hlsl:10-12,29-31,81-92` 的 active/batch 尺寸语义，增加 opt-in `EffectDesc::instanceBatchLimit` / `PipelineSettings::vfxInstanceBatchSize` / `--vfx-instance-batch=<n>`；active instance 按固定大小切成连续 batch，每个 batch 懒创建 `.active.indirect[.N]` 并用现有 `fill_indirect` 写 args，`u_vfxInstanceParams.z` 传 `activeBase + batchOffset`，shader 源码不变。color/MV 每 batch 一次 draw；V3.22 的 shadow 仍 total-sized batch。runtime 32/32（792）；D3D11/Vulkan instancing split color/MV 0 diff、4 draws；hbg_vfx_tests 98/98、adapter 8/8、compiler 30/30（507）、hbg_tests 323/33,687、CTest 148。
- VFX 渲染 V3.23：参考 Unity `VFXInstancing.hlsl:5,10-17,37-54,94-103` 的 `instancingCurrentOffset` 语义，把 `instanceBatchLimit` 扩展到 shadow：按 total instance 的连续 batch 懒创建 `.shadow.indirect[.N]`，`u_vfxInstanceParams.z` 传 batch offset，shadow VS 将其加到 batch-local instance index；shadow 仍绘制全部 instance、不读 camera active list。runtime 33/33（814），覆盖 3 instance/limit2 的 2 次 shadow draw、params `{3,2,0,0}`/`{3,1,2,0}` 与 cull 不变性；DXBC/SPIR-V shader 编译通过。D3D11/Vulkan 手工 smoke（3 个 local shadow instance、limit=1）shadow draws 4→12、shadow atlas 0 diff；当时共享 shader manifest 被并行 color-pyramid 工作改坏，测试时临时补齐并恢复，未把 fixture/CTest 加入仓库。
- VFX 渲染 V3.24：参考 HDRP `HDRenderPipeline.cs:3460-3475` 的 transparent RendererList 排序与 Unity `VFXInstancing.hlsl:5,10-17,37-54` 的 fixed-size batch offset，在 `--vfx-transparent-sort` + `--vfx-instance-batch>0` 时把每个 active instance batch 作为独立透明提交：union bounds 中心作为 key，逐 batch 使用 `.active.indirect[.N]` args 和 `VFX Output N Batch B` pass。`n=1` 即逐 instance interleave，`0` 保持 V3.16 effect-level。runtime 34/34（843）；新增 D3D11/Vulkan `vfx_transparent_batch_order`（3 grouped instance + panel）：batches 1→3、vfx_draws 2→6、两种顺序差 4,680 px/max81，双后端 golden 与跨后端 max1 通过；VFX CTest 106/106、hbg_tests 325/33,719、CTest 150。MV/shadow 不随透明 batch 拆分。
- M8.0 / M8.1 已完成：容量设置、CLI/JSON、IDL、CPU 32/64/128 reference、动态 stride、cluster 32/cell。
- M8.2 已完成：GPU producer 支持 32/64/128（128-slot AABB、4×32-bit mask、运行时 `lightListParams2` stride），GPU 路径的 32 容量 gate 已移除。
- M8.3 已完成：GPU cluster 保持 32/cell，第 33 个起的候选按 cluster 记录 overflow 并打包到 count texel；`--debug-view=light-cluster` 以品红显示 overflow，绿色强度随数量增加。
- M8.4 已完成：texture/CPU 路径按 backend `maxTextureSize` 和 16383 pitch 上限自动 128→64→32 clamp（buffer 路径保持 128），fog/contact/debug consumers 移除 32 硬编码。
- M8.5 已完成：DebugUI 完整显示 requested/effective/stride/truncated/clamped 并提供 GPU cluster stats 按钮；`--cluster-stats-readback` 在 buffer/texture 路径读回 overflow clusters/lights、max overflow/max lights；1440p/4K 下与 CPU reference 一致。
- M8.6 已完成：默认容量切到 **64**，32 保留安全回退，128 保持 opt-in/experimental；1080p 300 帧 full post 下 32-light 场景 capacity 32/64/128 无显著差异，64-light 场景 D3D11 **60.8–64.9 fps** / Vulkan **75.1–79.7 fps**，128-light 场景 D3D11 **40.0–43.8 fps** / Vulkan **48.2–51.3 fps**。默认 64 下 4K texture 路径保持 `capacity=64/64`、无 clamp（FPTL 宽度 15600）。
- 64/128 在 D3D11/Vulkan 的 buffer/texture、cluster/no-cluster、forward/deferred/transparent/fog/contact 与 CPU reference 均为 **0 diff**；32-light M7 buffer/texture/CPU 基线仍为 **0 diff**；默认 64 的 64-light GPU vs CPU 在双后端 **0 diff**。
- 1440p/4K texture 路径 clamp 后 GPU/CPU 为 **0 diff**；4K buffer 128 保持 `capacity=128/128`。
- M8.7 已完成验收：M7 32-light 基线、64/128 的 forward/deferred/fog/contact/debug/transparent/no-cluster、buffer/texture/CPU、D3D11/Vulkan 共 **126 组 0 diff** 对比；33 同位置灯、129 候选截断、128 cluster overflow、resize 资源重建均有 CPU/GPU 读回证据。`ctest` **209 cases / 20,225 assertions** + IDL/gfx-test 双后端通过。64-light/64 forward D3D11 7 次 **58.9–66.5 fps**（median 64.1，GPU 11.67–16.14 ms），Vulkan **69.1–76.3 fps**；默认 deferred D3D11 余量较小（median 61.7），保留 32 安全回退。
- D3D11 / Vulkan 的 32-light buffer、texture、CPU 三条路径对 M7 基线截图为 **0 diff**。

详细里程碑和验证记录见 [`docs/implementation-plan.md`](docs/implementation-plan.md) 和各 `docs/mN-checklist.md`。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022，安装 **Desktop development with C++**
- CMake **3.24+**
- Python 3（IDL 生成和 shader 构建推荐；缺少 Python 时会使用已提交的生成文件，但无法构建 shader）
- 支持 D3D11 或 Vulkan 的 GPU

首次配置时 CMake 会通过 `FetchContent` 拉取 bgfx、GLFW、cgltf、Google Draco、Basis Universal、MikkTSpace、nlohmann/json、doctest。离线环境可设置 `HBG_BGFX_CMAKE_DIR`、`HBG_GLFW_DIR`、`HBG_CGLTF_DIR`、`HBG_DRACO_DIR`、`HBG_BASISU_DIR`、`HBG_MIKKTSPACE_DIR`、`HBG_NLOHMANN_JSON_DIR` 指向本地依赖；不需要 Draco 时设置 `-DHBG_ENABLE_DRACO=OFF`，不需要 BasisU KTX2 超压缩转码时设置 `-DHBG_ENABLE_BASISU=OFF`。
非 glTF 场景导入可选启用 Parcel：`-DHBG_ENABLE_PARCEL=ON -DHBG_PARCEL_ROOT=<Parcel> -DHBG_PARCEL_ENTT_ROOT=<entt> -DHBG_PARCEL_ASSIMP_ROOT=<assimp>`。`.fbx/.obj/.dae/.3ds/.ply/.stl` 走 Parcel Assimp importer + `SceneRenderBridge`；`.blend` 通过 `-DHBG_BLENDER_EXECUTABLE=<blender.exe>` 调 Blender headless 转 GLB。Parcel 未启用时仍只支持 `.gltf/.glb`。

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
# 全量 CTest；VFX 功能迭代请按 docs/vfx/README.md 的 CTest label 只跑相关项
ctest --preset vs2022-debug
py -3 tools/idl_gen.py --check
```

可执行文件：

```text
build\vs2022\Debug\hbg.exe
```

Release 风格构建：

```powershell
cmake --build --preset vs2022-relwithdebinfo
```

## 运行

交互式加载 glTF 场景：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=C:\path\to\scene.gltf
```

Vulkan：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=vulkan --scene=C:\path\to\scene.gltf
```

加载项目配置：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --scene=C:\path\to\scene.gltf `
  --pipeline-settings=config\pipeline.json `
  --frame-settings=config\frame_settings.json
```

离屏渲染并截图：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --render-test=C:\path\to\scene.gltf `
  --frames=3 `
  --screenshot=out.png
```

Deferred + 后处理示例：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --render-test=C:\path\to\scene.gltf `
  --frames=3 `
  --deferred `
  --pipeline-settings=config\pipeline.json `
  --frame-settings=config\frame_settings.json `
  --volumetric-fog `
  --fog-profile=config\volumetric_fog.json `
  --screenshot=deferred.png
```

资源/渲染基础自测：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=vulkan --gfx-test
```

完整命令行参数：

```powershell
.\build\vs2022\Debug\hbg.exe --help
```

常用参数：

| 参数 | 说明 |
|---|---|
| `--scene=<gltf>` | 交互式场景 |
| `--render-test=<gltf>` | 渲染指定帧后退出 |
| `--frames=<n>` | render-test 帧数，默认 3 |
| `--screenshot=<png>` | 最后一帧写 PNG |
| `--backend=d3d11\|vulkan` | 后端选择 |
| `--deferred` | 切换到 deferred |
| `--ssao-temporal` / `--no-ssao-temporal` | SSAO history reprojection（默认关） |
| `--ssr-temporal` / `--no-ssr-temporal` | SSR history reprojection（默认关） |
| `--ssgi-temporal` / `--no-ssgi-temporal` | SSGI temporal accumulation（默认关，仅 deferred） |
| `--pipeline-settings=<json>` | PipelineSettings |
| `--frame-settings=<json>` | FrameSettings |
| `--render-scale=<0.1..1.0>` | 动态分辨率/RT scale |
| `--taa-upscale` / `--no-taa-upscale` | TAA temporal upsampling，target-res history（默认关；需 render scale < 1） |
| `--taa-velocity-rejection=<0..1>` | TAAU velocity magnitude history rejection（默认 0；需 `--taa-upscale`） |
| `--depth-pyramid-compute` / `--no-depth-pyramid-compute` | R32F min-only compute depth pyramid / 旧 raster 路径（默认 compute） |
| `--gpu-driven` / `--no-gpu-driven` | opaque/transparent GPU frustum + Hi-Z culling、间接 instance draw（默认关；forward/GBuffer/transparent） |
| `--vfx-instancing` | 兼容 scene VFX instance 共享 simulation；全部/部分可见时 batch color/MV，partial/单可见使用 active-count args，shadow 始终 batch（默认关；V3.15/V3.17/V3.18/V3.19） |
| `--vfx-instance-batch=<n>` | 将分组 effect 的 active instance 按 n 个/批切分 color/MV indirect draw，并把 shadow 按 total instance 切分；与 `--vfx-transparent-sort` 同开时每个 active batch 单独插入透明段（n=1 逐 instance）；0 = 不切分（默认 0；V3.22/V3.23/V3.24） |
| `--vfx-transparent-sort` | 将 VFX effect 按 effect-level 深度插入场景 transparent DrawList；若同时设置 `--vfx-instance-batch>0`，改为按 active batch 插入（默认关；V3.16/V3.24） |
| `--gpu-light-list` / `--no-gpu-light-list` | GPU light list 开关 |
| `--gpu-light-list-path=buffer\|texture` | light-list 存储路径 |
| `--light-capacity=32\|64\|128` | 容量请求；buffer 支持 32/64/128，texture 受 `maxTextureSize` 限制 |
| `--light-list-feature-flags` / `--no-light-list-feature-flags` | 启用/禁用 tile category gating（默认启用；关闭仅用于 M11.3 评估） |
| `--cookie-atlas` / `--no-cookie-atlas` | 启用独立 native cookie atlas / 使用 legacy 8×8 tile（默认启用） |
| `--cookie-atlas-size <512\|1024\|2048>` | Cookie atlas 分辨率（默认 1024） |
| `--no-light-cluster` | 强制走 FPTL，不使用 cluster |
| `--debug-view=<mode>` | depth / normal / motion / light-cluster / shadow atlas / `ssao\|ssr\|ssgi` 的 `-raw`、`-history`、`-denoised` 等调试视图 |
| `--contact-shadows` | 开启 contact shadows |
| `--volumetric-fog` | 开具体积雾 |
| `--shadow-profile=<json>` | 加载阴影配置 |
| `--directional-shadow-cache-mixed` / `--no-directional-shadow-cache-mixed` | 启用/禁用 cached directional 的 static/dynamic mixed（默认启用；需 `--directional-shadow-cache`） |
| `--postprocess-profile=<json>` | 加载后处理配置 |
| `--sky=<hdr\|exr>` | 加载 HDR sky |

### Debug UI

- `F1`：显示/隐藏面板
- `F12`：截图到工作目录
- `F11`：RenderDoc capture（需要 `renderdoc.dll` 已加载；未安装时忽略）

## 项目结构

```text
src/
  app/           窗口、CLI、Application、DebugUI、gfx/render test
  asset/         glTF、材质、贴图、场景 sidecar 加载
  core/          日志、Profiler、数学库
  gfx/           bgfx 封装、Texture/Buffer/Uniform/Encoder/RenderGraph 基础
  render/
    camera/      ViewState、ResolutionGroup
    culling/     DrawList、frustum、sort key
    graph/       RenderGraph
    lighting/    LightBuilder、LightListBuilder、GPU light data
    passes/      ForwardRenderer 及 pass 记录
    pipeline/    FrameSettings、PipelineSettings
    postprocess/ SSAO/SSR/SSGI、Bloom、TAA、DOF、Motion Blur、Color Grading
    shadows/     ShadowManager、ContactShadow、ShadowSettings
    sky/         Sky/IBL、EnvironmentTexture、ReflectionProbe
    atmosphere/  VolumetricFog、LocalFogVolume
shaders/         .sc shader 源码、manifest、生成产物
config/          Pipeline/Frame/Shadow/PostProcess JSON、uniform IDL
tools/           idl_gen.py、shader_build.py
tests/           doctest 单元测试和 gfx test
docs/            里程碑清单、实现计划、验证记录
```

## HDRP 参考

实现时对照了 Unity HDRP Runtime 和 Core 的以下部分：

- `FrameSettings` 字段/位布局、Pipeline/Frame 配置
- `LightLoopDef.hlsl`、FPTL/big-tile/cluster list 读取语义
- `ShaderConfig.cs` 的 FPTL 容量与 cluster cell 上限
- `HDRenderPipeline.LightLoop.cs` 的 GPU light-list buffer 分配
- HDRP 的 ACES/Neutral tonemapping、PCSS、contact shadow、volumetric fog、color grading
- bgfx 源码中的 D3D11/Vulkan 绑定、compute、indirect 和 shader 编译路径

项目没有直接引入 HDRP 运行时代码；概念对齐和差异记录在 `docs/m*.md` 中。

## 离线 GI（MagicDawn）

Dawn GI 的 C++ 接入（协议重写、UV2/atlas、lightmap 采样）规划见 [`docs/dawn-gi-plan.md`](docs/dawn-gi-plan.md)，进度见 [`docs/dawn-gi-checklist.md`](docs/dawn-gi-checklist.md)。v1 范围：lightmap-2D + SH L1，不含 PRT/Probe/ShadowMask。

## 已知限制

- 当前只重点验证 Windows D3D11 / Vulkan；不做 D3D12 / DXR / ray tracing。
- GPU 路径支持 effective light capacity 32/64/128，默认 64；64 在 4K texture 路径宽度 15600，仍在 16383 pitch 上限内，因此默认容量在 4K texture/CPU 路径不 clamp；128 在 1080p texture 路径宽度 15480，1440p+ 受 bgfx `uint16_t` pitch（宽度上限 16383）和 backend `maxTextureSize` 共同限制，自动 clamp 到 64/32；buffer 路径不受纹理宽度限制。
- Cluster 已改为 layered offset + global index 布局（M9.2），使用确定性 `clusterIndex * clusterCapacity` offset；normal capacity 128，8K/128 自动降为 64，不再有固定 32/cell overflow。`--debug-view=light-cluster` 与 `--cluster-stats-readback` 仍可用于诊断。
- Contact shadow mask 固定 8 bit（最多 8 盏灯，含方向光），volumetric fog 的 punctual 上限仍由 fog settings 控制（默认 8），两者不随 light capacity 扩展。
- M9 已实现 16-bit packed light index、light category/feature flags、light layers（forward uniform；deferred 复用 GBuffer alpha 通道）、spot/point cookie、spot IES（light data texel 打包，非独立 atlas）以及 128 shadow requests/4096 atlas opt-in（CPU 排序 shelf packing）。cap32/64/128 × 1080p/1440p/4K × D3D11/Vulkan × forward/deferred 的 buffer/texture/CPU 三路径已全部 0 diff，透明 light layers 已覆盖；1080p 300 帧 full post 实测 cap64 55.2 fps、cap128 29.2 fps，尚未达到 cap64 >60 fps 门禁；M9.7 仅剩性能项。
- M10 已完成（M10.0–M10.8）：light-data 扩到 83 texel（11 base + 8×8 cookie + 8 IES），LTC GGX 表追加在 light-data texture 行；Rectangle area light 使用 LTC GGX + `PolygonIrradiance`，Tube 使用 HDRP line LTC（`I_diffuse_line` + `ComputeLineWidthFactor`），Disc 明确 unsupported；GPU category producer 已接通 Punctual/Area 双层；area/directional cookie、cookieScaleOffset/repeat、point IES 和 spot IES cutoff 已接通；tile feature flags 嵌入 FPTL/big-tile/cluster list，runtime 按 category gating，并新增 GPU flags readback（area16/64-light 均 `flags_checked=240/mismatch=0`）。M10.7 新增 rectangle-only 独立 area shadow atlas（默认 1024、512 分辨率、spot-like cone、PCSS）；area shadow 通过 stage 13 depth atlas 采样，active 时 point PCSS 回退 tent。M10.8 矩阵：area rect 在 1080/1440/4K × D3D11/Vulkan × forward/deferred × buffer/texture/CPU 共 36 组对比全部 0 diff；4K/cap128 `capacity=128/128`、FPTL 15600×288、无 clamp；area/directional cookie 与 point IES on/off 双后端可区分；M9 64-light 与 transparent64 当前 buffer/texture/CPU 均 0 diff；720p forward GPU 中位 area off 1.273 ms、1/4/16 area 1.448/1.624/2.717 ms。已知限制：跨后端 deferred area 有极少边缘像素精度差异，M9.7 full-post cap64 55.2 fps 未达 >60，官方 shader_build O3 全量极慢且尚未完成。 M10 收口后补 area Rectangle/Tube 屏空间 OBB（HDRP `HDGpuLightsBuilder.Jobs.cs`），CPU `LightListBuilder` 与 GPU `light_screen_aabb*` 同步，Point/Spot/Disc 保持 sphere fallback；area 1080 双后端 forward/deferred 0 diff，area16 cluster flags/offset 0 mismatch。
- M11 进行中（M11.0 fixtures/perf baseline 与 M11.5 full O3 收口待做；当前全量 O3 518/0 zero-byte）：M11.0 shader build 工具 v1 已完成（超时/进度/失败报告/manifest 校验/stale 清理；518 variants 增量缓存验证 0 failed，manifest 与 binary 一致）。M11.2 cached area shadow v1 + mixed 已完成：all-static 使用独立 `shadow.cachedAreaAtlas`；mixed cached/dynamic 使用 dynamic atlas 的 static-first persistent slots，静态 request 只更新一次，动态 request 每帧绘制，all-static ↔ mixed 切换强制重建；D3D11/Vulkan × forward/deferred 1280×720 all-static 与 mixed 对 every-frame 均 **4/4 0 diff**，mixed warm 帧 `shadow_area_cache=true`、`shadow_cached_area=2`、`shadow_drawn=4`（4 area 中 2 静态）。新增 `--area-shadow-cache/--no-area-shadow-cache`、`cachedAreaShadows` 配置与 `shadow_area_cache/shadow_cached_area` diagnostics。**M11.1 sampler gate 已通过**：按 HDRP `StructuredBuffer<HDShadowData>` 思路把 stage 8 shadow context 改为 40-uint/request 的 storage buffer（`Buffer<uint>` at t8），不再创建 sampler；D3D11/Vulkan cached vs every-frame 4/4 0 diff。**M11.1b cookie atlas v1 已实现**：light data 压缩到 19 texels/light，64×128 cookie atlas 复用 light data 纹理（8×8 tile、128 capacity），texel7.z 存 tile index，texel7.w 打包 areaShape/hasIes；area/spot cookie 在 D3D11/Vulkan forward/deferred 的 buffer/texture/CPU 三路径均为 0 diff。**M11.1c 独立 cookie atlas 已完成**：新增 1024（512/2048 可配）RGBA8 `s_cookieAtlas`（stage 15，约 5.33 MiB 含 mip），light data 改为 20 texels/light（texel11 存 atlas scaleOffset）并缩为 64×192；按 HDRP `LightCookieManager`/`CookieSampling.hlsl` 实现 2 px padding、mip、`RemapUVWithPadding`、area LTC hit UV mip；源分辨率上限 256，point octa/IES 共用 native atlas。为释放 sampler，reflection probe 两张纹理合并为 `s_probeMaps` 2D array（stage 14），stage 15 给 cookie，不新增 sampler。`--no-cookie-atlas` 保留 8×8 legacy `texelFetch` fallback，`--cookie-atlas-size=512|1024|2048` 已接入；`m11_area16_cookie` native vs legacy 8/8 组合有可见差异（forward 136,925/max82、deferred 136,924/max82），D3D/VK 1 px/max1、texture/buffer 0 diff；`hbg_tests` 231/29,299。按 HDRP `LightCookieManager.Fetch2DCookie` 的 `NeedsUpdate` 语义增加 atlas content-hash 上传缓存，未变化时跳过 5.33/21.3 MiB 上传；并按 `PowerOfTwoTextureAtlas.RelayoutEntries` 的 biggest-to-smallest 顺序分配 tile。已知限制：native 源 256、largest-first shelf（非二叉树 allocator）、atlas 满白 fallback、无 streaming。**M11.3 variant strategy 评估曾否决 view-wide 4 类 category variants / 29 Lit variants（M12.3 已用 per-tile 实现）**：对照 HDRP `Lit.hlsl` 的 29 个 `kFeatureVariantFlags` 与 per-tile indirect dispatch，当前全屏 program + per-pixel tile flag 无法用 view-wide 变体替代；新增 `--no-light-list-feature-flags` 评估开关，area16/spot/point100 × D3D11/Vulkan × forward/deferred 12 组及 texture/CPU 8 组 gating on/off 全部 0 diff；1080p 300 帧 full post stress 中 D3D11 gating 约省 1.27 ms。**M11.4 1080p 60fps 已完成**：按 HDRP `HDShadowContext.hlsl` 的 `StructuredBuffer<HDShadowData>` 语义，把 shadow context 的 10 个 `float4` 改为一次 vector load（D3D11 `Buffer<float4>`、GLSL `vec4[]`、SPIR-V `StructuredBuffer<float4>`），不新增 sampler/SRV；`m8_lights64_perf` 1080p 300 帧 full post cap64 稳定中位数 D3D11 forward **63.7 fps**（15.70 ms，GPU 11.9 ms）、deferred **62.3 fps**，Vulkan forward **64.6 fps**、deferred **63.2 fps**；cap128 记录 37.5/40.1 fps，保持 opt-in。Vulkan forward 新旧 shader 0 diff；D3D11 no-TAA 为 146,139 px/max 7、PSNR 56.86 dB，差异集中在 shadow 边缘 1–2 ULP（vector load 的 HLSL 编译重排），M11.5 已接受该 D3D 精度例外，并发现 area shadow 需要 typed context：新增仅 D3D area 使用的 `shadow.contextArea`（StorageVertex/RGBA32F，仅 area request 时上传），area shadow 4/4 0。M11.5 核心矩阵：M10 area 36/36 0、area shadow 4/4 0、M9/transparent 路径 16/16 0、M11.1c native cookie 8/8 有差异（D3D/VK 1 px/max1、path 0）、cached 4/4 0、mixed 4/4 0、variant 8/8 0；M9 64 D3D 对 M10 基线为 127,795 px/max11（1–2 ULP，接受）。M8.7 base32 已用同一历史命令复跑：Vulkan 0，D3D typed 19,074 px/max165；HDRP 风格 structured context 诊断构建为 0 但 GPU 成本约 2×，未作为默认。全量 clean O3 已完成（518/0 zero-byte）；M10 fixtures/perf baseline 仍待做。
- M12.0 边界台账：M3–M11 剩余边界、关闭条件、A/B/C/D 分类和 M12 里程碑拆分见 [docs/m12-checklist.md](docs/m12-checklist.md)。
- M12.1 temporal screen-space：SSAO 使用 `--ssao-temporal`（RGBA8 双缓冲，TAA 语义重投影 + depth rejection）；SSR 使用 `--ssr-temporal`（RGBA16F 双缓冲，surface motion 重投影 + depth 拒绝，current-dominant 0.1 权重）；SSGI 使用 `--ssgi-temporal`（RGBA16F RGB+sample-count history、R32F depth、RGBA8 normal，sample-count 累积 + depth/normal rejection）。三者默认 opt-in，temporal off 保持旧路径。新增 `ssao|ssr|ssgi` 的 `-raw` / `-history` / `-denoised` debug view；`-history` 显示上一帧 temporal buffer，并自动打开对应 temporal 开关。真实窗口 resize、DebugUI render scale、setter temporal toggle 均会重建 history；SSR/SSGI temporal 已修正取样 Y 翻转，orbit ghost 消失。M11.5 64-light 1080p full-post 3 次中位数在当前机器为 D3D11 off/SSAO/SSR/SSGI **55.1/51.6/50.8/44.3 fps**、Vulkan **47.0/45.4/47.1/38.8 fps**，**>60 fps 门禁仍未通过**，需安静 GPU 环境复测。实现与验收见 [docs/m12.1-checklist.md](docs/m12.1-checklist.md)。
- M12.2 shadow v2（进行中）：P0 带 shadows 跨后端分叉已修复——shadow.context 改为 typed StorageVertex/RGBA32F，atlas PCF texel 改为 atlasRect.xy * requestParams.zw（1/atlasSize），point face 加 6° guard；D3D/VK m26_ssgi_corner forward/deferred 0 diff。Area + point PCSS 共存：point depth 固定 stage 9，area stage 13，directional stage 12，三者可同时开启；area PCSS 已按 HDRP `PCSS.hlsl:267-360` / `HDShadowSampling.hlsl:306-357` 的 cone blocker/filter 重写（clumped blocker + uniform filter、Fibonacci 方向、screen-space IGN jitter、standard-depth cone），640 D3D/VK **98.08 dB/max1/7 px**、1280 **100.24 dB/max1/17 px**、forward/deferred 640 **59.27 dB/max41**；EVSM/VSM/MSM vs 新 PCSS 1280 **41.03/40.98/40.98 dB**（旧 area PCSS 对比 60.63/60.49/60.48 已标注为旧值）；m122_area_point_dir 全 PCSS D3D/VK **83.32 dB/max10/219 px**。Cached directional 已完成 per-cascade partial v1，并完成 HDRP static/dynamic mixed v2：cache hash 只覆盖 static caster，dynamic caster 每帧叠加；新增 `shadow_copy_depth` depth blit 与 sidecar `objects[].staticShadowCaster`（默认 static）。EVSM v1 已接 `evsm` filter/mode 5：HDRP Chebyshev + exponential warp；area EVSM 已按 HDRP `EVSMBlur.compute` 对齐为 half-res RGBA32F moment atlas（512² ×2，约 8 MiB）与 2x2 point 下采样 + LDS 水平/垂直 9-tap 分离 blur，area filter 为 `evsm` 时 stage13 绑定 blurred moments；directional/punctual EVSM 仍为 local 3x3 gather。area EVSM 640 D3D/VK **97.50 dB / max1 / 8 px**（sample-side 修正后；修正前 92.55 dB / 25 px），forward vs deferred **59.27 dB / max41 / 30,796 px**（sample-side 修正后；normalBias=0 暴露两条路径的深度重建差异，修正前 60.26 dB / max4）（half-res LDS 与旧全分辨率 9x9 为 **65.24 dB / max11 / 1,287 px**）；EVSM 采样已接 HDRP per-light `evsmParams`（exponent/leak/variance，默认 15/0/1e-5，area compute 与片元共享 per-request 值）；area 已接 HDRP `evsmBlurPasses`（0..8，ping-pong half-res moment atlas + 额外 Blur pass + 奇数次 final copy；`m10_area_shadow` 1280 修正后 blur0→blur3 **65.51 dB/max8**、blur0→blur4 **63.22 dB/max9**、blur0→blur8 **57.23 dB/max12**，blur0/3/4/8 D3D/VK **100.24/99.54/99.99/100.24 dB/max1**），directional/punctual 仍为 local gather、不消费该 pass；m10_area_shadow directional/point EVSM 历史指标见 m12.2。VSM v2 已完成 directional half-res moment atlas + 9-tap 分离 blur（`shadow.directionalVsmMoments`，stage 6 linear 采样；area/punctual 仍为 local 3x3）；640 `m10_dir_shadow` VSM moment D3D/VK **92.38 dB/max2**，forward **95.07 dB/max1**，advanced **64.36 dB/max2**，cached on/off **0 diff**。area VSM vs 旧 area PCSS 60.49 dB、vs EVSM 78.57 dB，D3D/VK 100.24/102.55 dB。MSM v2 已启用 directional half-res 4-moment atlas + 9-tap 分离 blur（`shadow.directionalMsmMoments`，stage 6 linear 1-tap；punctual/area 仍 local）：参考 HDRP `SampleShadow_MSM_1tap` / `ShadowMoments_SolveMSM` / Delta3/Delta4；640 `m10_dir_shadow` MSM v2 D3D/VK **55.10 dB/max131**，local v1 vs v2 **40.76/40.63 dB**，MSM vs PCF7x7 **36.67/36.68 dB**；`advall` advanced material **52.83 dB/max48**。IMS v1 已接 `ims` filter/mode 8：directional-only RGBA32F 4-moment atlas + per-slot SAT，3 个 compute pass，stage 12 在 IMS 下切 SAT；blocker search 用 integer SAT，最终 filter 用 bilinear SAT，并接入 HDRP light-angle/maxDepthBias/kernel 参数与 CLI；参数按 request 写入 shadow context texel9，cascade 不再共享首个 request。`m26_ssgi_corner` frames=1 IMS vs MSM 53.14 dB、vs PCF 45.75 dB，D3D/VK 67.08 dB；`m10_dir_shadow` no-contact 单 cascade IMS vs PCF 46.79 dB、4 cascade 28.69 dB（640），4 cascade D3D/VK 36.93 dB。HDRP static/dynamic mixed cached directional 与 fixed-precision 已完成；transparent/SSS/cloud 仍阻塞；area EVSM 片元侧已按 HDRP `EvalShadow_AreaDepth` 使用 `blurPassesScale = 1 + min(4, blurPasses) * 4` 收缩 UV 边界并令 EVSM `normalBias=0`，非 EVSM area 不变。advanced material 方向光/点光 PCSS/EVSM/VSM/MSM 已接（directional MSM 用 stage 6 moment atlas，其余 stage 6/9 raw）；advanced forward IMS 已启用（stage 12 SAT）；advanced deferred（lit-variant off）IMS 也已启用，专用 pass 在 stage 6 采样 SAT、stage 12 保留 GBuffer4；advanced lit-variant（raster + compute tile）IMS 也已启用，standard variants 保持 stage 12、advanced variants 走 stage 6 SAT + stage 12 GBuffer4，D3D11 advanced tile 因 FXC 回退 raster。详见 [docs/m12.2-checklist.md](docs/m12.2-checklist.md)。
- M12.3 材质/GBuffer/Decal（进行中）：A03 v1 已完成——DBuffer 3RT→4RT RGBA8，albedo/normal/metallic/AO/roughness 独立 coverage，`channelMask` per-channel 与 `layerMask` receiver 过滤；GBuffer 增加 RGBA32F advanced 第 5 MRT。A02 deferred/forward/area advanced lighting v1 已完成；`ForwardAdvanced` 按 no-map/map、single/double-sided 选择变体；area rectangle/tube 增加 iridescence/clearcoat/transmission/SSS LTC 项。HDRP 29 Lit variants v1 已接：`--lit-variants` 使用 per-pixel stencil 分派；`--lit-variants-tile` 的 per-tile material flags + indirect compute 路径支持 SSAO 与 forward-only opaque（Vulkan stdscene 100.51 dB、advscene 94.49 dB、advall 100.51 dB/max1；D3D11 stdscene 100.51 dB/max1；feature-only 0 diff；D3D 58 raster + 29 compute dxbc 已完成）。D3D11 advanced 材质因 FXC no-opt compute 性能回退 raster；标准场景 1080p tile 107–125 fps、SSAO 139 fps。详见 [docs/m12.3-checklist.md](docs/m12.3-checklist.md)。
- M12.5 A09 depth pyramid v1 已完成：`R32F` min-only compute（11 mips，mip0 copy + 逐 mip 2×2 min），D3D11/Vulkan `m24_ao_boxes` deferred+SSAO 与 multi_skin depth debug 均 **0 diff**；render graph 41→31 pass，R32F 1280×720 约 4.7 MiB；1080p m28 100 帧 compute **109.5** vs raster **107.3 fps**，小场景 m24 为 **133.8 vs 148.6 fps**；`hbg_tests` 280/32,915。逐 mip dispatch，不是 HDRP packed atlas；A07 v1 见下条。
- M12.5 A07 v2 + C05 bindless draw-list v1 已完成：opaque forward + deferred GBuffer 的 GPU frustum + max Hi-Z culling；独立 R32F max mip chain（720p 约 4.7 MiB、1080p 约 10.5 MiB；双缓冲 previous-frame/current history 共约 9.4/21 MiB）。depth prepass 参考 HDRP `HDRenderPipeline.Prepass.cs:217-389` 的 `OcclusionTest.TestAll` / `TestCulled`：phase 1 用 previous-frame final max cull，phase 2 用 current intermediate max 仅 retest phase 1 culled items，最后重建 final max 供 forward/GBuffer culling；首帧 history 不可用时 CPU prepass。透明 v1 继续 CPU back-to-front 排序、每 item 独立 cull/indirect record；graph/advanced/light-layer 回退 CPU。C05 v1（受限解冻）：`BindlessForwardInstanced` + 固定 32 材质 uniform array（12 Vec4/材质，含 per-material variant flags、UV scale/offset、wrap/heterogeneous 标记）+ 真实材质 texture array（同通道同格式/mipCount/采样器/UV0 约束，BC1/3/5/7 + 完整 mip 链，缺失层用白色/法线 neutral fallback；未压缩/压缩单 mip 异构尺寸 v1 使用 padded array + border/UV scale/offset，未压缩 border 为 1 texel、压缩 border 为 1 个 4×4 block；未压缩/压缩多 mip 异构使用 max mip count + 无 border padded array + 采样梯度）+ 全局静态 geometry；GPU per-item compact 出 `firstIndex/baseVertex/firstInstance`，D3D11 逐 record `submitIndirect` 并保留 instance/vertex/index 状态，Vulkan `submitIndirectCount` 一次消费 GPU count；仅 forward opaque 标准 Lit、lightmap/advanced/graph/light-layer/transparent/动态 mesh 回退。`hiz_boxes`、`m28`、真实纹理 `sphere_textured`/`m28_textured_one` D3D/VK 各自 **0 diff**；`m24` deferred+SSAO、`m125_blend_boxes` forward、textured fallback、60 帧 orbit、0.5 scale 均 **0 diff**。1080p 300 帧 m28 baseline/GPU D3D11 **232.5/157.6 fps**、Vulkan **156.5/140.5 fps**（当前环境；bindless 仍受两阶段 prepass/max 链成本影响）。`hbg_tests` 当前工作区 **325/33,719**（含既有未提交测试）。真实材质纹理数组 v1 已接（同格式/mipCount/采样器/UV0；RGBA8/RGBA32F 与 BC1/BC3/BC5/BC7 + 完整 mip 链；未压缩/压缩单/多 mip 异构尺寸 v1 已接（多 mip 用 max mip count + 采样梯度）），meshlet/descriptor indexing/VRS/D3D12 仍开放。
- M12.5 C05 透明 GPU 排序 v1 已完成：参考 HDRP `InstanceCuller.cs:67` 的 `transparentInstanceID`、`InstanceCullingBatcherBurst.cs:206` 的 `HasSortingPosition` 与 `InstanceCuller.cs:1376-1393` 的 sorting position；标准 Lit Blend、非 double-sided、无 lightmap/advanced/graph/light-layer 的透明整组启用。`gpu_driven_cull.cs.sc` 写 compute-readable `drawRecords` mirror（bgfx indirect buffer 只有 UAV，不能直接读）；新增 `gpu_driven_transparent_sort.cs.sc` 按 sortingLayer/renderQueue/rendererPriority/sortingOrder/distance 做 stable rank，输出 `sortedVisible`/`sortedIndirect`。D3D11 逐 sorted record `submitIndirect`，Vulkan `submitIndirectCount` 一次 multi-draw。`m125_blend_boxes` 与自建 `m125_sort3`（5 transparent/4 materials）D3D11/Vulkan 各自 **0 diff**，跨后端 2 px/max1；1080p 300 帧 `m125_sort3` forward D3D11 CPU/GPU **141.1/163.3 fps**、Vulkan **113.7/105.3 fps**；`hbg_tests` **295/33,236**。限制：O(N²) rank sort、透明 backface v1 见后续、无 distortion、异构尺寸纹理/descriptor indexing/VRS 未接。
- M12.5 透明 Motion Vectors v1 已完成：参考 HDRP `FrameSettings.cs:209-210` 的 `TransparentsWriteMotionVector`、`HDCamera.cs:1702-1705` 的 `_TransparentCameraOnlyMotionVectors` 与 `HDRenderPipeline.RenderGraph.cs:1330-1425`；新增 `TransparentMotionVectors` pass 与 `transparent_motion_vectors.{vs,fs}.sc`，在 transparent color pass 后按 back-to-front 写 scene motion target（depth LEQUAL/read-only、无 depth write，base color alpha `<0.001` discard），`TransparentsWriteMotionVector` 控制 object/camera-only 语义。`m125_blend_animated` object mode 非黑 **12,532 px**、camera mode **0**，D3D11/Vulkan **6 px/max1**，forward/deferred 一致；alpha=0 场景 **0 px**；`hbg_tests` **295/33,236**。限制：每个像素只保留最后（最近）一层透明 MV、无逐层透明 MV；蒙皮/变形 camera-only 仍保留 previous position。
- M12.5 透明 pre-color depth prepass v2 已完成：参考 HDRP `HDRenderPipeline.RenderGraph.cs:1740-1764` 的 `RenderTransparentPrepass` 在 transparent color 之前执行、`:1446-1476` 的 `RenderTransparentDepthPrepass` 与 `Lit.shader:775-810` 的 `ZWrite On`；把 v1 的 depth pass 从 transparent color 之后前移到 transparent color 之前（opaque/VFX 之后），仍使用 `m_transparentDepthPrepassDrawList`、`TransparentMotionVectors` program 和 `prepassDepthState` 写 main depth，transparent color 现在按该 depth 做 LEQUAL，透明 MV 继续复用同一 depth。`m125_blend_boxes` forward/deferred on/off D3D11/Vulkan 各 **2,968 px/max17**，D3D/VK on 交叉 **2 px/max1**；`m125_blend_offset_boxes` 同 **2,968 px/max17**、交叉 **2 px/max1**；depth debug on/off **3,630 px/max6** 且 D3D/VK **0 diff**；`m125_blend_animated` MV on/off **2,908 px/max17**、D3D/VK **0 diff**；alpha=0 depth **0 diff**；backface+prepass on/off **3,398 px/max36**、D3D/VK **2 px/max1**；`hbg_tests` **300/33,301**。限制（当时）：未实现 HDRP pre-refraction/refraction 双队列与 depth copy；无 distortion/逐层 MV；与 Transparent Backface 同开时 prepass 前表面深度会遮挡 backface；O(N) 额外透明 draw。后续 M12.6 B09 v1 已补 pre-refraction/refraction split + depth copy（additive composite）。
- M12.5 透明 Transparent Backface v1 已完成：参考 HDRP `HDRenderPipeline.cs:181-184` 的 AllTransparentPassNames 顺序（Backface 在 Forward 之前）、`HDRenderPipeline.RenderGraph.cs:964-965` 的 supportTransparentBackface gate 与 `Lit.shader:834-850` 的 `TransparentBackface`（Blend、Cull Front、ZTest）；新增 `asset::Material::transparentBackface` 与 sidecar `materials.<name>.transparentBackface`，仅 Blend、非 double-sided 且显式启用的材质进入 `m_transparentBackfaceDrawList`。forward/deferred 的 transparent color 内先按 back-to-front 绘制 backface list（`CULL_CCW`、alpha blend、无 depth write），再绘制 front list；新增 `--transparent-backface/--no-transparent-backface`、`supportTransparentBackface`，默认关闭。为避免 backface 与 bindless transparent sort 的顺序冲突，backface active 时 transparent bindless/sort 回退到 GPU per-item cull + CPU 顺序；double-sided/graph 材质不进入该 pass。验证：`m125_blend_boxes` 加 sidecar 后，功能 off 与 no-sidecar **0 diff**；on/off forward/deferred **3,398 px/max28**，D3D/VK **2 px/max1**；`m125_sort3` 无 material flag 时 on/off **0 diff**；GPU/CPU、Vulkan CPU/GPU on **0 diff**；`hbg_tests` **295/33,236**。限制：不是 HDRP 的逐材质 renderer-list 顺序（v1 为 group backfaces 后 fronts）、double-sided 不支持、无 distortion/低分辨率透明路径。
- M12.6 B05 TAAU v2（实验）：参考 HDRP `GlobalDynamicResolutionSettings.cs:109` 的 `TAAUInjectionPoint=BeforePost`、`HDRenderPipeline.PostProcess.cs:873-937` 的 exposure 后/DOF 前 upscale 顺序、`:3734-3736` 的 DOF 紧跟 TAA，以及 Core `DynamicResolutionHandler.cs:215-236` 的 `UpsamplerScheduleType`、`HDRenderPipeline.PostProcess.cs:873/909/968` 的 BeforePost/AfterDepthOfField/AfterPost 分支，以及 `:2243-2431/2434-2530` 的 TAAU history viewport/pass、`HDCamera.cs:1301-1322/1459-1462` 的 history reset/target-size reference、`TemporalAntiAliasing.shader:208-236/262-326` 的 `outputPixInInput`/central filter/`GetUpsampledConfidence`，以及 `TemporalAntialiasing.hlsl` 的 `GatherNeighbourhood`/`VarianceNeighbourhood`/`GetSampleWeight`/`DirectClipToAABB`/`HistoryBicubic5Tap`（含 `ANTI_RINGING`，`TemporalAntialiasing.hlsl:330-399`）、`:217-298` 的 `GetClosestFragmentOffset`/`GetMotionVector`/`ModifyBlendWithMotionVectorRejection`，以及 `TemporalAntiAliasing.shader:164-170/319-323/349-350` 的 R16F velocity magnitude history MRT（`HDRenderPipeline.PostProcess.cs:2154-2157/2480-2510`）；`TaaSettings::upscale` + `--taa-upscale/--no-taa-upscale`、`TaaSettings::upscaleInjection` + `--taa-upscale-injection=before-post|after-dof|after-post`（默认 before-post）、`TaaSettings::velocityRejection` + `--taa-velocity-rejection`（默认 0，HDRP 默认），history/输出使用 target resolution；BeforePost 在 exposure 后/DOF 前 upscale，DOF/MB/Bloom 消费 target-res color；AfterDepthOfField 在 DOF 后 upscale，DOF 消费 render-res color、MB/Bloom 消费 target-res color；AfterPost 在 Uber 后 upscale，DOF/MB/Bloom/Uber 消费 render-res color，结果需 final copy，且缺少 Uber 时回退 BeforePost；场景 motion/depth 保持 render resolution；TAAU 分支做 render-pixel jitter 对齐、YCoCg/perceptual 8 邻域 UPSAMPLE filter、variance AABB + direct clip、MV-dependent blend 与 upsample confidence；history 用 HDRP 5-tap bicubic 重采样（history sharpening 0.35，对齐 HDRP 默认）+ anti-ringing clamp；TAAU 使用 depth-dilated MV 和 HDRP velocity-history rejection（阈值 0.015935382、`250 * r³`），移除 legacy per-pixel motion weight；normal TAA 仍用 `motionVectorRejection`；scale-only dynamic resolution 变化保留 target-res TAA history，非 TAAU temporal 仍 reset。验证：`m125_prerefraction` 1280×720、render scale 0.5、16 帧，TAAU history **1280×720**；static 相对 1.0×TAA 参考 PSNR **43.56 dB**（v1 **41.26 dB**），D3D11/Vulkan **481 px/max1**；16 帧 orbit 默认 velocityRejection=0 **40.87 dB**（v1 **41.46 dB**），D3D/VK **4,657 px/max26**，opt-in 0.5 为 **40.77 dB / 4,392 px/max23**；quality profile D3D/VK bloom+Uber+TAAU 正常（跨后端 **28 px/max1**），BeforePost 的 TAAU+DOF+MB+bloom 为 **26 px/max1**；schedule 验证：AfterDOF 在无 DOF 时 16 帧 static 与 BeforePost **逐字节相同**，AfterDOF 全后处理 DOF **320×180**、Bloom **640×360**、TAA history **1280×720**（D3D/VK 4 帧 static **81.57 dB / 1,227 px/max2**）；AfterPost 全后处理 DOF/Bloom **320×180**、TAA history **1280×720**（D3D/VK **76.80 dB / 3,453 px/max2**），1080p 正常；AfterPost 无 Uber 时日志回退 BeforePost，16 帧 static 与 BeforePost **逐字节相同**；dynamic resolution target 1 fps 60 帧与 TAAU 同开可调整 scale，scale=1 回退 `upscale=false`；scale-only dynamic 20 帧 D3D/VK 4 次调整 TAAU history 保持 target-res（`taa_frames=20`）；`hbg_tests` **321/33,638**。限制：BeforePost 的 DOF/MB/Bloom 在 target-res 运行，成本随 upscale 增大；AfterPost 需要 Uber 且 TAA 在 LDR 上运行，full-post orbit 跨后端差异仍较大；velocity rejection 默认关闭；static/orbit TAAU-only 跨后端仍非 0 diff（481/4,657 px）；orbit PSNR 仍低于 v1（40.87 vs 41.46）；dynamic scale-only 变化保留 target-res TAAU history（SSAO/SSR/SSGI/fog 仍 reset）；无硬件 DRS/upscaler 质量切换。
- M12.6 B09 透明 pre-refraction/refraction 队列 + depth copy v1 已完成：参考 HDRP `HDRenderPipeline.RenderGraph.cs:1740-1823` 的 `RenderTransparentPrepass`、`:781-807` 的 `CopyDepth`、`:928-946` 的 `GetTransparentRenderQueueRange`、`:1869-1919` 的 pre/refraction color + combine，以及 `HDRenderPipeline.cs:181-184` / `Lit.shader:775-810`。新增 opt-in `PipelineSettings::supportTransparentRefraction` 与 `--transparent-refraction/--no-transparent-refraction`；`buildDrawLists` 按 `kRenderQueueRangePreRefraction` 与 `kRenderQueueRangeTransparentWithLowRes` 拆 `m_transparentPreRefractionDrawList`/`m_transparentRefractionDrawList`，并拆对应 depth prepass list。pre-refraction depth prepass 后复用 `ShadowCopyDepth`（full-screen `gl_FragDepth`）把 main depth 复制到 `forward.preRefractionDepth`，refraction depth prepass 继续写 main depth；refraction color 先以 main depth 画到 `resources.color`，pre-refraction color 以 copied depth 画到独立 RGBA16F target，随后用 `BakedGIForwardComposite` fullscreen program 以 additive blend 叠加到 scene color。GPU-driven/bindless transparent 在 split active 时回退 CPU，Transparent Backface active 时禁用 split；未实现 HDRP 的 before-refraction RGB/alpha target、全 alpha-correct combine、refraction shader/water sampling、distortion/low-res transparent。验证：自建 `m125_prerefraction`（2 个 alpha 0.9 box，pre-refraction renderQueue 2750 / refraction 3000）forward/deferred off/on D3D11 **98,596 px/max82**；on D3D11/Vulkan **0 diff**，off D3D11/Vulkan **0 diff**；无透明 prepass 时 on D3D11/Vulkan **0 diff** 且 off/on 仍 **98,596 px/max82**；0.5 render-scale on D3D11/Vulkan **4 px/max1**；`m125_rgba8_hetero_mips` baseline 回归 **0 diff**；`hbg_tests` **309/33,443**。
- M12.6 B09 透明 pre-refraction alpha-correct composite v2 已完成：参考 HDRP `Lit.shader:842-843` 的 before-refraction color/alpha target 与 `Blend One OneMinusSrcAlpha`、`UpsampleTransparent.shader:107-126/151-158` 的 combine/`Blend One SrcAlpha`、`HDRenderPipeline.RenderGraph.cs:1565-1587` 的 `CombineTransparents`；pre-refraction pass 改为 premultiplied-alpha 状态（RGB `SrcAlpha/InvSrcAlpha`，alpha `One/InvSrcAlpha`），fullscreen composite 输出 coverage alpha，并新增 `u_bakedGICompositeMode` 区分 baked GI（0/additive）与 pre-refraction（1）。验证：`m125_prerefraction` v2 on/off **0 diff**（v1 98,596 px/max82），v1→v2 **23.79 dB / 98,596 px/max82**；v2 on D3D11/Vulkan **0 diff**，forward/deferred on/off **0 diff**，0.5 scale on D3D/VK **0 diff**（on/off **16 px/max1**）；`hbg_tests` **321/33,638**。限制：仍为 CPU pre/refraction 两条队列，未做 per-pixel refractive sorting/refraction shader/water sampling/distortion。
- M12.6 B09 HDRP refraction sampling v1 已完成：参考 HDRP `Lit.shader:108-109/1304-1310` 的 refraction model/IOR、Core `Refraction.hlsl:51-75` 的 `RefractionModelBox`、`HDRenderPipeline.RenderGraph.cs:1400/1898-1902` 的 refraction color、`ScreenSpaceTracing.hlsl:52` 的 proxy raycast；在 refraction 队列前把 opaque+pre-refraction color 复制到 `forward.refractionSourceColor`，pre-refraction source 单独合成，主流仍保持 refraction 后 composite；transparent forward shader 用 `u_material_advanced0.z/w`、`u_material_advanced1.z` 和 `features.transmission` 做 planar/box 屏幕空间采样（复用 transparent 不用的 contact shadow stage 11）。验证：`m125_refract_alpha0`（pre-refraction alpha 0，仅保留 split 队列）D3D11/Vulkan、forward/deferred sampling on/off **28.90 dB / 161,604 px/max47**，跨后端 **2 px/max1**；0.5 scale on/off **28.94 dB / 161,604 px/max47**、跨后端 0；`m125_prerefraction` split on/off D3D/VK forward/deferred **0 diff**；`hbg_tests` **321/33,638**。限制：full-res source copy，无 HDRP color pyramid/proxy raycast/depth rejection/rough refraction、water sampling、distortion、per-pixel refractive sorting；仅 feature transmission + alpha blend，pre-refraction 与 main 共用 RGBA16F 路径。
- M12.6 B09 HDRP refraction sampling v2（depth rejection）已完成：参考 HDRP `Lit.hlsl:1867/1876` 的 `refractionOffsetMultiplier` 与 `hitLinearDepth > posInput.linearDepth` 深度拒绝语义；refraction source 的 full-screen copy 现在一次合并 opaque+pre-refraction RGB，并把 pre-refraction depth 写入 RGBA16F alpha（移除独立 source composite pass），refraction shader 用 source alpha 重建 offset 线性深度，若 offset texel 在折射表面前方则回退 center texel。新增 `PipelineSettings::supportRefractionDepthRejection` 与 `--refraction-depth-rejection/--no-refraction-depth-rejection`（默认 true），source depth 不存在时保持 v1 行为。验证：`m125_refract_front2_0` D3D11/Vulkan、forward/deferred on/off **33.45 dB / 39,894 px/max175**，跨后端/forward-deferred **0 diff**；depth rejection on vs off **45.78 dB / 11,252 px/max20**，no-reject on/off **32.85 dB / 39,894 px/max175**；0.5 scale on/off **33.13 dB / 42,744 px/max175**、跨后端 0；`m125_refract_alpha0` 保持 **28.90 dB / 161,604 px/max47**、跨后端 **2 px/max1**；`m125_prerefraction` split on/off **0 diff**；`hbg_tests` **323/33,687**。限制：仍为 full-res source，无 HDRP color pyramid/proxy raycast/rough refraction/water sampling、distortion、per-pixel refractive sorting；pre-refraction 与 main 共用 RGBA16F 路径。
- M12.6 B09 rough refraction v1 已完成：参考 HDRP `Lit.hlsl:1313` 的 `transparentSSMipLevel = pow(iblPerceptualRoughness, 1.3) * (lodCount - 1)`、`:1867/1876` 的 offset multiplier/depth rejection，以及 `ColorPyramidPS.hlsl:37-58` 的五点 Gaussian 权重；`supportRefractionRoughness` + `--refraction-roughness/--no-refraction-roughness`（默认 false）。在 source 全分辨率采样上以 perceptual roughness 选择屏幕空间 Gaussian blur 半径（HDRP ColorPyramidPS 权重），并用 roughness-derived mip 近似降低偏移；depth rejection 仍使用 source alpha 的 mip0 深度。验证：`m125_refract_rough`（RefractionBox roughness 1.0、PreRefractionBox 0.1）D3D11/Vulkan、forward/deferred on/off **64.07 dB / 6,564 px/max24**，跨后端/forward-deferred **0 diff**；0.5 scale **63.19 dB / 4,969 px/max23**；1440p **57.94 dB / 28,086 px/max22**；4K **56.44 dB / 47,717 px/max22**；rough off 与 v2 **0 diff**；`m125_refract_alpha0`/`m125_prerefraction` split on/off **0 diff**；`hbg_tests` **325/33,719**。限制：v1 是 screen-space 5-tap 近似，不生成真实 HDRP color-pyramid mip chain；无 proxy raycast/water/distortion/per-pixel refractive sorting。
- M12.6 B11 frame-time dynamic resolution v1 已完成：参考 Core `DynamicResolutionHandler.cs:433-452` 的 min/max scale lerp 与 `:538-578` 的 scale/size clamp 语义；新增 `DynamicResolutionController`，按固定 evaluation interval 统计实际帧时间，在 `minScale..maxScale` 间以 step 调整 render scale，并用 dead band 防止抖动；交互循环与 render-test 均接入，scale 变化走 `ForwardRenderer::setRenderScale`；TAAU scale-only 变化保留 TAA/exposure history，SSAO/SSR/SSGI/volumetric fog history 仍 reset，其它模式 reset 全部 history。新增 `--dynamic-resolution`、`--dynamic-resolution-target-fps`、`--dynamic-resolution-min-scale`、`--dynamic-resolution-max-scale`、`--dynamic-resolution-step`、`--dynamic-resolution-interval`；默认关闭，`--render-scale` 作为初始 scale。验证：`m125_prerefraction` D3D11/Vulkan 各 80–90 帧，target 1 fps 时 scale 0.5→0.70、target 1000 fps 时 1.0→0.80，日志有逐次 scale/average frame ms；`hbg_tests` **315/33,480**。限制：反馈基于 wall frame time，不是 GPU timer；TAAU 后续 B05 v2 已接（可选 --taa-upscale）；TAAU 下 scale-only 变更保留 target-res TAA history，其余 temporal history 仍 reset；未做硬件 DRS/upscaler 质量切换。
- M12.5 C05 压缩/mip 材质纹理数组 v1 已完成：参考 HDRP `TextureCache2D.cs:28-66` 的同尺寸/同格式分组语义，`MaterialSystem` 的 bindless `Texture2DArray` 现支持 RGBA8/RGBA32F 与 BC1/BC3/BC5/BC7，并保留完整 mip 链（bgfx layer-major mip 上传）；同通道仍要求同格式/mipCount/采样器/UV0，压缩通道缺失源时回退，未压缩缺失层继续 white/normal neutral fallback。新增 `material_texture_array` 布局 helper 与 BC1/BC7 layer-byte 单测；`m125_bc7`（128×128 BC7 8 mips）D3D11/Vulkan baseline/GPU-driven 各自 **0 diff**，`m28_textured_one` 回归 **0 diff**；`hbg_tests` **300/33,301**。限制：异构尺寸 v1 仅支持未压缩单 mip；无 descriptor indexing/mesh shader/VRS/D3D12。
- M12.5 C05 异构尺寸材质纹理数组 v1 已完成：参考 HDRP `TextureCache2D.cs:28-66` 的同尺寸/同格式 cache 分组约束，v1 不创建多 cache，而是为未压缩单 mip 通道建立 max-size padded `Texture2DArray`；每个源图放在 1-texel border 内，padding 按源 sampler 的 repeat/clamp/mirror 生成，shader 用 per-channel UV scale/offset 映射回源区域。`u_bindlessMaterialData` 扩到 12 Vec4/材质（scale/offset/wrap/heterogeneous），支持 baseColor/metallicRoughness/normal/occlusion/emissive 任一通道异构；array 尺寸取 `nextPow2(max+2)` 以减少 UV 精度误差。验证：自建 `m125_hetero`（64×64 + 128×128 baseColor，相同 repeat sampler）D3D11/Vulkan baseline/GPU-driven 各 1 px/max1；显式 clamp 场景 0 diff，UV 0..2.5 repeat 场景 0 diff，normal map 64/128 异构场景 D3D11/Vulkan 0 diff；`m28_textured_one`、`m125_bc7`、`m125_blend_boxes`、`m125_hiz_boxes` D3D11/Vulkan 回归 0 diff；`hbg_tests` **300/33,301**。限制（当时）：压缩格式异构、异构 mip 链仍回退；array 内存按 nextPow2 padding；多 cache/descriptor indexing 未做。
- M12.5 C05 压缩异构尺寸材质纹理数组 v1 已完成：参考 HDRP `TextureCache2D.cs:28-66` 的同尺寸/同格式 cache 分组约束；在未压缩 1-texel border 基础上，压缩单 mip 通道使用 **一个 4×4 block border**（每边 4 texel），块对齐复制源 BC1/BC3/BC5/BC7 数据并复制边界 block，array 尺寸取 `nextPow2(max + 2*blockExtent)`，shader 仍用 per-channel UV scale/offset 映射；`material_texture_array` 新增 block extent/bytes 与 heterogeneous layout helper，单测覆盖 RGBA8/BC1/BC7 的 border/尺寸。验证：自建 `m125_bc7_hetero`（64×64 + 128×128 BC7 单 mip）D3D11/Vulkan baseline/GPU-driven 各 **1 px/max1**，GPU log `bindless=true`；`m125_bc7` 128×128 BC7 8 mips baseline/GPU **0 diff**；`m125_hetero` 未压缩 64/128 回归 **1 px/max1**；`hbg_tests` **302/33,334**。限制（当时）：压缩异构源需宽高 4 对齐；多 mip 异构当时仍回退；压缩通道缺源仍回退；padding 显存开销；无 descriptor indexing/mesh shader/VRS/D3D12。
- M12.5 C05 未压缩多 mip 异构尺寸材质纹理数组 v1 已完成：`TextureLoader` 对非压缩 bimg 图像使用 `imageConvert(..., true)` 保留完整 mip 链（KTX1/KTX2 单测均覆盖 4 级 mip），材质数组按 max mip count 逐级复制/填充；参考 HDRP `TextureCache2D.cs:55-95` 的 `useMipMap` 与全 mip 链 `CopyTexture` 语义。验证：自建 `m125_rgba8_hetero_mips`（64×64 + 128×128 RGBA8，7/8 mips）D3D11 baseline/GPU **0 diff**，Vulkan **780 px/max2**；`m125_bc1_hetero_mips` 与单 mip 异构回归仍通过；`hbg_tests` **309/33,443**。限制：非压缩 loader 只保留源 mip 链（不生成 mip）；较短链复用最后一级；不支持每层不同 mipCount 的精确 cache 分组；压缩通道缺源仍回退；padding 显存开销。
- M12.5 C05 压缩多 mip 异构尺寸材质纹理数组 v1 已完成：延续压缩单 mip 的 block-border 方案，多 mip 异构改用 **无 border、max mip count、源层复用最后 mip** 的 padded array；每层按源 mip 的块对齐尺寸复制到 array 对应 mip，余下区域按源 sampler 的 repeat/clamp/mirror 填充。shader 对异构通道改用 `texture2DArrayGrad`，梯度按 `dFdx/dFdy(uv) * UV scale` 传入 array 空间，避免 `fract` 造成的 mip 缝并保持与源纹理一致的 LOD。`material_texture_array` 新增 `LayeredLayout`/`MipOffset` helper 与 RGBA8/BC1/BC7 单测；`hbg_tests` **304/33,369**。验证：自建 `m125_bc1_hetero_mips`（64×64 + 128×128 BC1，7/8 mips）D3D11 baseline/GPU **0 diff**、0.25 render-scale **0 diff**；Vulkan **780 px/max2**、0.25 scale **3,421 px/max2**、D3D/VK GPU 交叉 **785 px/max2**；`m125_bc7_hetero` 与 `m125_hetero` 仍 **1 px/max1**，`m125_bc7` 8 mips **0 diff**。限制（当时）：多 mip 异构仅接压缩 KTX 路径（非压缩 loader 当时只保留 mip0）；较短 mip 链复用最后一级；不支持每层不同 mipCount 的精确 cache 分组；压缩通道缺源仍回退；padding 显存开销。
- GPU shadow atlas packing、async compute 仍是后续工作；默认 shadow 仍为 64 requests/2048 atlas。
- 方向光目前只支持 1 个主方向光。
- 项目尚未添加 `LICENSE` 文件；使用或分发前请先确认授权。
