# hdrp-bgfx M12 边界台账与偿还计划

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M12.0 边界台账首版完成**（2026-11-17）；M12.0 的构建/CI/基线收口仍在进行。
> 前置：M0–M11 主体已完成；M11.5 核心矩阵已跑通，M11.0 全量 clean O3 / fixtures / perf baseline 仍在收口。
> 原则：先读 HDRP / Core / bgfx 源码再实现；文档只做索引，冲突时以源码为准；不把平台不可行项写成 TODO；每个边界必须有 ID、关闭条件、验证矩阵和目标里程碑。

---

## 0. 为什么要有这份台账

M1–M11 的每份 checklist 都有“简化边界/明确不做/已知限制”，但这些信息有三个问题：

1. **分散**：同一个边界可能出现在 M1、M2、M5、M10 多份文档里，读的时候不知道现在还算不算数。
2. **会过期**：M1 写的“LightList 是 CPU 16×16 tile”已被 M6/M7 的 GPU FPTL/cluster 取代；M2 写的“SSGI debug 留到 M2.9”已在 M2.6 实现。
3. **没有关闭条件**：很多条目只写“不做”，没写“做到什么程度算关闭、怎么验证、依赖什么”。

M12.0 的目标是把 M3–M11 的剩余边界收敛成一份**可执行的台账**：

- 每条边界只出现一次，有稳定 ID。
- 每条的现状、HDRP 参考、分类、关闭条件、依赖、目标里程碑、验证方式都写清楚。
- 已关闭的进入 D 类，只做文档回填，不再重复实现。
- 平台不可行的进入 C 类并冻结，不占 M12/M13 的 TODO。
- 后续每个 M12.x 任务只允许关闭 1–2 个 A 类边界，避免“一个里程碑同时改 GBuffer、材质、阴影、后处理”导致无法定位回归。

---

## 1. 分类规则

| 分类 | 含义 | 处理方式 |
|---|---|---|
| **A 架构型** | 改数据契约、资源布局、采样器预算、资产模型或新增底层能力；会污染后续功能与金图基线 | 提前做，独立任务；一旦新功能依赖它，先做它，不并行堆功能 |
| **B 局部增强型** | 单点 shader/CPU 数学或局部效果；不改变主数据契约 | 批量偿还，放进对应里程碑的“边界偿还窗口” |
| **C 平台/数据模型不可行** | bgfx/API 抽象不提供，或依赖 Unity 编辑器/烘焙/资产系统 | 冻结为 out-of-scope；文档保留原因，不写 TODO |
| **D 已关闭** | 已由后续里程碑关闭，但旧文档仍写着“不做” | 只回填旧文档/本台账，不再实现 |

判定规则：

- 边界是否改变 **GBuffer/DBuffer/LightData/ShadowContext/LightList/Probe/History** 的布局？是 → A。
- 边界是否只改一个 shader 的采样/滤波/权重？是 → B。
- 边界是否需要 **DXR / async compute queue / bindless / mesh shader / HDR swapchain / Unity 烘焙数据**？是 → C。
- 边界是否已在后续里程碑的验收记录里出现 `[x]`？是 → D，并核对旧文档是否过期。

---

## 2. 当前基线状态（2026-11-17）

### 2.1 已通过

- Debug 构建：518 shader variants，0 failed；M12.1 新增 `ssaoTemporal`/`ssrTemporal`/`ssgiTemporal`/`debugSsgiRaw` 后全量 O3 518/518、0 zero-byte。
- `ctest` 2/2、`py -3 tools/idl_gen.py --check`、D3D11/Vulkan `--gfx-test` 通过。
- M11.5 核心矩阵（M10 area 36 组、area shadow、M9 64/transparent、cached/mixed、variant、M8.7 base32 复跑）已在 `m11-checklist.md` 记录。
- D3D11 forward cap64 1080p 300 帧复测中位数 **67.0 fps**，过 60 门禁。

### 2.2 未收口

- **M11.0 全量 clean O3**：M12.1 已完成增量重建：518 variants/0 failed/0 zero-byte；forward 单变体约 **600–1000 秒**，全量仍不适合在 600 秒 shell timeout 内一次跑完，必须分批/增量。非 forward 旧二进制通过时间戳缓存复用，未做 clean 全量重编。
- **M11.0 fixtures/perf baseline**：尚未有仓库内脚本；M10 area 36、area16 flags、M9 64/transparent、M8.7 base32 仍靠手工命令。
- **1080p 性能复测存在波动**：D3D11 deferred cap64 中位数 **59.6 fps**（59.6/62.3/57.9），擦线；Vulkan forward/deferred **51.3/50.8 fps**，明显低于 M11.4 记录的 64.6/63.2，需要排除后台负载、未提交 M11.1c cookie/probe 改动和驱动状态。
- **未提交改动**：工作区包含 M11.1c cookie atlas / probe array / area context 等改动；在 M11.0 验证通过前不应作为 M12 基线。

### 2.3 结论

M12 的第一个任务不是新功能，而是 **M12.0：把构建、fixtures、perf 基线冻结住**；否则 M12.1 之后的优化无法判断是进步还是基线漂移。

---

## 3. 边界总表

> 状态：`open` 未开始；`in-progress` 进行中；`closed` 已关闭；`frozen` 冻结；`verify` 需要复测/回填。
> 目标里程碑是建议值，实施时以依赖和性能门禁为准。

### 3.1 A 类：架构型

