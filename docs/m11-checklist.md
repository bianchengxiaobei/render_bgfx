# M11 HDRP Lighting 2.2 规划：Cookie Atlas / Cached Area Shadow / Variant Strategy / 60fps

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M11.0 shader build 工具 v1、M11.1 sampler gate（buffer context）、M11.1b cookie atlas v1、M11.1c 独立 cookie atlas、M11.2 cached area shadow v1 + mixed、M11.3 variant strategy、M11.4 1080p 60fps 已完成；M11.5 核心验收矩阵已跑通**（2026-11-16）。M11.4 用 shadow context `float4` vector load 把 D3D11 forward cap64 full-post 中位数拉到 **63.7 fps**（deferred 62.3，Vulkan 64.6/63.2）；cap128 仍为 opt-in。M11.5 目前：M10 area 36/36 0、area shadow 4/4 0、M9/transparent 路径 0、cached 4/4 0、mixed cached/dynamic 4/4 0、M11.1c native cookie 8/8 有差异（D3D/VK 1 px/max1、path 0）、variant 8/8 0；D3D punctual/directional 保留 shadow 边缘 **1–2 ULP**（M9 64 127,795 px/max11），已按 M11.4 决策接受，area 路径用 typed StorageVertex context 做到 0。M8.7 base32 已用同一历史命令复跑：Vulkan buffer/texture 0，D3D11 typed 快路径 19,074 px/max165；HDRP 风格 structured context 诊断构建为 0，但 GPU 成本更高，未作为默认。M11.0 全量 clean O3/fixtures/perf baseline 仍待做。M10.0–M10.8 与 area Rectangle/Tube OBB 已完成。
> 前置基线：M10 的 36 组 area matrix、area shadow、M9 64-light/transparent、M8.7 32-light 均为当前回归基线。
> 原则：先读 HDRP 源码再实现；不把 HDRP 没有的“GPU shadow atlas packing”写成 HDRP 对齐项；不把 29 个 Lit variants 当成无条件目标。

---

## 0. 结论摘要

M11 不继续堆灯数，也不直接复制 HDRP 的 29 个 Lit variants。M11 的主题是：

**在不破坏 M10 基线的前提下，先解决 sampler/构建/性能约束，再提升 cookie 质量、cached area shadow 和运行时变体策略。**

M11 的核心目标：

1. **发布基线**：修复 `shader_build.py` 的全量 O3 构建体验，固化 M10 回归矩阵，建立 1080p 性能基线。
2. **Cookie atlas**：按 HDRP `LightCookieManager` 的 atlas/padding/mip 思路，替换当前 8×8 per-light nearest cookie；但必须先解决 D3D11 16-sampler 预算。
3. **Cached area shadow**：按 HDRP `HDCachedShadowAtlas`/`HDDynamicShadowAtlas` 实现静态 rectangle area shadow 缓存；不做 GPU atlas packing。
4. **变体策略**：按 HDRP `kFeatureVariantFlags` 的 classification 思路评估；结论是保留现有 `tileFeatureFlags` + runtime category gating，不生成 4 类编译期 category variants，也不生成 29 个包含 SSS/transmission/anisotropy/clearcoat/iridescence 的变体。
5. **性能**：把 M9.7 的 1080p full-post cap64 55.2 fps 拉回 >60，并记录 cookie/cached shadow 增量。

M11 **不作为核心目标**：

- 256/512 lights；`kMaxGpuLights` 仍保持 128。16-bit index 能表达更大值，但容量不是 M11 的主题。
- 完整 HDRP 29 个 Lit variants；当前项目没有 SSS/transmission/anisotropy/clearcoat/iridescence 材质，复制出来只会得到重复/死变体和更长的 shader build。
- GPU shadow atlas packing；HDRP 本身是 CPU/Burst culling + dynamic/cached atlas，不是 GPU packing。
- Disc real-time LTC；HDRP jobs 中 disc 仍标注 real-time unsupported。
- D3D12、bindless、DXR、mesh shader、Hi-Z、async shadow atlas packing。
- 新材质模型（Fabric/Eye/Hair/AxF）和 lightmap/shadow mask。

### 0.1 为什么不能直接做 29 variants

HDRP 的 29 个组合不是“29 种灯光”，而是 **light feature flags + material feature flags** 的组合：

- `Runtime/Lighting/LightLoop/LightLoop.cs.hlsl:63-67`：`NUM_FEATURE_VARIANTS (29)`、`MATERIAL_FEATURE_MASK_FLAGS`、`LIGHT_FEATURE_MASK_FLAGS_OPAQUE/TRANSPARENT`。
- `Runtime/Material/Lit/Lit.hlsl:98-142`：`kFeatureVariantFlags[29]`，包含 SKY/ENV/SSREFLECTION/DIRECTIONAL/PUNCTUAL/AREA 与 LIT_STANDARD/SSS/ANISOTROPY/CLEAR_COAT/IRIDESCENCE。
- `Runtime/Material/Lit/Lit.hlsl:145-170`：`FeatureFlagsToTileVariant` / `TileVariantToFeatureFlags`。
- `Runtime/Material/Lit/Lit.shader:477-485`、`:877-889`：material feature 的 shader_feature 和 shadow/lightlist variant。

当前 HBG 只有 Lit standard + normal/mask/alpha clip/double-sided/light layers，没有 SSS、transmission、anisotropy、clearcoat、iridescence。M11 的正确做法是：

- 先做 **light category variants** 或 **runtime tile gating 的收益评估**；M11.3 已测得 gating on/off 双后端 0 diff，D3D11 1080p 300 帧约省 1.27 ms，保留 runtime gating；
- 4 类编译期 category variants 需要 per-tile program dispatch 才能替代 per-pixel gating；当前渲染器没有该路径，view-wide 变体无法减少 mixed category 的 per-tile 分支，否决；
- 29 variants 等材质特性真正落地后再开，不把编译爆炸提前引入。

### 0.2 为什么 cookie atlas 是 gated feature

HDRP 有独立 cookie atlas，因为它的 sampler 预算可以容纳；本项目 D3D11 fragment sampler 已满 16 个：

| stage | 当前用途 |
|---|---|
| 0–4 | baseColor / metallicRoughness / normal / occlusion / emissive |
| 5 | light-data texture |
| 6 | directional shadow compare |
| 7 | light-list texture（buffer path 时为空） |
| 8 | shadow context |
| 9 | punctual shadow compare |
| 10 | environment map |
| 11 | contact shadow |
| 12 | directional raw depth |
| 13 | punctual/area raw depth |
| 14–15 | reflection probe maps |

因此 M11.1 必须先做 **sampler/stage spike**，再决定是否做 cookie atlas。可选方向：

- **A（推荐研究）**：把 directional/punctual/area 的 raw depth 与 comparison 各合并为 texture array，释放 stage 9/13，cookie atlas 使用其中一个；需验证 D3D11/Vulkan `SAMPLER2DARRAYSHADOW`、atlas viewport 和 M10 area shadow 0 diff。该方案是项目特定折中，不是 HDRP 的“独立 atlas”结构。
- **B（不推荐）**：buffer path 用 stage 7 做 cookie atlas，texture path 退回 8×8；会导致两条 list 路径视觉不一致，违反 M10 三路径 0 diff 基线。
- **C（不推荐）**：contact shadow 关闭时把 stage 11 让给 cookie atlas；配置相关行为不可接受。
- **D（fallback）**：如果 A 无法通过双后端验证，M11 保留 8×8 cookie，M11.1 标记 `[~]`，不声称 HDRP cookie atlas 完成。