| ID | 边界 | 现状 | 关闭条件 | 依赖 | 目标 | 状态 |
|---|---|---|---|---|---|---|
| **A01** | Temporal SSAO/SSR/SSGI：history、reprojection、depth/normal rejection、temporal denoise | SSAO/SSR/SSGI temporal 与 raw/history/denoised debug 均已落地；真实窗口 resize、DebugUI render scale、setter toggle reset 已验证；修正 SSR/SSGI temporal 取样 Y 翻转，orbit ghost 消失；M11.5 64-light 1080p full-post 3 次 median：D3D11 off/SSAO/SSR/SSGI **55.1/51.6/50.8/44.3**，Vulkan **47.0/45.4/47.1/38.8** fps | temporal off 对当前基线 0 diff；temporal on 收敛、无 ghost；显式 resize/scale/开关 reset；1080p >60 fps | M3.4 TAA、M3.6 motion vectors | M12.1 | in-progress（仅性能门禁未过） |
| **A02** | 材质特性 + 29 Lit variants：clear coat / SSS / transmission / anisotropy / iridescence | 材质字段/glTF/sidecar/Uniforms/GBuffer4 打包、deferred/forward/area advanced lighting、29 Lit variants v1 已完成（per-pixel stencil + value 分派）；D3D11 58 raster dxbc + 29 compute dxbc 已完成，tile 路径在 D3D11/Vulkan 均验证（D3D11 advanced 材质因 no-opt compute 性能回退 raster） | 材质模型 + GBuffer 数据 + feature flags 落地；29 variants 真实分派，非 view-wide 同代码变体 | A03、sampler/RT 预算 | M12.3 | done v1 |
| **A03** | GBuffer/DBuffer 布局扩展：4RT DBuffer per-channel mask、decal layers、额外 MRT | 5RT GBuffer advanced、4RT DBuffer、per-channel mask、receiver/decal layer mask v1 已落 | 显式 RT/sampler 预算；`maxFBAttachments`/格式 fallback；M10/M11 基线 off 0 diff | A02、设备 caps | M12.3 | done v1 |
| **A04** | Reflection Probe v2：realtime capture、cubemap atlas、GPU convert/convolve、planar、probe volume/APV、hierarchy/streaming | 最多 4 个静态 equirect probe、CPU 选最近 2 个、stage 14 array | capture/atlas/卷积路径 + fallback；APV 单独立项 | A02?、compute、资产管线 | M13 | open |
| **A05** | Volumetric Fog v2：3D VBuffer/froxel、local VFX/clouds、area/box volumetrics、PBR atmosphere | 2D screen-space raymarch + low-res history；无 3D VBuffer | VBuffer/voxelization + lighting/filtering；local VFX 资产 | compute、资产管线 | M13 | open |
| **A06** | 资产管线 v2：skin/morph/animation、Draco、KTX2/BasisU、压缩纹理、sampler wrap/filter、mask 重打包 | glTF 静态网格/材质/贴图/相机/灯光；skin/morph/animation/压缩纹理未接 | loader + 单测 + 真实场景；压缩纹理双后端加载 | glTF/cgltf、bimg | M12.4 | open |
| **A07** | GPU-driven geometry / Hi-Z：opaque/transparent GPU culling、indirect draw、Hi-Z occlusion、meshlet/mesh shader/bindless/VRS/D3D12 | GPU light-list/shadow culling 已完成；GPU-driven geometry 未做 | D3D11 固定 draw + GPU instanceCount、Vulkan indirectCount；Hi-Z compute；无 validation error | compute、A09、M7 buffer 基础 | M12.5 | open |
| **A08** | FrameSettings per-camera / Volume 栈 / FrameSettingsHistory | 只有全局 Camera FrameSettings；无 Volume 混合、per-camera override、DebugMenu 四列 | per-camera FrameSettings + override mask + Volume 优先级混合 | 编辑器/JSON 资产模型 | M13 | open |
| **A09** | Depth Pyramid compute/atlas | `RG32F` min/max + scratch 栅格路径；不是 HDRP `R32F` min-only compute/atlas | compute 单 pass min pyramid；消费者无需改语义；性能/显存记录 | compute | M12.5 | open |
| **A10** | 构建/CI/回归基线：全量 clean O3、fixtures、perf baseline | shader build 工具 v1；全量 O3 仍慢；无仓库内 fixtures | 可重复完成全量 O3；manifest/zero-byte 校验；M10/M9/M8.7/1080p fixtures 入仓 | 无 | M12.0 | in-progress |

### 3.2 B 类：局部增强型

| ID | 边界 | 现状 | 关闭条件 | 目标 | 状态 |
|---|---|---|---|---|---|
| **B01** | 阴影质量 v2：EVSM/VSM/MSM/IMS、cached directional per-cascade/HDRP mixed、area+point PCSS 共存、transparent/SSS/cloud shadow | P0 banding 已修；area+point PCSS 共存、cached directional per-cascade partial 已完成（v1）；EVSM v1、VSM v1、MSM v1（local raw-depth moments）与 directional IMS v1（RGBA32F moment atlas + SAT，integer blocker search + bilinear filter + light-angle penumbra + per-cascade request 参数）已实现；HDRP 完整 static/dynamic mixed、transparent/SSS/cloud、fixed-precision 已完成；advanced stage 未完成 | 见 `docs/m12.2-checklist.md`；完整 mixed 需 caster static flag；IMS 剩余 advanced stage | M12.2 | in progress |
| **B02** | Cookie atlas v2：native >256、二叉树 allocator、streaming/async upload/defrag、HDR/彩色 cookie、动态 cookie revision | M11.1c 已做 1024/512/2048 RGBA8、2px padding、mip、largest-first shelf | 每项独立；atlas 满不再只白 fallback；动态编辑可失效 | M13 | open |
| **B03** | Cached area shadow v2：defrag/persistent slot、2048 opt-in、EVSM area、screen-space area | M11.2 all-static + mixed static-first persistent slots | defrag 不破坏 0 diff；2048 opt-in；EVSM/screen-space 单独立项 | M13 | open |
| **B04** | Decals v2：4RT per-channel mask、decal layers、VFX/transparent/forward decals、normal buffer patch、cube mesh/scissor/排序 | 3RT DBuffer、全屏 quad、sidecar box decal | 4RT/layers 依赖 A03；其余每项 off 0 diff | M13 | open |
| **B05** | 后处理 v2：physical DOF/bokeh、MB tile/scattering/transparent MV、Bloom anamorphic/dirt/lens flare、SMAA、distortion/CA/film grain/dithering、grading curves/external LUT、TAAU | M3 v1：manual DOF、单 pass MB、Bloom 基础、FXAA、64³ LUT | 每项独立开关 + 双后端 + off 0 diff；TAAU 依赖 dynamic resolution | M12.6 | open |
| **B06** | Sky/IBL v2：cubemap + BSDF LUT、动态天空/clouds/backplate/distortion/flowmap、specular occlusion/DFG LUT、mip bias | equirect 1024×512×7mip + CPU GGX + SH9 | 每项独立；cubemap/BSDF 与现有 equirect 路径可切换 | M13 | open |
| **B07** | Debug/tooling v2：probe debug、GPU cluster 可视化、shadow overlay、motion arrows、pyramid mip 选择、color picker、RenderDoc | 15 个 debug 模式；无 ReflectionProbe debug；RenderDoc/F11 未接 | 每项独立；debug 不改变正常路径 | 持续 | open |
| **B08** | 256 lights 容量 | `kMaxGpuLights=128`、16-bit packing；128 为 opt-in，默认 64 | 128 稳定后研究 256；内存/cluster/性能门禁；不默认 | M12.7 | open |
| **B09** | Transparent prepass/MV/distortion | 透明不写深度、无透明 MV、无 distortion pass | 两遍透明 + MV + distortion；透明 forward 路径 0 diff | M12.6 | open |
| **B10** | MSAA deferred / partial prepass | 仅 full prepass、无 MSAA；D32F 无 stencil | MSAA deferred resolve/partial prepass 单独立项；D32F 保持 fallback | M13 | open |
| **B11** | Dynamic resolution 调度 | 只有 scale API，无帧时反馈 | 帧时反馈 + scale 控制 + history reset；TAA/后处理尺寸正确 | M12.6 | open |
| **B12** | Light layers for env/probe；shadow mask | punctual/directional/透明 light layers 已有；env/probe 未接；shadow mask 无 | env/probe layer 过滤；shadow mask 依赖烘焙（C04） | M13 | open |
| **B13** | Shadow request/atlas packing | HDRP 也是 CPU/Burst culling + dynamic/cached atlas；本项目 CPU request/atlas packing | **不作为 HDRP 对齐项**；只记录性能优化 | — | closed |

### 3.3 C 类：平台/数据模型不可行（冻结）

| ID | 边界 | 原因 | 处理 |
|---|---|---|---|
| **C01** | DXR / RT shadows / RTGI / RTAO / path tracing | bgfx 不暴露 DXR/Vulkan RT；HDRP `useScreenSpaceShadows` 本身是 ray tracing only | 冻结；不写 TODO；需要时换后端/自写平台层 |
| **C02** | Async compute / async light-list | bgfx 单队列；`--async-gpu-light-list` 只能 experimental | 冻结；不计入性能门禁 |
| **C03** | HDR output / swapchain sRGB | bgfx/GLFW 无标准 HDR swapchain 路径 | 冻结 |
| **C04** | Lightmap / shadow mask / APV / Unity Volume / DecalProjector 资产 | 依赖 Unity 编辑器/烘焙/Lightmap 数据；glTF 无对应数据 | 冻结；用 sidecar 表达可表达的部分 |
| **C05** | bindless / descriptor indexing / mesh shader / VRS / D3D12 | bgfx 抽象不提供；D3D11 为当前后端 | 冻结 |
| **C06** | Disc real-time LTC | HDRP jobs 中 disc 亦标注 real-time unsupported | 冻结；sidecar 标 unsupported/近似 |
| **C07** | GPU shadow atlas packing | HDRP 本身没有 GPU atlas packing；本项目 CPU atlas 已对齐语义 | 冻结，不作为差距 |
| **C08** | 无条件 29 Lit variants | 29 variants v1 已接 `--lit-variants`，并新增 `--lit-variants-tile` 的 material flags + indirect compute v1（D3D11/Vulkan 均已验证；tile 现支持 SSAO 与 forward-only opaque，D3D11 advanced 材质因 no-opt compute 性能回退 raster）；默认 `supportLitVariants=false`、`supportLitVariantTiles=false` | 默认不改变 M2–M12 基线；D3D11 advanced tile 保持 raster fallback |

### 3.4 D 类：已关闭项（只做文档回填）