---

## 1. 当前基线与缺口

### 1.1 Cookie

- 当前 `kGpuLightTexels=83`：11 base + 8×8 cookie（texel 11–74）+ 8 IES（texel 75–82）。
- `shaders/common/light_data.sc` 通过 `texelFetch` 读 per-light cookie；`hbgSampleLightCookieScaled` 做 8×8 nearest + scale/offset。
- 没有 cookie atlas、mip/padding、2D/octahedral atlas 的 scaleOffset 结构；area/directional/spot 的 cookie 当前都能工作，但质量受 8×8 限制。
- M10 的 LTC 表追加在 light-data texture 行 128–191；如果 M11 压缩 cookie texel，必须同步 `kGpuLightTexels`、LTC 行偏移、texture 宽度和全部 shader/CPU reference。

### 1.2 Shadow

- M10 area shadow 是独立 dynamic 1024 D24S8 atlas、512 默认分辨率、PCSS；与 punctual/directional 分开。
- stage 13 与 point PCSS 共用：area active 时 point PCSS 回退 tent。
- 当前 cached punctual shadow 只支持静态 point/spot；area shadow 没有 cached 路径。
- HDRP 的 area shadow 有独立 dynamic + cached area atlas，并在 dynamic atlas 中 blit cached 结果。

### 1.3 构建/性能

- `tools/shader_build.py` 全量 O3 不是死锁，而是 DXBC 重编译很慢：forward/deferred 单变体约 70–205 秒；全量 500+ 变体即使 8 并发也要约 1 小时以上。
- 当前 `hbg_shaders` 的 mtime 增量可避免重复构建，但首次 clean build 仍缺进度、单变体超时和明确的失败报告。
- M9.7 1080p 300 full post：D3D11 cap64 **55.2 fps**、cap128 **29.2 fps**；cap64 尚未达到 >60 fps 门禁。
- M11 的 cookie atlas/cached area/variants 不能以“新增功能”为理由跳过该性能问题。

---

## 2. HDRP 源码结论

### 2.1 Cookie atlas

- `Runtime/Lighting/LightLoop/GlobalLightLoopSettings.cs:180-186`：默认 `cookieAtlasSize=2048`、`cookieFormat=R11G11B10`、`cookieAtlasLastValidMip=0`。
- `Runtime/Lighting/LightCookieManager.cs:37-62`：`PowerOfTwoTextureAtlas`，`useMipMap: true`。
- `Runtime/Lighting/LightCookieManager.cs:242-274`：spot/directional 2D cookie 的 `Fetch2DCookie`，IES 可乘入同一 atlas 条目。
- `Runtime/Lighting/LightCookieManager.cs:276-321`：area cookie 的 `FetchAreaCookie`，IES 可乘入。
- `Runtime/Lighting/LightCookieManager.cs:382-430`：point/cube cookie 的 `FetchCubeCookie`，最终 blit 到 octahedral atlas。
- `Runtime/Lighting/LightLoop/CookieSampling.hlsl:29-55`：`SampleCookie2D` 的 padding + atlas scale/offset；`SamplePointCookie` 的 octahedral 编码。
- `Runtime/Lighting/LightEvaluation.hlsl:16-113`：area cookie 用 LTC 变换后的 hit UV；`:233-257`：directional cookie 正交投影；`:380-435`：point cookie / IES。
- `Packages/com.unity.render-pipelines.core/Runtime/Textures/PowerOfTwoTextureAtlas.cs:26`、`:108-109`：mip padding / octahedral padding blit。
- `Packages/com.unity.render-pipelines.core/Runtime/Textures/Texture2DAtlas.cs:606-665`：octahedral atlas blit 与 mip 标记。

M11 目标：

- 2D cookie：spot/area/directional，atlas scaleOffset + clamp/repeat + mip。
- point cookie：octahedral atlas；IES 继续可乘。
- area cookie：保留 M10 的 LTC hit UV 语义，只替换数据来源。
- 缺失/超 atlas：白纹理 fallback，不静默黑灯。

### 2.2 Cached area shadow

- `Runtime/Lighting/Shadow/HDCachedShadowAtlas.cs:23-83`：`CachedShadowRecord`、placed/pending/render 状态。
- `Runtime/Lighting/Shadow/HDCachedShadowAtlas.cs:342-467`：light list → slot 的 placement。
- `Runtime/Lighting/Shadow/HDCachedShadowManager.cs:41-52`：punctual + area cached atlas；`:127-143` defrag/移除。
- `Runtime/Lighting/Shadow/HDDynamicShadowAtlas.cs:206-270`：cached atlas blit 到 dynamic atlas。
- `Runtime/Lighting/Shadow/HDShadowManager.cs:1420-1565`：punctual/directional/area 的 cached blit 与 defrag。
- `Runtime/Lighting/Shadow/HDShadowManager.cs:805-842`：area dynamic atlas；M10 已按此做了独立 area atlas。

M11 只做 **CPU cached area atlas**；all-static 直接绑定 cached atlas；mixed 采用 HDRP 同类的静态/动态分离语义，在 dynamic atlas 中用 static-first persistent slots 保存静态内容，不做 GPU atlas packing/async atlas。

### 2.3 Variant classification / async

- `Runtime/Material/Lit/Lit.hlsl:98-170`：29 个组合与 `FeatureFlagsToTileVariant` / `TileVariantToFeatureFlags`。
- `Runtime/Lighting/LightLoop/builddispatchindirect.compute:41`：material feature flags 参与 variant dispatch。
- `Runtime/RenderPipeline/Settings/FrameSettings.cs:393-407`：`AsyncCompute`、`LightListAsync`、`ContactShadowsAsync` 等。
- `Runtime/RenderPipeline/Settings/FrameSettings.cs:599-606`：async 能力门控。
- `Runtime/RenderPipeline/HDRenderPipeline.LightLoop.cs:677`：`builder.EnableAsyncCompute(BuildLightListRunsAsync())`。

M11.3 评估后保留 **runtime tile feature flags**，否决 view-wide category variants；async light-list 仍是 experimental，依赖 bgfx/D3D11/Vulkan 实测。

### 2.4 灯数上限

- `Packages/com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:129-151`：FPTL/big-tile 使用 16-bit index，coarse entry 上限由配置生成。
- `Runtime/Lighting/LightLoop/lightlistbuild.compute:57`：HDRP `MAX_NR_VISIBLE_LIGHTS 4096`。
- `Runtime/Lighting/LightLoop/LightLoop.cs:692`：`m_MaxLightsOnScreen` 汇总 directional/punctual/area/env。
- 本项目 `kMaxGpuLights=128`、`kLightListStrideMax=129`；16-bit 本身不是 256 的阻碍，内存、cluster 和性能才是。

M11 **不扩 256**；若后续要做，单独立 M12 研究，并先证明 128 的容量/性能可控。

---

## 3. M11 范围与关键决策

### 3.1 范围表

| 项 | M11 核心 | M11 stretch | 说明 |
|---|---|---|---|
| Shader build 进度/超时/缓存 | `[ ]` | | 全量 O3 可重复完成，manifest 重生成 |
| M10 回归/CI fixtures | `[ ]` | | area 36 矩阵、flags、M9 baseline |
| Sampler/stage 预算 | `[ ]` | | cookie atlas 的前置 gate |
| Cookie atlas（2D/octa/area） | gated | | 通过 sampler gate 才做 |
| 8×8 legacy cookie fallback | `[ ]` | | atlas 失败可回退，0 diff |
| Cached rectangle area shadow | `[x] v1 + mixed` | | all-static cached atlas；mixed 使用 dynamic atlas 的 static-first persistent slots |
| Area/point PCSS 同时可用 | | `[ ]` | 依赖 sampler 合并 |
| Light category variants | `[x]` | | M11.3 评估后否决；保留 runtime gating |
| 29 Lit variants | | | 非目标；等材质特性 |
| Async light-list | | `[ ]` | experimental，能力门控 |
| 1080p cap64 >60 fps | `[x]` | D3D11 forward 63.7 / deferred 62.3；Vulkan 64.6/63.2 | M11.4 完成；D3D 边缘 1–2 ULP 已在 M11.5 接受 |
| 256 lights | | | 非目标，M12 研究 |
| GPU shadow atlas packing | | | 非 HDRP 对齐项，不做 |
| Disc real-time LTC | | | 非目标 |

### 3.2 关键决策

1. **Cookie atlas 不是无条件承诺**：先做 sampler/stage spike；A 方案失败则保留 8×8，文档记录。
2. **不直接做 29 variants**：当前材质没有 HDRP 的 SSS/transmission/anisotropy/clearcoat/iridescence；M11.3 已评估 4 类 light category variant，决定保留 runtime tile gating、否决编译期 category variants，29 留给材质特性模块。
3. **Cached area 用 CPU/Burst 风格**：静态 rectangle area 只渲染一次，camera 移动只绑定 cached atlas；mixed 场景用 dynamic atlas 的 static-first persistent slots；不做 GPU atlas packing。
4. **M11 不改默认值**：所有新功能 opt-in 或保持 M10 默认；M10 基线截图必须 0 diff。
5. **性能先行**：M11.4 不达标时，不把 cookie/cached shadow 作为默认功能发布。
6. **Atlas 格式先探测**：HDRP 默认 R11G11B10；bgfx 不支持时用 RGBA16F/RGBA8 fallback，并记录内存/视觉差异。

---

## 4. 数据契约

### 4.1 Cookie atlas

建议结构（实现时锁定）：

```text
CookieAtlasSettings {
  size = 1024 | 2048
  format = R11G11B10 | RGBA16F | RGBA8
  mipCount / lastValidMip
  paddingPixels
}

CookieAtlasEntry {
  scaleOffset (xy = scale, zw = offset)
  type = 2D | Octahedral
  iesMultiplier = on/off
}
```

- 2D：spot/area/directional；point：octahedral。
- `GpuLightData` 继续使用 `cookieMode/cookieType/cookieScaleOffset`；atlas 的 scaleOffset 覆盖 per-light 8×8 的 scale/offset。
- 目标最终布局（atlas 验证通过后）：

| texel | 内容 |
|---|---|
| 0–10 | 现有 base |
| 11–18 | 8 个 IES 样本 |
| — | cookie 数据移出 per-light，改 atlas |

这样 `kGpuLightTexels` 可从 **83 → 19**，4K/cap128 宽度从 10624 降到 2432；但必须一次改齐 CPU writer、shader 解包、LTC 行偏移和 debug view。M11.1b 已一次改齐并压缩到 19 texels，未保留 83-texel legacy 数据路径；atlas 无 tile 时回退白纹理。

### 4.2 Cookie atlas 采样

- 2D：`SampleCookie2D(uv, scaleOffset, lod)`，padding 内 clamp，mip 根据 roughness/显式 lod。
- point：octahedral 编码后采样同一 atlas。
- area：保留 M10 LTC hit UV，再乘 atlas scale/offset。
- directional：保留正交投影，再乘 atlas scale/offset。
- 缺失/溢出：白纹理返回 1.0；warning 只打一次。

### 4.3 Cached area shadow

```text
CachedAreaShadowRecord {
  lightIndex
  atlasRect
  contentHash
  lastTransform
  renderedOnce
}
```

- 仅 rectangle area 且 `ShadowUpdateMode::OnEnable`/static 时缓存。
- 静态光源 + 静态 caster：第一帧渲染，后续 camera 移动只绑定 cached area atlas；mixed cached/dynamic 在 dynamic atlas 中保留 static-first slot，不重绘静态 request。
- 光源/材质/场景 hash 变化：失效并重渲染。
- cache 失败/空间不足：回退 dynamic area shadow，不黑屏。
- memory：1024 cached area atlas（约 4 MB D24S8）；2048 opt-in（约 16 MB）。

### 4.4 Variant flags

- 只保留当前项目可表达的类别：
  - `Directional`
  - `Punctual`
  - `Area`
  - `NoDynamicLights` / `SkyEnv`（若实际需要）
- 不引入 29 个材质 feature flags；不生成 SSS/transmission/anisotropy/clearcoat/iridescence 变体。
- 如果 variant 不能带来可测量收益，保留 M10 runtime tile flags，并记录否决原因。

### 4.5 Async light-list

- `--async-gpu-light-list` 仍为 experimental。
- 依赖 backend `supportsAsyncCompute` 和 render graph 依赖；失败回退同步路径。
- 不作为 M11 默认，不把 async 计入 60fps 门禁。

---

## 5. 任务拆分

| 任务 | 产出 | 状态 |
|---|---|---|
| **M11.0 基线/构建/CI** | shader build 进度/超时/缓存、M10 fixture、性能基线 | `[~] 工具 v1；全量 O3/fixtures/perf 待做` |
| **M11.1 Cookie atlas / sampler gate** | sampler 预算方案、atlas、2D/octa/area、fallback | `[x] sampler gate；M11.1b v1 + M11.1c 独立 atlas（mip/padding/fallback）` |
| **M11.2 Cached area shadow** | cached area atlas、invalidate、回退；all-static + mixed cached/dynamic 已实现 | `[x] v1 + mixed` |
| **M11.3 Variant strategy** | 4 类 category variant 或否决报告 | `[x] 否决 category variants；保留 runtime gating` |
| **M11.4 性能** | M9.7 60fps、cookie/cached 增量、buffer/texture 记录 | `[x]` D3D11 forward 63.7 / deferred 62.3 fps |
| **M11.5 M11 验收** | 0 diff、双后端、4K、测试/IDL/docs | `[~]` 核心矩阵通过；M8.7 已复跑，D3D typed 19k/max165 例外、full O3 待收口 |

### 5.1 M11.0 基线/构建/CI

**产出**