| ID | 旧边界 | 关闭里程碑 | 需要回填的旧文档 |
|---|---|---|---|
| **D01** | per-request shadow cull、alphaMask caster、PCSS、contact shadows、cached punctual、per-light dimmer/fade/tint | M5.0–M5.3 | `m1-checklist.md` 已知限制、`m5-checklist.md` |
| **D02** | CPU LightList 16×16/64×64、GPU light screen AABB/big-tile/FPTL/cluster、GPU shadow culling/indirect | M6、M7 | `m1-checklist.md:214/338/355`、`m2-checklist.md:279` 附近 |
| **D03** | 64/128 capacity、16-bit packed FPTL、layered cluster offset、category/feature flags、light layers、cookies/IES | M8、M9 | `m8-checklist.md`、`m9-checklist.md` 旧问题列表 |
| **D04** | area rectangle/tube、area shadow、LTC、128 shadow requests/4096 atlas | M9.6、M10 | `m10-checklist.md` 开放决策 |
| **D05** | cookie atlas v1/v2、cached area shadow、variant strategy、1080p cap64 >60 fps | M11.1–M11.5 | `m10-checklist.md` 已知限制、`m11-checklist.md` |
| **D06** | histogram exposure、Bloom/ColorGrading/TAA/DOF/MB/FXAA、volumetric fog/local fog、contact shadows | M3、M4、M5 | `m1-checklist.md` 已知限制、`m2-checklist.md` 已知限制 |
| **D07** | M9.7 1080p 性能门禁 | M11.4（当前复测需复核） | `m9-checklist.md:188/414`，`m10-checklist.md:479/492` |
| **D08** | SSGI debug “留到 M2.9” | M2.6 已实现 SSGI debug | `m2-checklist.md:383` |

> 注意：D07 的“关闭”只表示 M11.4 记录过 60fps；本轮 2026-11-17 复测 D3D11 deferred 中位数 59.6、Vulkan 51.3/50.8，需要先完成 M11.0 的安静环境复测，再决定 D07 是否重新打开。
>
> ReflectionProbe debug 不在 D 类：它仍未实现，登记为 B07。

---

## 4. A 类详解

### A01 Temporal SSAO/SSR/SSGI（M12.1）

> 实现清单：`docs/m12.1-checklist.md`（SSAO + SSR + SSGI temporal、raw/history debug、真实 resize/scale reset、SSR/SSGI Y 翻转修复已完成；M11.5 full-post 性能门禁未过，见 3.4）。

**现状**

- SSAO：半分辨率 `ssao -> ssao_blur -> ssao_temporal -> ssao_upsample`，RGBA8 双缓冲 history，`--ssao-temporal` opt-in。
- SSR：全分辨率 `ssr_trace -> ssr_temporal -> ssr_composite`，RGBA16F 双缓冲 history，`--ssr-temporal` opt-in；temporal off 保持 M11.5 路径。
- SSGI：半分辨率 32 rays × 8 steps + 9×9 空间去噪/upsample，`ambientBias=0.5` 部分扣除 ambient。
- TAA 已有 jitter/history/motion reprojection；`HistoryBuffer<T>` 和 resize/scale reset 已存在。

**HDRP 参考**

- SSAO：`Runtime/Lighting/ScreenSpaceLighting/HDRenderPipeline.AmbientOcclusion.cs:147-188/204-250/253-312/410-450`；`GTAO.compute`、`GTAOCommon.hlsl`、`GTAOBlurAndUpsample.compute`。
- SSR：`Runtime/RenderPipeline/HDRenderPipeline.LightLoop.cs:1118-1450`；`ScreenSpaceReflections.compute:139-227/279-460/640-671`；`ScreenSpaceTracing.hlsl:36-...`。
- SSGI：`Runtime/Lighting/ScreenSpaceLighting/ScreenSpaceGlobalIllumination.compute:26-329`；`RayMarching.hlsl:5-139`；`HDRenderPipeline.ScreenSpaceGlobalIllumination.cs:172-313/400-468/470-517`；`RayMarchingFallbackHierarchy.cs:9-30`；`GlobalIllumination.cs:21-151`。

**关闭条件**

1. 每个效果有独立 history（SSAO `R8`/`RGBA8`，SSR/SSGI `RGBA16F`），首帧 `invalid` 时写当前帧，resize/render-scale/开关变化 reset。
2. 重投影使用 TAA 的 `sourceUv - (motion.x, -motion.y)` 语义；depth/normal rejection 防止 ghost；不做 vertex velocity history。
3. temporal off 对 M11.5 当前基线逐像素 0 diff；temporal on 双后端可见收敛，静态 16 帧稳定，orbit 无持续大面积 ghost；SSR v1 无 hit-point motion 的边缘 ghost 记录在 `m12.1-checklist.md`。
4. 1080p 300 帧 full post 不超过当前基线 +10%（先记录，不承诺一次达标）。
5. `--ssao-temporal/--ssr-temporal/--ssgi-temporal` 或 profile 字段 + off 回归；Debug view 仍可看 raw/history。

**非目标**

- 不做 RTGI/RTAO/RT shadows（C01）。
- 不做 SSR 的 color pyramid/Hi-Z 全逻辑；Hi-Z 归 A09/M12.5。
- 不做透明 SSR/透明 SSAO；透明路径保持 M2 边界。
- 不做 TAAU/dynamic resolution 时间放大；B11 单独做。

**验证矩阵**

- 场景：boxes/corner（SSAO/SSGI）、Uffizi smooth metal（SSR）、`m28_blend_view`（probe fallback）。
- 后端：D3D11/Vulkan；模式：forward/deferred；尺度：1.0/0.5。
- 对比：temporal off vs 当前基线、temporal on vs 参考收敛图、history reset 用例。