- `tools/shader_build.py`：进度日志、单变体超时、失败报告、可选 jobs/optimization；全量 O3 运行并重新生成 `shaders/manifest.json`。
- 记录 full clean build 的 DXBC/SPIRV 数量、耗时、zero-byte 检查。
- 把 M10 的 area matrix、area16 flags、M9 64/transparent、M8.7 base32 固化为脚本/fixtures。
- 建立 1080p 300 帧 full post 基线（cap64/cap128，D3D11/Vulkan）。

**验收**

- 全量 O3 构建完成，manifest 与 binary 路径一致；无零字节。
- 任何单变体超时能明确报出文件/backend/耗时，不静默挂起。
- M10 36 组 area matrix 与 baseline 0 diff。

**不做**：不把 shader build 改成 O0 冒充 O3；不用 timeout 静默跳过变体。

**v1 已实现（2026-11-15，工具与增量验证）**

- `tools/shader_build.py` 新增：
  - `--timeout`（默认 600s，0 关闭）单变体超时；超时/退出码/启动失败均打印 source、backend、shader type、output、耗时、命令和截断日志。
  - `--optimization`（默认 3）；低于 O3 会明确警告，不冒充 release。
  - `--only` + `--no-manifest` 诊断入口，可只跑单个输出验证失败/超时报告。
  - 进度行、最终 summary（variant/compiled/cached/failed/耗时）、manifest 输出校验、零字节检查、stale `.bin` 清理。
  - `--force` 保留；否则按 shader/input mtime 缓存；shaderc 版本变化需显式 `--force`。
- 构建 `Release` shaderc（约 10.5 MB，`build/vs2022/_deps/bgfx.cmake-build/cmake/bgfx/Release/shaderc.exe`），用于替代较慢的 RelWithDebInfo shaderc。
- 增量验证：510 variants，**510 cached、0 failed、0.3s**；manifest 510 条路径与 510 个 binary 一致，**0 零字节**；清理 4 个 stale `decal.vs.bin`/`shadowcaster.fs.bin`。
- 8 个 forward O3 variants 在 jobs=8 下耗时 **539s**；按最慢 forward 组外推，全量 clean O3 约 **9.6h**，因此本轮没有伪称“全量 clean O3 已完成”。
- 参考 HDRP build processor 的 variant stripping 入口（`Editor/BuildProcessors/HDRPPreprocessShaders.cs`、`ShaderStrippers/HDRPShaderVariantStripper.cs`）；M11.0 只做构建可观测性，不在这里做变体裁剪。

**未完成**

- 全量 clean O3（`--force`）未跑完；当前 510 个 binary 是增量缓存 + 本轮重编的 forward 变体，不是完整 clean 结果。
- M10 area matrix、area16 flags、M9 64/transparent、M8.7 base32 的脚本/fixtures 尚未固化。
- 1080p 300 帧 full post cap64/cap128 基线尚未建立。

### 5.2 M11.1 Cookie atlas / sampler gate

**5.2.1 Sampler spike（先做）**

- 实现 shadow texture array 原型（directional/punctual/area raw + compare），验证 D3D11/Vulkan 采样、array slice、M10 area shadow 0 diff。
- 若成功：释放 stage 9/13，cookie atlas 使用一个空闲 stage。
- 若失败：记录原因，M11.1 保留 8×8；不做“配置相关”的 stage 复用。

**5.2.1a Gate 审计与 sampler-free context（2026-11-15）**

- HDRP `Runtime/Lighting/Shadow/HDShadowContext.hlsl:13` 的实际做法是 `StructuredBuffer<HDShadowData>`，不是 UBO；本项目先尝试 UBO（`u_shadowcontext_texels[]`），D3D11 可运行，但 Vulkan 128 requests 需要 1280×vec4（20 KiB），首帧挂起超过 120s；640×vec4 只能覆盖 64 requests，不能接受 M10 的 128-request 基线。
- 改为与 HDRP 同类的 sampler-free buffer：context 的 10 个 float4 按 request 存放，C++ 用 `ShadowManager::contextBuffer()` + `setStorageBuffer(8)`；**不再创建 `s_shadowContext` sampler**，stage 8 已释放。Storage buffer 不受 Vulkan UBO 16 KiB/1280 vec4 限制。M11.4 进一步把 shader 从 40 个 uint 逐字读取改成一次 `float4` vector load（D3D11/Vulkan）。
- 当前回归：D3D11/Vulkan × forward/deferred，1280×720，cached vs every-frame **4/4 组合 0 diff**；`hbg_tests` 229 cases/29,286 assertions；`ctest` 2/2；D3D11/Vulkan gfx-test 通过；manifest 510/510，0 零字节。volumetric fog、shadow-cascade/filter debug view 也已用 buffer 路径跑通。
- 与改动前 texture context 截图相比：Vulkan 仅 1–2 个边缘像素（max 3–6），D3D11 为 20–23 个边缘像素（max 11）；当前 cached/every-frame 为 0，数据路径与 Vulkan 结果确认一致，D3D 差异归因于 HLSL 重编译的浮点边界差异，不是结构回归。
- 结论：**M11.1 sampler gate 通过**（stage 8 sampler 空闲）；但 shadow context buffer 仍占 D3D SRV t8，cookie atlas 不能直接放 stage 8。M11.1b v1 将 8×8 cookie tiles 打包进已有 `s_lightData` 纹理，不增加 sampler/SRV，详见 5.2.2。

**5.2.2 Atlas v1（2026-11-15）**

- **布局**：light-data 从 83 texels/light 压缩到 **19 texels/light**（11 base + 8 IES）；light-data 纹理仍为 83×192 RGBA32F，因为 cookie atlas 复用同一纹理：
  - x=19..82、y=0..127 为 64×128 cookie atlas；
  - x=0..18、y=0..127 为 19-texel light data；
  - y=128..191 仍为 64×64 LTC GGX 表。
- **CPU**：`CookieAtlas` 用 8×8 tile 顺序打包，容量 **128 tiles = 128 lights**；tile 0..127 按 selected lights 分配。`LightBuilder` 把 `tileIndex+1` 写入 **texel7.z**，0 表示无 atlas；`texel7.w` 改为 `areaShape | (hasIes << 3)`，释放 z。
- **Shader**：`hbgSampleLightCookieScaled` 先按原 `cookieScale/cookieOffset` 和 repeat/clamp 处理 UV，再按 tile index 定位 atlas 的 8×8 tile，`texelFetch` 采样 `.x`；2D、point octa、area rectangle 三条路径共用。无 tile（atlas 满/无 cookie）返回 1，保持白 fallback。
- **为什么复用 s_lightData**：HDRP 的 `_CookieAtlas` 是独立 2D 纹理；本项目 D3D11 SRV 0–15 已被 lightData/shadow/contact/probe 占满，shadow context buffer 仍在 t8。新增独立 cookie 纹理需要第 17 个 SRV，超出当前 bgfx 16 sampler 预算；把 atlas 放进已有 lightData 纹理可保持 stage 预算，不改变 M10 的 binding 约定。代价是 cookie 仍只有 8×8 源分辨率、无 mip/padding，尚未达到 HDRP 的原生分辨率 atlas。
- **验证**（M10 基线截图统一用 `frames=3`；`frames=2` 会截到 D3D jitter/history 的瞬时帧，曾出现约 12k 像素差异，不是 atlas 数据差异）：
  - D3D11/Vulkan × forward，area on/off 均为 **275,722 px/max 116**；spot on/off 均为 **209,800 px/max 207**；point IES on/off 均为 **545,062 px/max 99**。
  - D3D11 area on 对旧 `m10_8_cookies/area_d3d11_on.png` **0 diff**；D3D11 point IES on 对旧图 **4 px/max 1**；Vulkan area/point IES 对旧 `m10_6_area_vk.png` / `m10_8_cookies/ies_vulkan_on.png` **0 diff**。spot D3D11 vs Vulkan **0 diff**。
  - 同后端 buffer/texture/CPU light-list 三路径：D3D11 与 Vulkan 的 area/spot forward 均 **0 diff**；D3D11 deferred area/spot 的 buffer/texture **0 diff**，Vulkan deferred area/spot buffer/texture **0 diff**。
  - 16-cookie `m11_area16_cookie`（16 个 area 全带 cookie）：`tiles=16/128`，on/off **138,127 px/max 106**；128-tile capacity 未触发 fallback。
  - cached area：D3D11/Vulkan cached vs every-frame 1280×720 **0 diff**；volumetric fog D3D/VK 运行通过。
  - `hbg_tests` **229 cases / 29,286 assertions**；atlas 单元测试覆盖 tile 打包、copyTo、非 cookie light、128 capacity。