### A02/A03 材质特性 + GBuffer/DBuffer（M12.3）

**现状**

- 只有 Lit standard 材质；16 个 opaque material mask；normal/mask/alpha clip/double-sided/light layers。
- GBuffer 4RT（RGBA8×3 + RGBA16F）+ D24S8；DBuffer 3RT；没有 SSS/transmission/anisotropy/clearcoat/iridescence 数据通道。
- M11.3 已否决“先做 29 个变体”；材质落地后仍需 HDRP 式 per-tile classification，不能把 29 个同代码 view-wide 变体当完成。

**HDRP 参考**

- `Runtime/Material/Lit/Lit.hlsl:98-170`（29 个 `kFeatureVariantFlags`）、`Lit.shader:477-485/877-889`。
- `Runtime/Lighting/LightLoop/LightLoop.cs.hlsl:63-67`（`NUM_FEATURE_VARIANTS`、material/light feature mask）。
- GBuffer/DBuffer：`HDRenderPipeline.Prepass.cs:721-793/827-855/1321-1341/1356-1447`；`Material/Decal/Decal.cs.hlsl:33-47`；`Decal.shader:116-155`；`DecalNormalBuffer.shader:56-128`。

**关闭条件**

1. 先做材质模型（clear coat/SSS/transmission/anisotropy/iridescence 至少选 1–2 个），再做 GBuffer 通道；不允许先铺 29 个空变体。
2. GBuffer/DBuffer 扩展有显式预算：`maxFBAttachments`、格式、sampler、显存；不满足时 fallback 旧路径并打印一次原因。
3. 新材质 off 时 M11.5 基线 0 diff；on 时双后端截图有差异且无 NaN/黑块。
4. 29 variants 只在对应材质/feature flag 真实存在后生成；变体数量、编译时间、manifest 记录。
5. 29 variants 不能减少 mixed category 的 per-tile 分支；如果仍是 view-wide 变体，保留 runtime tile gating。

**非目标**

- 不做 fabric/eye/hair/AxF（除非先立材质模块）。
- 不做 lightmap/shadow mask/APV（C04）。
- 不做 29 个变体的无条件编译爆炸。

### A04 Reflection Probe v2（M13）

- 现状：sidecar 最多 4 个静态 equirect probe，CPU 选最近 2 个，stage 14 array；无 realtime capture/atlas/planar/APV。
- HDRP：`HDProbe.cs:735-803`、`ProbeSettings.cs:141-175/283-298`、`VolumeProjection.hlsl:8-115`、`ReflectionProbeTextureCache.cs:126-135/243-329/594-636`、`LightEvaluation.hlsl:779-821`、`Lit.hlsl:1912-2007`。
- 关闭条件：realtime capture（cubemap RT + convolve）、cubemap atlas、probe hierarchy/importance/streaming、debug view；每项 off 0 diff。
- 非目标：APV 单独立项；不做 Unity 资产导入。

### A05 Volumetric Fog v2（M13）

- 现状：2D screen-space raymarch + low-res history + local box/sphere + 2D mask；无 3D VBuffer/froxel。
- HDRP：`Fog.cs:17-72/207-273`、`VolumeVoxelization.compute:51-99`、`VolumetricLighting.compute:157-290/574-774`、`AtmosphericScattering.hlsl:241-333`、Core `VolumeRendering.hlsl:128-193/271-304`。
- 关闭条件：VBuffer voxelization + lighting/filtering + opaque/transparent 采样；local VFX/clouds 资产；area/box light volumetrics；PBR atmosphere 单独立项。
- 非目标：不做 froxel 可见性缓冲的完整 HDRP 调度；不做体积云材质。

### A06 资产管线 v2（M12.4）

- 现状：cgltf 静态网格/材质/贴图/相机/`KHR_lights_punctual`；MikkTSpace；sidecar。
- 未做：skin/morph/animation、Draco、`KHR_texture_basisu`/KTX2、压缩纹理、sampler wrap/filter、mask 重打包、Sponza 完整贴图。
- 关闭条件：每项 loader + 单测 + 真实场景；压缩纹理双后端加载；sampler wrap/filter 与 glTF 一致。
- 非目标：不做 Unity 资产导入；不做完整 glTF 扩展宇宙。

### A07/A09 GPU-driven / Hi-Z / Depth Pyramid（M12.5）

- 现状：GPU light-list/cluster/shadow culling/indirect 已完成；GPU-driven opaque/transparent、Hi-Z、meshlet/mesh shader/bindless/VRS/D3D12 未做；depth pyramid 仍是 `RG32F` min/max + scratch 栅格。
- 平台事实：D3D11 `indirectCount=false`，Vulkan `indirectCount=true`（gfx-test caps）；D3D11 需要固定 draw 数 + GPU instanceCount。
- HDRP：`DepthPyramid.compute`、`MipGenerator.cs:62-118`；GPU-driven 参考 HDRP culling/indirect 路径（实施时补 file:line）。
- 关闭条件：D3D11/Vulkan 各自路径；Hi-Z occlusion 有 off 0 diff；depth pyramid compute 语义与现有消费者一致；性能/显存记录。
- 非目标：C05 的 bindless/mesh shader/VRS/D3D12。

### A08 FrameSettings/Volume（M13）