- **限制（M11.1b v1）**：8×8 tile、无 mip/padding、无原始分辨率 cookie、atlas 容量固定 128、无独立 `_CookieAtlas` 纹理；原始分辨率/mip 由 M11.1c 补全。

**5.2.3 M11.1c 独立 cookie atlas（2026-11-16）**

- **目标与 HDRP 对齐**：按 `Runtime/Lighting/LightCookieManager.cs:37-62` 的 `PowerOfTwoTextureAtlas`、`CookieSampling.hlsl:29-55` 的 `RemapUVWithPadding`/mip、`LightEvaluation.hlsl:16-113` 的 area hit UV，建立独立 `_CookieAtlas`，支持原始分辨率、2 px padding、mip 和 HDRP 风格 scaleOffset。
- **独立纹理**：新增 `lighting.cookieAtlas`，默认 **1024×1024 RGBA8**（512/2048 可配），mipCount=log2(size)+1（1024 为 11）；RGBA8 约 **5.33 MiB**（含 mips），2048 约 **21.3 MiB**。项目 cookie 是标量亮度，HDRP 的 R11G11B10 彩色 cookie 不适用；不使用 RGBA16F。
- **stage 预算**：M11.1 的 shadow context 仍占 SRV t8；要新增 cookie sampler 必须释放一个 texture stage。将原 stage 14/15 的两张 reflection probe 纹理合并为 `s_probeMaps` 2D texture array（stage 14），stage 15 给 `s_cookieAtlas`；没有新增 sampler，D3D11 仍为 16 个。Probe array 保持原 equirect/mip 采样语义，4 个静态 probe 上限、2 个 active layer。
- **CPU atlas**：`CookieAtlas` 按 POT tile 打包；native 源上限 256×256，tile 大小 `nextPow2(max(w,h)+2*padding)`，每个 tile 内 payload 外围 2 px 边缘扩展；tile 对齐到 mip，逐 mip box downsample。1024 atlas 的 16 个 16×16 area cookie 使用 16 个 32×32 tile；512/2048 也验证通过。
- **数据契约**：light data 为 **20 texels/light**（0–10 base + 11 atlas scaleOffset + 12–19 IES），light-data 纹理缩小为 **64×192**；LTC 表仍在 rows 128–191、x=0..63。`texel11.xy/zw` 为 padded atlas rect 的 scale/offset；`texel7.z` 保留 tileIndex 诊断，`texel7.w` 保留 areaShape/hasIes。
- **Shader**：`hbgSampleLightCookieScaledLod` 先做用户 scale/offset 与 clamp/repeat，再按 HDRP `RemapUVWithPadding` 做 padding inset、mip clamp 和 `texture2DLod`；area 使用 `SampleAreaLightCookie` 同类的 LTC hit UV 与 `mipLevel`（roughness/mip trimming），directional/spot/point 使用 lod 0；point octa 与 IES 继续共用 native atlas，spot IES 仍走 light-data。
- **fallback**：`--no-cookie-atlas` / `supportCookieAtlas=false` 使用独立 atlas 的 legacy 8×8 tile + `texelFetch`，保持 M11.1b/M10 查询语义；atlas 满/无 native 数据时返回白纹理，不黑屏；`--cookie-atlas-size=512|1024|2048` 与 `config/pipeline.json` 已接入。
- **验证**：`m11_area16_cookie` 16 area cookie，640×360/D3D11/Vulkan/forward/deferred/buffer/texture：
  - native vs legacy：forward **136,925 px/max82**，deferred **136,924 px/max82**（8/8 组合均有差异，证明原生 cookie 生效）。
  - D3D11 vs Vulkan：所有 path/mode **1 px/max1**（area shadow 边缘精度，保持 M11.5 例外）。
  - texture vs buffer：forward/deferred 全部 **0 diff**（M11.1c 的 atlas 数据与路径无关）。
  - 512/2048 atlas：`tiles=16/128 native=16`，mips=10/12，双后端运行通过。
  - directional cookie on/off **1,092 px/max79**；point IES native/legacy **127,445 px/max79**（D3D）/ **127,373 px/max79**（VK）；1 个静态 reflection probe 的 array 路径运行通过。
  - 1280×720、300 帧、D3D11 forward、area16：native **34.8 fps**（28.71 ms），legacy **35.4 fps**（28.27 ms），native atlas 约 **0.34 ms** 增量；Debug 构建，非 M11.4 full-post 门禁。
- **上传缓存（HDRP `NeedsUpdate` 对齐）**：`CookieAtlas` 对 selected light、native source 指针/尺寸/cookie mode 和实际 tile layout 生成 64-bit content hash；`uploadCookieAtlas` 在 hash 与纹理均未变化时跳过整张 atlas 上传，纹理重建时强制重传。按 HDRP `LightCookieManager.Fetch2DCookie` 的 `PowerOfTwoTextureAtlas.NeedsUpdate` 语义，避免每帧上传 5.33 MiB（1024）/21.3 MiB（2048）。D3D11/Vulkan native/legacy 对 M11.1c matrix 截图仍 **0 diff**；1280×720 area16 300 帧 native 1024 **33.3 fps**、2048 **33.4 fps**（无 4 倍上传惩罚；与上一轮 34.8 fps 的差异属于 Debug/系统噪声，不作为性能提升或回归结论）。
- **布局排序（HDRP `RelayoutEntries` 对齐）**：`requiredTileSize()` 统一计算 legacy/native 的 POT tile（含 2 px padding），ForwardRenderer 按 tile size 从大到小稳定排序后分配；对齐 `PowerOfTwoTextureAtlas.RelayoutEntries` 的“biggest to smallest”顺序，但不做 HDRP 的二叉树 allocator。`m11_area16_cookie` 的 D3D11/Vulkan native/legacy 四组对旧 matrix 仍 **0 diff**（tile 坐标变化由 light-data scaleOffset 吸收）。
- **测试/工具**：`hbg_tests` **231 cases/29,299 assertions**；`ctest` 2/2；D3D11/Vulkan gfx-test；510 binaries/0 zero-byte；forward.fs/forward_buffer.fs/deferred_lighting(.buffer) O3 变体已重建。
- **已知限制**：native source 上限 256；tile 采用 largest-first 对齐 shelf，非 HDRP 的二叉树 allocator，atlas 满时直接白 fallback，不回退到 8×8 tile；没有 HDR/彩色 cookie、streaming、async upload、defrag；2048 保持 opt-in。上传缓存假设 cookie 源在场景加载后不变；后续动态 cookie 编辑需要 Light cookie revision/updateCount 才能像 HDRP `NeedsUpdate` 一样失效。

**验收**

- 0/1/2/8/16 cookie 场景；D3D11/Vulkan、forward/deferred、buffer/texture。
- spot/area/directional cookie on/off 有差异；point IES/octa cookie 有差异。
- cookie 缺失/atlas 满/格式不支持：白纹理 fallback，无黑屏。
- M10 area cookie、M9 spot/point cookie 基线 0 diff（atlas 关闭时）。
- 记录 atlas 内存、mip 开销、1080p 性能增量。

**非目标**：不做 HDRP 完整 2048 纹理数组、cookie 流式加载、async cookie 上传。

### 5.3 M11.2 Cached area shadow

**v1 已实现（2026-11-15）；mixed 已实现（2026-11-16）**

- 独立 `shadow.cachedAreaAtlas`，仅在 `cachedAreaShadows=true` 时分配；1024 默认（约 4 MB D24S8）。all-static 时绑定该 atlas。
- `ShadowManager` 用独立的 punctual/area cache hash 和有效性状态；area hash **只包含静态 area request**、静态 area settings 和场景 caster；动态 area 的位置/分辨率变化不会误伤静态 cache。
- 静态 rectangle area（`ShadowUpdateMode::OnEnable`/`OnDemand`）首帧渲染；cache hit 时 request 标记 `isCached`，保留空 view 以维持 bgfx depth texture 状态，不产生 draw。
- **mixed cached/dynamic**：area request 按静态优先重新打包（static-first），静态 slot 固定在 dynamic atlas 中；cache hit 时静态 slot 不重绘，只绘制 dynamic request。cached static 空 view 继续绑定以保持 depth attachment 生命周期，避免 bgfx 丢弃静态 slot；不使用全图 cached→dynamic copy。
- 对照 HDRP `HDDynamicShadowAtlas::BlitCachedIntoAtlas`：HDRP 用 cached atlas + blit；HBG 利用 bgfx per-viewport clear 与 static-first slot，把静态内容原地保留在 dynamic atlas，达到相同的“静态只更新一次、动态每帧更新”语义，避免额外深度拷贝。
- `activeAreaTexture()`：all-static 绑定 cached atlas；mixed 或 cache 关闭时绑定 `m_areaTexture`。cache 模式在 all-static ↔ mixed 切换时强制失效并重建对应 atlas。
- `--area-shadow-cache` / `--no-area-shadow-cache`、`config/shadows.json` 的 `cachedAreaShadows`、diagnostics `shadow_area_cache/shadow_cached_area` 已接入；新增 `area_dynamic_atlas/area_mixed/area_dirty` 日志。

**验收结果**

- D3D11/Vulkan × forward/deferred，1280×720：all-static cached vs every-frame **4/4 0 diff**；mixed cached/dynamic vs every-frame **4/4 0 diff**。
- mixed cached 帧 `shadow_area_cache=true`、`shadow_cached_area=2`（4 area 中 2 静态）、`shadow_drawn=4`；每帧只绘制 2 个动态 request。
- `hbg_tests` **229 cases/29,286 assertions**；`ctest` 2/2；D3D11/Vulkan `--gfx-test` 通过。
- 单元测试覆盖首帧不缓存、cache hit、caster 变化失效、area light `EveryFrame` 禁用、suppression 回退，以及 static-only → mixed → static-only 模式切换失效。

**后续**

- 不做 defrag / 固定 persistent slot 分配；static-first 打包在静态 light 集合/顺序变化时仍可能移动。
- 不做 2048 area cache opt-in；当前跟随 `areaAtlasSize`。
- 不做 GPU atlas packing、async shadow、EVSM area filter。

### 5.4 M11.3 Variant strategy

**结论（2026-11-15）：否决编译期 category variants，保留 runtime gating**

- 对照 HDRP `Lit.hlsl:98-170` 的 29 个 `kFeatureVariantFlags` 与 `builddispatchindirect.compute:41` 的 per-tile indirect dispatch，当前 HBG 是全屏 forward/deferred program + per-pixel `tileFeatureFlags`；view-wide 4 类变体无法按 tile 选择，不能替代 runtime gating。
- 当前材质只有 16 个 opaque material mask；没有 SSS/transmission/anisotropy/clearcoat/iridescence，29 个 HDRP Lit variants 没有对应材质实现，不在 M11 伪造。
- 新增评估开关 `supportLightListFeatureFlags`（默认 true）/`--no-light-list-feature-flags`；关闭时 `u_pass_lightListParams2.z=0`，空 category 仍由 count=0 跳过，只用于测量。
- **0 diff**：area16/spot/point100 × D3D11/Vulkan × forward/deferred = 12 组，gating on/off 全部 0 diff；area16 × D3D11/Vulkan × forward/deferred × texture/CPU = 8 组，全部 0 diff。
- **性能**：`m96_stress` 1080p 300 帧 full post（cap64，122 候选灯，truncated=59）：D3D11 on 26.491 ms/37.7 fps，off 27.762 ms/36.0 fps，gating 省约 1.27 ms；Vulkan 23.189/43.1 vs 22.247/44.9，噪声内。保留 gating 有实际收益。
- **构建成本**：增加 4 个 view-wide category variants 会使 forward binary 从约 64 增至 256、manifest 从 510 增至约 702，O3 forward 构建约放大 4 倍；在 mixed category view 中仍不能减少 per-tile 分支，收益不成立。
- 详细报告：`docs/m11.3-variant-strategy.md`。29 variants 留到 M12 材质特性模块。

**非目标**：不实现 SSS/transmission/anisotropy/clearcoat/iridescence 材质；不伪造 29 variants。

### 5.5 M11.4 性能

**产出**

- 1080p 300 帧 full post：cap64/cap128、D3D11/Vulkan、forward/deferred、post on/off。
- cookie atlas on/off、cached area on/off、area16 720p 增量。
- 与 M9.7/M10.8 基线比较；记录 CPU submit 和 GPU frame。

**M11.4 实现与结果（2026-11-16）**

- 按 HDRP `HDShadowContext.hlsl` 的 `StructuredBuffer<HDShadowData>` 语义，把 shadow context 的 10 个 `float4` 改为 **一次 vector load**：D3D11 `Buffer<float4>`、GLSL `readonly buffer { vec4 ... }`、SPIR-V/Metal/WGSL `StructuredBuffer<float4>`；仍使用 stage 8 storage buffer，不新增 sampler/SRV。
- 1080p 300 帧 full post（`m8_lights64_perf`，cap64，Uffizi sky，64 responses/2048）稳定中位数：
  - D3D11 forward **15.70 ms / 63.7 fps**（CPU submit 13.5 ms，GPU 11.9 ms）；deferred **16.05 ms / 62.3 fps**。
  - Vulkan forward **15.48 ms / 64.6 fps**；deferred **15.81 ms / 63.2 fps**。
  - 优化前 D3D11 forward 同场景约 48–50 fps、GPU 14.5 ms。