- 现状：全局 Camera FrameSettings；无 per-camera override、FrameSettingsHistory、Volume 混合。
- HDRP：`FrameSettings.cs:107/429/455/508/658/820`、`FrameSettingsDefaults.cs:7/92/149`、Core `VolumeManager.cs`。
- 关闭条件：per-camera FrameSettings + override mask + Volume 优先级混合；JSON/调试面板；sanitize 不变。
- 非目标：完整 Unity Volume 资产/编辑器。

### A10 构建/CI/回归基线（M12.0）

- 现状：shader build 工具 v1；全量 O3 慢；无仓库内 fixtures；perf 复测有波动。
- 关闭条件：
  1. 全量 O3 可重复完成（按输出分组、断点续跑、单变体超时、失败报告），manifest/zero-byte 校验。
  2. `tools/regression.py`（或等价工具）+ fixtures：M10 area 36、area16 flags、M9 64/transparent、M8.7 base32、1080p perf。
  3. 1080p cap64/cap128、D3D11/Vulkan、forward/deferred 的 5–7 次中位数和方差入文档。
  4. M11.1c 未提交改动验证后提交；M11.0/M11.5 状态更新。
- 非目标：不在 CI 里跑 1 小时的全量 O3；CI 用增量 + 关键变体。

---

## 5. M12 里程碑拆分

| 里程碑 | 主题 | 关闭边界 | 出口标准 |
|---|---|---|---|
| **M12.0** | 边界台账 + 构建/CI/基线 | A10、D08；冻结 C 类 | 台账评审通过；全量 O3 可重复；fixtures/perf 入仓；M11.5 基线复核 |
| **M12.1** | Temporal screen-space | A01 | temporal off 0 diff；on 双后端收敛；resize/scale reset；性能记录 |
| **M12.2** | 阴影质量 v2 | B01（+B03 可并入） | EVSM v1、VSM v1、MSM v1 已落地；directional IMS v1（moment atlas + SAT + bilinear filter/light-angle penumbra + per-cascade request 参数）已落地；HDRP 完整 static/dynamic mixed cached directional、transparent/SSS/cloud、fixed-precision 已完成；advanced stage 仍开放；旧 PCF/PCSS 路径 off 不变 |
| **M12.3** | 材质 + GBuffer/DBuffer | A02、A03 | 至少 1–2 个新材质；GBuffer 预算明确；旧基线 off 0 diff；29 variants 有前提 |
| **M12.4** | 资产管线 | A06 | skin/morph/anim/压缩纹理/sampler 至少完成一组；真实场景 |
| **M12.5** | GPU-driven / Hi-Z / Depth Pyramid | A07、A09 | D3D11 固定 draw + GPU instanceCount；Vulkan indirectCount；Hi-Z/depth pyramid 语义一致 |
| **M12.6** | 后处理/透明/动态分辨率 | B05、B09、B11 | 每项独立开关；TAA/后处理尺寸正确；off 0 diff |
| **M12.7** | 256 lights 研究 | B08 | 128 稳定；256 压力场景；不默认 |
| **M13** | Probe/Volumetric/Decal/Sky v2 | A04、A05、B02、B04、B06 | 按子项验收；APV/lightmap 仍冻结 |

顺序理由：

- M12.1 先做：复用 TAA/history，改动集中在屏幕空间 pass，不碰 GBuffer/材质/阴影契约。
- M12.2 与 M12.1 独立，可并行；但同一时间只允许一个主任务，避免双后端矩阵互相污染。
- M12.3 是最大的契约变更，放在 temporal/shadow 稳定之后；否则 GBuffer 一改，所有金图失效。
- M12.4 资产管线可以更早，但它不阻塞渲染边界；按内容需求提前。
- M12.5 是性能/规模化，放在视觉边界之后；依赖 compute 和 A09。
- M12.6/M12.7 作为局部偿还；M13 是大系统（probe/volumetric/decal/sky）。

---

## 6. 每个任务的 DoD

1. **源码**：实现前读 HDRP/Core/bgfx 对应源码，任务记录实际 `file:line`；不以旧文档替代源码。
2. **契约**：改数据布局/采样器/RT 时，先写预算和 fallback；不满足 caps 时打印一次原因并回退。
3. **开关**：新增功能必须有 CLI/FrameSettings/profile 开关；off 时对上一里程碑基线 0 diff。
4. **测试**：CPU reference/单测覆盖数学边界；GPU 路径用双后端截图矩阵。
5. **验收**：D3D11/Vulkan、forward/deferred、buffer/texture/CPU（适用时）、0/1/2/4/8/16 灯或对应场景规模。
6. **性能**：记录 1080p 300 帧中位数、CPU submit、GPU frame、pass/draw；不以单次峰值当结论。
7. **文档**：回填 checklist、`implementation-plan.md`、README；旧文档的过期边界一并修正。
8. **不回归**：`hbg_tests`、`ctest`、`hbg_idl_check`、双后端 `--gfx-test`、`git diff --check`。

---

## 7. 文档回填清单（M12.0 内完成）

- `docs/m1-checklist.md`：
  - `:214` LightList 仍写“CPU 屏幕空间 AABB v1，不是 HDRP GPU compute/cluster”；M6/M7 已关闭，改为“已由 M6/M7 GPU light-list/cluster 关闭；剩余 256 lights/async”。
  - `:338` “LightCluster 是 16×16 屏幕空间 LightList tile”；M2.3/M6 已升级为 64×64×32 CPU + GPU cluster，修正。
  - `:355` 已知限制重复旧 LightCluster 描述；改为指向本台账 A01/B08。
- `docs/m2-checklist.md`：
  - `:383` SSGI debug “M2.9 统一接入”；M2.6 已实现，修正。
  - `:484` ReflectionProbe debug “M2.9 若需要再补”；仍未实现，保留并登记为本台账 B07。
  - `:279-281` cluster 边界保留；补充“M6.2 已 GPU cluster，M9 已 layered/category/layers”。
- `docs/m5-checklist.md` / `m6-checklist.md` / `m7-checklist.md`：
  - 把“明确不做”中已由 M7/M8/M9/M10/M11 关闭的项加“closed by Mx”或删除。
- `docs/m9-checklist.md` / `m10-checklist.md`：
  - M9.7 性能门禁改为“M11.4 记录通过，2026-11-17 复测需复核”；不直接写 `[x]`。
- `docs/m11-checklist.md`：
  - M11.0/M11.5 保持 `[~]`，直到全量 O3/fixtures/perf 收口。
- `README.md` / `docs/implementation-plan.md`：
  - 增加 M12 台账入口和“平台不可行冻结”说明；不复制台账全文。

---

## 8. HDRP/Core 源码索引

本台账引用的主要源码（任务实施时补实际 `file:line`）：

- 材质/GBuffer：`Runtime/Material/Lit/Lit.hlsl:98-170`、`Lit.shader:477-485/877-889`、`Lighting/LightLoop/LightLoop.cs.hlsl:63-67`、`RenderPipeline/HDRenderPipeline.Prepass.cs:721-793/827-855/1321-1341/1356-1447`。
- SSAO：`Runtime/Lighting/ScreenSpaceLighting/HDRenderPipeline.AmbientOcclusion.cs:147-188/204-250/253-312/410-450`、`GTAO.compute`、`GTAOCommon.hlsl`、`GTAOBlurAndUpsample.compute`。
- SSR：`Runtime/RenderPipeline/HDRenderPipeline.LightLoop.cs:1118-1450`、`Lighting/ScreenSpaceLighting/ScreenSpaceReflections.compute:139-227/279-460/640-671`、`ScreenSpaceTracing.hlsl:36-...`。
- SSGI：`Runtime/Lighting/ScreenSpaceLighting/ScreenSpaceGlobalIllumination.compute:26-329`、`RayMarching.hlsl:5-139`、`HDRenderPipeline.ScreenSpaceGlobalIllumination.cs:172-313/400-468/470-517`、`RayMarchingFallbackHierarchy.cs:9-30`、`GlobalIllumination.cs:21-151`。
- Probe：`Runtime/Lighting/Reflection/HDProbe.cs:735-803`、`Utilities/ProbeSettings.cs:141-175/283-298`、`Lighting/Reflection/VolumeProjection.hlsl:8-115`、`ReflectionProbeTextureCache.cs:126-135/243-329/594-636`、`LightEvaluation.hlsl:779-821`、`Lit.hlsl:1912-2007`。
- Fog：`Runtime/Lighting/AtmosphericScattering/Fog.cs:17-72/207-273`、`VolumeVoxelization.compute:51-99`、`VolumetricLighting.compute:157-290/574-774`、`AtmosphericScattering.hlsl:241-333`、Core `VolumeRendering.hlsl:128-193/271-304`。
- Shadow：`Runtime/Lighting/Shadow/HDShadowAlgorithms.hlsl:36-144`、`HDShadowManager.cs:872-890/1420-1565`、`HDCachedShadowAtlas.cs:23-83/342-467`、`HDCachedShadowManager.cs:41-52/127-143`、`HDDynamicShadowAtlas.cs:206-270`、`HDShadowUtils.cs:45-71/121-141/161-...`。
- Cookie：`Runtime/Lighting/LightCookieManager.cs:37-62/242-321/382-430`、`CookieSampling.hlsl:29-55`、`LightEvaluation.hlsl:16-113/233-257/380-435`、Core `PowerOfTwoTextureAtlas.cs:26/108-109`、`Texture2DAtlas.cs:606-665`。
- Light list/容量：`Runtime/RenderPipeline/HDRenderPipeline.LightLoop.cs:665-697`、`Lighting/LightLoop/LightLoop.cs:101-110/558-565/635-653/871-874/1921-1954`、`LightLoopDef.hlsl:245-330`、`lightlistbuild.compute:57`、`lightlistbuild-clustered.compute:256-500`、`Packages/com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:129-151`。
- 后处理：`Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs:802/1455-1585`、`PostProcessing/Shaders/Exposure.compute:15-153`、`FinalPass.shader:170-205`、Core `Color.hlsl:608-786`、`PhysicalCamera.hlsl:12-74`。
- FrameSettings：`Runtime/RenderPipeline/Settings/FrameSettings.cs:107/429/455/508/658/820`、`FrameSettingsDefaults.cs:7/92/149`、Core `Runtime/Volume/VolumeManager.cs`。
- Sky：`Runtime/Sky/HDRISky/HDRISkyRenderer.cs:90-161`、`HDRISky.shader`、`Sky/SkyUtils.hlsl:14-39`、`Sky/AmbientProbeConvolution.compute:51-215`、`Material/GGXConvolution/IBLFilterGGX.cs:129-194`、`GGXConvolve.shader`。
- Debug：`Runtime/Debug/DebugDisplay.cs:55-139`、`Runtime/Debug/DebugFullScreen.shader:230-475`。
- Async：`Runtime/RenderPipeline/Settings/FrameSettings.cs:393-407/599-606`、`HDRenderPipeline.LightLoop.cs:677`。
- RT-only screen-space shadow：`Runtime/Lighting/HDAdditionalLightData.cs:622-638`。