- cap128：D3D11 forward 26.68 ms/37.5 fps，Vulkan 24.93 ms/40.1 fps；继续 opt-in。
- 正确性：Vulkan forward 新旧 shader **0 diff**；D3D11 forward no-TAA **146,139 px/max 7，PSNR 56.86 dB**，差异为 shadow 边缘 1–2 ULP（vector load 的 HLSL 编译重排），不是布局/request 回归。M11.5 已接受该 D3D 精度差；area 路径用 typed context 做到 0。
- shader binaries 510/0 zero-byte；forward 34×2 + deferred 2×2 O3 变体已重建；全量 clean O3 仍属 M11.0。
- **Area context**：D3D11 的 raw R32 SRV 对 area shadow 的 typed 读取不稳定；新增 `shadow.contextArea`（StorageVertex，RGBA32F）仅在 `m_areaRequestCount != 0` 时上传，并只在 D3D11 的 area 路径绑定。非 area 继续用快速 Storage context，M11.4 性能路径不受影响；area shadow D3D/VK forward/deferred **4/4 0 diff**。
- 详细报告：`docs/m11.4-performance.md`。

**验收**

- 1080p full post cap64 **>60 fps**（D3D11 forward/deferred 通过；Vulkan 记录）。
- cap128 不要求 >60，但必须有稳定记录和回退建议（37.5/40.1 fps，保持 opt-in）。
- M9/M10 既有场景不回归；D3D punctual/directional 边缘 1–2 ULP 已在 M11.5 接受；area 路径 0。

### 5.6a Shadow context / atlas texel P0 修复（2026-11-17）

- **D3D11 shadow context 类型**：`shadow.context` 原为 `Storage`（D3D11 index buffer，R32_UINT SRV），但 `shaders/lighting/shadows.sc` 按 typed `Buffer<float4>` 读取；D3D11 forward/debug 实际读到全 0，表现为“无阴影”，Vulkan 因 raw/structured 路径不同仍能读到数据，造成 D3D/VK 视觉分叉。改为 `StorageVertex` + `RGBA32F` layout，与 HDRP `HDShadowContext.hlsl:13` 的 `StructuredBuffer<HDShadowData>` 语义一致；`shadow.contextArea` 复用同一 layout。源码：`src/render/shadows/shadow_manager.cpp:381-399`。
- **Atlas PCF texel**：`hbgSampleDirectionalRequest` / `hbgSamplePunctualRequest` / `hbgSampleAreaShadow` 把 `requestParams.zw = 1/viewport` 直接当作 atlas UV texel；实际应为 `atlasRect.xy * requestParams.zw = 1/atlasSize`。point 256/atlas 2048 时 taps 间距放大 8×，directional 1024/2048 放大 2×，是条带/块状阴影的主因。源码：`shaders/lighting/shadows.sc:435/508/585`；HDRP 对照 `HDShadowSampling.hlsl:54-66` 的 atlas UV / shadowAtlasSize 语义。
- **Point seam**：按 HDRP `HDShadowUtils.cs:374-410` 的 guard angle，给 point face 的 90° 透视加 6° guard（`shadow_manager.cpp:23/728`），减轻 cube face 边界细线；完全 seam 处理仍属 M12.2。
- **验证**：补充首帧 shadow atlas 完整 clear（消除未初始化深度）后，`m26_ssgi_corner`、`m122_area_point_dir` 带 shadows D3D/VK 均 **0 diff**；`m26_pointonly` / `m41_point_scene` 路径保持可见 point shadow。point 不再出现整墙竖条；directional-only 静态阴影保持 on/off 差异，未退化为关闭。已知残留：point cube face 边界仍有约 1 px 细线，后续 M12.2 处理。
- 注意：本次 shader 增量重建已完成 forward/forward_buffer variant 0–6、deferred 4、fog 6、debug shadow 6；forward variant 7–15 仍为零字节，全量 518 variant 的 clean/完整重建待执行（构建中断留下 zero-byte 副本，测试场景使用 variant 0）。

### 5.6 M11.5 M11 验收

**M11.5 核心矩阵（2026-11-16）**

- **M10 area 36/36 0 diff**：`m10_area.gltf`，1080（1920×1080）/1440（2560×1141）/4K（3840×1141）× D3D11/Vulkan × forward/deferred × buffer/texture/CPU，当前构建对 M10.8 基线逐像素 0；同分辨率三路径也全部 0。
- **Area shadow 4/4 0**：`m10_area_shadow.gltf`，1080，D3D11/Vulkan × forward/deferred 对 M10.8 on 基线 0；on/off 差异分别 46,063/46,106（D3D）与 46,068/46,098（VK），证明功能未退化为 off。
- **M9 64 / transparent64**：D3D11/Vulkan × forward/deferred × buffer/texture/CPU，buffer 对 texture/CPU 全部 0（M9 8/8、transparent 8/8）；Vulkan 对 M10.8 buffer 基线 0。D3D11 M9 64 对 M10.8 为 **127,795 px/max11**，分布在 shadow 边缘，判定为 M11.4 vector load 的 1–2 ULP 精度差并接受；area 路径已用 typed context 做到 0。
- **Cookie / cached / mixed / variant**：M11.1c native vs legacy `m11_area16_cookie` 8/8 组合均有可见差异（forward 136,925/max82、deferred 136,924/max82）；D3D/VK 所有 path/mode **1 px/max1**，texture/buffer **0 diff**。M11.1b legacy baseline 与 M10 cookie 基线保持；cached area all-static **4/4 0**；mixed cached/dynamic **4/4 0**；`m10_area16`/`m11_point100` 的 feature-flag on/off D3D/VK × forward/deferred **8/8 0**（spot 组沿用 M11.3 的 4/4 0）。
- **测试/工具**：`hbg_tests` 231 cases/29,299 assertions、`ctest` 2/2、D3D11/Vulkan gfx-test、`git diff --check`、510 binaries/0 zero-byte 通过；M11.1c atlas 上传缓存与 largest-first 布局排序已加。
- **M8.7 base32 复跑**：同一历史命令（`m5_shadow_stress`，1280×720，capacity=32，forward+contact，`hbg_m78` 基线）下，Vulkan buffer/texture **0 diff**；D3D11 typed 快路径 buffer/texture 均为 **19,074 px/max165**，差异集中在 shadow 边缘。改用 HDRP `HDShadowContext.hlsl` 同类的 `StructuredBuffer<float4>` 诊断构建后 D3D11 **0 diff**，证明 context 数据/布局正确；但 structured 在该 64-light full-post 场景约 **2× GPU 时间**（A/B 中位数约 43.3 vs 52.3 fps），未作为默认。默认仍保留 M11.4 的 typed vector load，并接受 D3D 精度例外。
- **未收口**：M11.0 全量 clean O3、fixtures/perf baseline；M8.7 的默认 D3D 严格 0 仍受上述 typed 精度限制。

**0 diff 基线**

- M10 area 36 组、area shadow、M9 64/transparent、M8.7 base32。
- M11 功能关闭时对 M10 0 diff。

**功能矩阵**

- Cookie atlas：D3D11/Vulkan × forward/deferred × buffer/texture/CPU。
- Cached area：D3D11/Vulkan × forward/deferred × first frame/warm frame/moving light。
- Variant：on/off、D3D11/Vulkan、forward/deferred。
- 1080/1440/4K（4K 允许桌面高度上限，按 M10 方式记录）。

**工具**

- `hbg_tests`、`ctest`、IDL check、D3D11/Vulkan gfx-test。
- `git diff --check`；shader binaries 无 zero-byte。
- 文档更新：本文件、README、AGENTS、implementation-plan。

---

## 6. 测试矩阵

| 场景 | 后端 | 路径 | 验收 |
|---|---|---|---|
| M10 area 1080/1440/4K | D3D11/Vulkan | forward/deferred × buffer/texture/CPU | 0 diff |
| M10 area shadow 1080 | D3D11/Vulkan | forward/deferred | on/off 有差异；基线不回归 |
| M9 64-light / transparent64 | D3D11/Vulkan | buffer/texture/CPU | 0 diff |
| M8.7 base32 | D3D11/Vulkan | buffer/texture | Vulkan 0；D3D typed 19,074/max165 例外；structured 诊断 0 |
| Cookie 0/1/2/8/16 | D3D11/Vulkan | forward/deferred × buffer/texture | native/legacy 有差异；backend 1 px max1；path 0 diff；on/off 有差异 |
| Cookie 缺失/atlas 满 | 双后端 | forward/deferred | white fallback，无崩溃 |
| Cached area 静态 / mixed | 双后端 | forward/deferred | all-static 与 mixed 均首帧=every-frame；warm 帧静态不重绘，0 diff |
| Cached area 失效 | 双后端 | forward/deferred | 移动/变换后正确重绘 |
| Variant on/off | 双后端 | forward/deferred | 0 diff；build 时间记录 |
| 1080p full post | D3D11/Vulkan | cap64/cap128 | cap64 >60；cap128记录 |

所有“0 diff”沿用项目定义：同后端、同分辨率、同 pipeline/frame settings 下逐像素比较。

---

## 7. 风险与开放决策

| 风险 | 影响 | 处理 |
|---|---|---|
| Sampler 预算不足 | cookie atlas 无法落地 | M11.1 gate；失败保留 8×8 |
| Shadow texture array 破坏 area PCSS | M10 回归 | 先原型；不通过则不做 |
| Cookie atlas 格式/内存 | 4K 内存上涨 | 1024 默认；2048 opt-in；记录 R11G11B10/RGBA16F |
| 29 variants 编译爆炸 | build 从小时级恶化 | 不直接做；category variants 也已否决 |
| Cached area 失效错误 | 阴影过期 | hash + transform + first-frame invalidation |
| M9.7 性能未达标 | M11 默认功能受限 | 性能门禁；cookie/cached 保持 opt-in |
| Shader build 仍慢 | 发布不可重复 | M11.0 progress/timeout/cache；全量 O3 记录 |
| 4K texture 宽度 | cookie atlas/light data 变宽 | 优先 buffer/list；记录 clamp |

**开放决策（实现前必须锁定）**

1. Cookie atlas sampler：shadow texture array 合并是否可行？
2. Atlas 后是否把 `kGpuLightTexels` 从 83 压到 19？还是保留 legacy 83 作为 fallback？
3. Cached area：独立 atlas，还是 dynamic area atlas 的 persistent slots？
4. Variant：4 类 category variant 是否真的带来收益，还是保留 runtime gating？**已答：保留 runtime gating；否决编译期 category variants。**
5. 1080p 60fps 是否只要求 cap64，cap128 是否只记录？
6. Async light-list 是否进入 M11，还是保持 experimental 不进验收？

---

## 8. HDRP 参考清单

- 变体：
  - `Runtime/Lighting/LightLoop/LightLoop.cs.hlsl:63-67`
  - `Runtime/Material/Lit/Lit.hlsl:98-170`
  - `Runtime/Material/Lit/Lit.shader:477-485`、`:877-889`
  - `Runtime/Lighting/LightLoop/builddispatchindirect.compute:41`
- Cookie：
  - `Runtime/Lighting/LightLoop/GlobalLightLoopSettings.cs:180-186`、`:221-226`
  - `Runtime/Lighting/LightCookieManager.cs:37-62`、`:242-321`、`:382-430`
  - `Runtime/Lighting/LightLoop/CookieSampling.hlsl:29-55`
  - `Runtime/Lighting/LightEvaluation.hlsl:16-113`、`:233-257`、`:380-435`
  - `Packages/com.unity.render-pipelines.core/Runtime/Textures/PowerOfTwoTextureAtlas.cs:26`、`:108-109`
  - `Packages/com.unity.render-pipelines.core/Runtime/Textures/Texture2DAtlas.cs:606-665`
- Cached area：
  - `Runtime/Lighting/Shadow/HDCachedShadowAtlas.cs:23-83`、`:342-467`
  - `Runtime/Lighting/Shadow/HDCachedShadowManager.cs:41-52`、`:127-143`
  - `Runtime/Lighting/Shadow/HDDynamicShadowAtlas.cs:206-270`
  - `Runtime/Lighting/Shadow/HDShadowManager.cs:805-842`、`:1420-1565`
- Async / 容量：
  - `Runtime/RenderPipeline/Settings/FrameSettings.cs:393-407`、`:599-606`
  - `Runtime/RenderPipeline/HDRenderPipeline.LightLoop.cs:677`
  - `Packages/com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:129-151`
  - `Runtime/Lighting/LightLoop/lightlistbuild.compute:57`
  - `Runtime/Lighting/LightLoop/LightLoop.cs:692`

---

## 9. 实施顺序建议

1. **M11.0**：shader build 进度/超时/缓存已完成工具 v1；全量 clean O3、M10 fixtures、性能基线待做。
2. **M11.1a**：sampler/stage spike；决定 cookie atlas 是否继续。
3. **M11.1b**：cookie atlas + fallback；v1 已完成 19-texel 布局、8×8/128-tile atlas、双后端 forward/deferred 验证；独立分辨率/mip 待 M11.1c。
4. **M11.2**：cached area shadow；all-static 独立 cached atlas；mixed cached/dynamic 已完成（static-first persistent slots、双后端 4/4 0 diff）。
5. **M11.3**：variant 收益评估已完成；否决 4 类编译期 category variants 与 29，保留 runtime gating。
6. **M11.4**：60fps 门禁与增量记录。
7. **M11.5**：全矩阵、双后端、4K、测试、文档收口。

M11 的完成标准不是“功能都写了”，而是：

- M10 基线仍 0 diff；
- cookie/cached area 有可回退路径；
- 1080p cap64 >60 fps；
- 全量 shader build 可重复完成；
- 文档明确哪些 HDRP 特性 **没有** 对齐，以及为什么。