---

## 9. 下一步

1. **M12.0**：完成 A10（构建/CI/基线）+ 第 7 节文档回填；冻结 C 类。
2. **M12.1**：SSAO + SSR + SSGI temporal、raw/history debug、真实 resize/scale reset 已完成；剩余 M11.5 1080p full-post >60 fps 门禁（当前机器 baseline 已 55.1/47.0 fps，需安静 GPU 环境复测）。
3. 每个 M12.x 完成后，更新本台账状态列，不把新模块写回旧 checklist。

---

## 10. 变更记录

- 2026-11-17：创建 M12 边界台账；核对 M3–M11 的“明确不做/已知限制”；登记 A/B/C/D 类边界；记录 M11.0/M11.5 未收口项和 2026-11-17 性能复测波动。
- 2026-11-17：M12.1 temporal SSAO 落地：RGBA8 双缓冲 history、TAA 语义重投影、depth rejection、`--ssao-temporal` opt-in；全量 O3 512/0 zero-byte；详见 `docs/m12.1-checklist.md`。
- 2026-11-17：M12.1 temporal SSR 落地：RGBA16F 双缓冲 history、surface motion 重投影、depth 拒绝、current-dominant 0.1、`--ssr-temporal` opt-in；D3D11/Vulkan 静态/orbit/TAA/0.5 scale 0 diff；全量 O3 514/0 zero-byte；详见 `docs/m12.1-checklist.md`。
- 2026-11-17：M12.1 temporal SSGI 落地：RGBA16F RGB+sample-count history、R32F depth、RGBA8 normal、MRT、surface motion、depth/normal 拒绝、`--ssgi-temporal` opt-in；1080p TAA+DOF+MB 双后端 >100 fps；全量 O3 516/0 zero-byte；详见 `docs/m12.1-checklist.md`。
- 2026-11-17：M12.1 raw/history debug 落地：SSAO/SSR/SSGI 增加 `-raw` / `-history` / `-denoised`；history 显示上一帧 temporal buffer；D3D11/Vulkan 1280x720 `--no-shadows` 验证；SSGI 新增 `debug_ssgi_raw.fs.sc`；Shader 518/0 zero-byte。
- 2026-11-17：A01 补验收：真实窗口 resize 1280x720→1008x537→1264x681、DebugUI render scale 1.0→0.52→1.0、setter temporal toggle 均重建 history；修正 SSR/SSGI temporal 取样 Y 翻转，SSR orbit ghost 从 237 px/max176 降至 18 px/max1；M11.5 64-light full-post 3 次中位数 D3D11 55.1/51.6/50.8/44.3、Vulkan 47.0/45.4/47.1/38.8 fps，>60 门禁仍未过。
- 2026-11-20：M12.2 IMS v1：`ShadowFilter::Ims`/mode 8，directional-only；RGBA32F moment atlas + per-slot SAT，`ShadowMoments`/`ShadowSatHorizontal`/`ShadowSatVertical` compute；stage 12 在 IMS 下切 SAT；后续补 integer blocker search、bilinear filter、light-angle/maxDepthBias/kernel 参数、退化 guard 与 per-request（cascade）shadow context texel9 参数；`m26_ssgi_corner` frames=1 IMS vs MSM 53.14 dB、vs PCF 45.75 dB，D3D/VK 67.08 dB；`m10_dir_shadow` no-contact 单 cascade IMS vs PCF 46.79 dB，4 cascade 28.69 dB（640），4 cascade D3D/VK 36.93 dB。
- 2026-11-19：M12.2 MSM v1：`ShadowFilter::Msm`/mode 7，HDRP `SolveMSM` + Delta3/Delta4 + 4 个 raw-depth moments + local 3x3 gather；area 60.48 dB vs PCSS、89.06 dB vs VSM；双后端 87.43/87.59 dB；IMS（SAT/moment atlas）等仍开放。
- 2026-11-19：M12.2 VSM v1：`ShadowFilter::Vsm`/mode 6，HDRP Chebyshev + 原始 depth moments + local 3x3 gather；area 60.49 dB vs PCSS、vs EVSM 78.57 dB；双后端 100.24/102.55 dB；MSM/IMS 等仍开放。
- 2026-11-19：M12.2 EVSM v1：`ShadowFilter::Evsm`/mode 5，HDRP EVSM 公式 + local 3x3 raw-depth moment gather；area 60.63 dB vs PCSS、双后端 100.24/103.01 dB；VSM/IMS 等仍开放。
- 2026-11-18：M12.2 进度：area+point PCSS 共存（point depth 移到 stage 9，area 保持 stage 13），cached directional per-cascade partial（draw 75→63，shadow_drawn 31→15），P0 跨后端 0 diff；EVSM/VSM/IMS、mixed、transparent/SSS/cloud 未完成，见 `docs/m12.2-checklist.md`。\n- 2026-11-17：P0 阴影分叉定位并修复：`shadow.context` 从 Storage index（D3D R32_UINT SRV）改为 StorageVertex RGBA32F typed context，修复 D3D 读取全 0；atlas PCF texel 从 `1/viewport` 改为 `atlasRect.xy * 1/viewport = 1/atlasSize`，消除 point 256/2048 的 8× tap 间距；point face 加 6° guard。`m26_ssgi_corner` forward D3D/VK **102.13 dB**、deferred **101.76 dB**，point-only **109.54 dB**；M12.2 可恢复带 shadows 的跨后端对比。已知残余 point face 边界约 1 px 细线。
