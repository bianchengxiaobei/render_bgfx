# M10 HDRP Lighting 2.1 规划：Area Lights / IES / Cookies

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M10.0–M10.8 已完成**（2026-11-15；M9.7 性能、官方 shader_build O3 与跨后端少量精度差异作为已知限制记录）。
> 实现记录：83 texel / LTC 表通过 light-data 纹理追加行读取；Rectangle 使用 LTC GGX + `PolygonIrradiance`，Tube 使用 HDRP line LTC，Disc 明确 unsupported；Area/Directional cookie、point IES、spot IES cutoff 与 tile feature flags 已接通。M10.7 新增 rectangle-only 独立 area shadow atlas：1024 D24S8、512 默认分辨率、spot-like cone、HDRP 风格 area PCSS；sidecar 仅在 rectangle 且显式 `shadow` 时启用。
> 前置：M9.0–M9.6 核心完成；M9.7 视觉/容量矩阵已完成，1080p full-post 性能仍为已知限制。
> 原则：先读 HDRP 与 vendored bgfx 源码再实现；M10 关闭时保持 M9/M8.7 基线 0 diff；不把 M9 的 8×8 cookie 临时方案直接扩展成新的 atlas 大爆炸。

---

## 0. 结论摘要

M10 不是继续加灯数，也不是一次性复刻 HDRP 全部 29 个 material variants。M10 的主题是：

**把 M9 预留的 Area category、Directional cookie 和 point IES 真正接通，同时给 area/directional 增加 LTC 与可用的 cookie scale/offset。**

M10 的核心目标：

1. **Rectangle area light**：Rectangle LTC 光照，Lit GGX specular + Lambert diffuse；forward/deferred 都可用。
2. **Area cookie**：复用 M9 的 8×8 cookie 数据，按 LTC hit UV 采样；支持 clamp/repeat、scale/offset。
3. **Directional cookie**：支持 directional light 的 2D cookie，按 light right/up 正交投影采样。
4. **Point IES**：LM-63 的 horizontal/vertical 二维分布，烘焙为 point octahedral 8×8，与 point cookie 相乘；不增加新的 IES texel。
5. **Tile feature flags**：把 CPU 已有但未上传的 per-tile feature flags 接到 GPU producer/consumer，让 punctual/area/directional 的循环可以跳过，先不做 29 个静态 material variants。
6. **Area 数据契约**：新增 right/up/half-size/cookieScaleOffset，保持 M9 旧场景视觉 0 diff。

M10 **不包含**：

- 完整 HDRP `kFeatureVariantFlags` 的 29 个 Lit variants；只在 M10 做 tile flags 和运行时 category gating，静态 variants 留 M11。
- 256/512 lights；`kMaxGpuLights` 仍为 128，16-bit index 不扩。
- GPU shadow atlas packing；HDRP 本身也是 CPU/Burst request + dynamic atlas。
- Cached area shadow atlas、EVSM area filter、screen-space area shadow；M10.7 只做 dynamic rectangle atlas + PCSS。
- Disc area light 的完整 LTC；M10.6 stretch，核心先 rectangle，tube 作为 line LTC。
- 完整 cookie atlas、HDR cookie、mip/padding、16×16 以上 cookie；M10 保持 8×8 per-light 数据，cookie 质量提升另立 M11。
- 新的材质类型（Fabric/Eye/Hair/AxF）area LTC；M10 只覆盖当前 Lit 材质。

### 0.1 与 M9 的边界

- M9 已经把 4 个 category 的**布局**做好，但只有 Punctual 层 active；M10 把 Area 层接上。
- M9 的 cookie 是 8×8 亮度样本，放在 light-data texel；M10 继续用这个存储，但增加 scale/offset 和 area/directional 采样语义。
- M9 的 point cookie 是 octahedral 8×8；M10 的 point IES 会**乘进同一组 8×8 样本**，不再追加 64 个 IES texel。
- M9.7 的性能门禁尚未满足；M10 不允许以“area 更快”为借口跳过 M9 的 55.2 fps 已知限制记录。

---

## 1. 当前基线与缺口

### 1.1 当前 light data

- `src/render/lighting/gpu_light_data.h`：`kGpuLightTexels=80`；8 base texels + 8×8 cookie（texel 8–71）+ 8 IES 样本（texel 72–79）。
- `GpuLightType` 只有 `Directional/Point/Spot`；`LightType` 也只有 `Directional/Point/Spot`。
- `src/render/scene/light.h`：没有 area shape、width/height、area shadow cone；cookie 只有 `Spot2D/PointOctahedral`。
- `src/asset/scene_loader.cpp`：`cookie` 只接受 spot/point；directional cookie 直接 warning 并忽略；IES 只取 horizontal=0 的一组 vertical slice。
- `shaders/common/light_data.sc`：texel7 的 `cookieScaleOffset` 实际只用了 mode/type/hasIes；没有 scale/offset；没有 area LTC。
- `src/render/lighting/light_list_builder.cpp`：CPU 生成 `tileFeatureFlags`，但 GPU producer 没有输出，shader 也没有绑定；consumer 只跑 Punctual category。
- `src/render/lighting/light_builder.cpp`：所有 punctual 都写 `LightCategory::Punctual` 和 `kLightFeaturePunctual`；没有 Area。

### 1.2 当前性能/资源约束

- M9.7 1080p 300 full post：D3D11 cap64 **55.2 fps**、cap128 **29.2 fps**；M9.7 仍为 `[~]`。
- 4K/cap128 light-list 约 37.3 MB；4096 shadow atlas 3×D24S8 约 192 MiB。
- D3D11 `maxTextureSamplers=16` 已满；M9.4 因此没有第 5 个 GBuffer MRT。
- 因此 M10 的 LTC 数据不能默认再占一个 texture sampler。计划使用 **StructuredBuffer/Buffer 形式的 LTC LUT**，与 light-list buffer 同类；如果后端不支持，必须显式 fallback，不允许静默关闭 area。

### 1.3 已知未覆盖项（M9 留下）

- Area light LTC、area shadow。
- Point IES；当前只支持 spot IES。
- Directional cookie；当前 loader warning 后忽略。
- Cookie scale/offset/repeat 的真实 tiling 测试；M9 的 spot UV 始终在 `[0,1]`，clamp/repeat 截图为 0 diff。
- 29 material feature variants；M9 只有 flags 数据和 CPU 侧 tile flags。
- GPU shadow atlas packing/async compute；M9 已明确不是 HDRP 对齐项，另立研究。

---

## 2. HDRP 源码结论

### 2.1 Area light 数据与评估

HDRP 的 area light 使用 `LightData` 中的 `right/up/size/forward/rangeAttenuation*`，在 Lit 材质里走 LTC：

- `Runtime/Lighting/LightDefinition.cs:70-90`：`right/up`；`size` 在 `:190`（rectangle 为 width/height；punctual 为 radius）。
- `Runtime/Lighting/LightEvaluation.hlsl:96-176`：`EvaluateLTC_Rect`、`EvaluateLTC_Area`；`PolygonIrradiance`、`ComputeLineWidthFactor`、`I_diffuse_line`。
- `Runtime/Material/Lit/Lit.hlsl:1553-1730`：`EvaluateBSDF_Area`；rectangle 走 LTC，tube 走 line；area cookie 通过 `SampleAreaLightCookie`。
- `Runtime/Material/LTCAreaLight/LTCAreaLight.hlsl:6-75`：`_LtcData` 纹理数组、`SampleLtcMatrix`、64×64 LUT；`LTCAreaLight.cs.hlsl:10-18` 为 8 个 BRDF model。

M10 只取当前 Lit 材质需要的最小集：

- **Diffuse**：Lambert + `PolygonIrradiance`（不引入 Disney/Charlie 的 diffuse LTC）。
- **Specular**：GGX 的 LTC 矩阵（只生成 GGX 一个 64×64 表）。
- 不实现 clear coat、SSS、transmission、anisotropy、fabric/eye/hair 的 area 评估。

### 2.2 Rectangle / Tube / Disc

- HDRP 的 `LightType.Rectangle/Disc/Tube` 是 area；`HDAdditionalLightData.cs:2905-2907` 设置 lumen 默认强度。
- `HDAdditionalLightData.cs:3460-3471`：Rectangle 使用 `AreaLightAtlas`，其他 punctual 使用 `PunctualAtlas`。
- `Lit.hlsl:1567` 只有 `GPULIGHTTYPE_RECTANGLE` 走 rectangle LTC；非 rectangle 走 line LTC。
- `HDGpuLightsBuilder.LightLoop.cs:147-196`：Rectangle/Tube/Disc 使用 area cookie（`FetchAreaCookie`），DS 模式 clamp。

M10 核心先做 **Rectangle**；Tube 作为 line LTC（M10.6）。Disc 不做完整 LTC，只在 sidecar 中标为 unsupported/近似，避免把“disc 当 line”当成已完成。

### 2.3 Area cookie

- `Runtime/Lighting/LightEvaluation.hlsl:16-113`：`SampleAreaLightCookie` 用 LTC 变换后的四个角点求 hit UV，按 roughness 选 mip。
- `Runtime/Lighting/LightCookieManager.cs:297-321`：`FetchAreaCookie` 把 area cookie/IES 过滤成 octahedral/2D atlas。
- `Runtime/Lighting/LightLoop/CookieSampling.hlsl:29-55`：2D cookie 的 padding/atlas scaleOffset；point 用 octahedral。

M10 没有独立 cookie atlas，因此不复制 HDRP 的 padding/mip 逻辑；只保留：

- 用 LTC 角点求 UV；
- 用 `cookieScaleOffset` 做 tiling；
- 8×8 数据做 nearest 采样（保持 M9 的存储）；
- 缺 cookie/解析失败回退。

### 2.4 Directional cookie

- `Runtime/Lighting/LightEvaluation.hlsl:233-257`：`EvaluateCookie_Directional` 把 world offset 变换到 light space，`positionCS.xy` 正交投影，`[-1,1]` 映到 `[0,1]`，repeat 时 `frac`。
- `Runtime/Material/Lit/Lit.hlsl` 的 directional 评估在 `EvaluateLight_Directional` 中乘 cookie。
- `LightDefinition.cs` 的 `right/up` 对 directional 也是预缩放基。

M10 需要：

- directional light 的 sidecar `cookie` 不再 warning；
- light data 写入 right/up（单位正交基）和 cookieScaleOffset；
- shader 用 `dot(worldOffset, right/up)` 投影；repeat 用 `frac`，clamp 用 `saturate`。

### 2.5 Point IES / spot IES

- `Runtime/Lighting/Light/HDAdditionalLightData.cs:480-551`：`IESPoint` 是 cubemap，`IESSpot` 是 2D；`IESTexture` 按 light type 分派。
- `Runtime/Lighting/LightCookieManager.cs:242-272`：2D spot/area cookie + IES 通过 `Fetch2DCookie` 合并。
- `Runtime/Lighting/LightCookieManager.cs:405-430`：point IES 用 `FetchCubeCookie`，最终 `BlitOctahedralTexture` 到 octahedral。
- `CookieSampling.hlsl:47-55`：point cookie 使用 `PackNormalOctQuadEncode` 到 2D。

M10 的 point IES 不走 cube atlas，也不新增 texel：

- CPU 把 LM-63 的 horizontal/vertical 二维分布按 8×8 octahedral 方向重采样；
- 若同时有 point cookie，逐样本相乘；
- 结果写入 M9 已有的 64 个 cookie 样本；
- shader 仍走 `PointOctahedral` cookie 路径。
- Spot IES 保留现有 8 个 0–90° 样本，新增 `iesCut`（来自光锥/IES cutoff）用于提前截断。

### 2.6 Tile feature flags / variants

- `Runtime/Lighting/LightLoop/LightLoop.cs:76-87`：`LightFeatureFlags`：Punctual/Area/Directional/Env/Sky/SSRefraction/SSReflection。
- `Runtime/Lighting/LightLoop/LightLoop.cs:120-122`：opaque/transparent 各自去掉一个 bit。
- `Runtime/Material/Lit/Lit.hlsl:97-118`：29 个 `kFeatureVariantFlags` 组合（Lit standard/SSS/anisotropy/clearcoat/iridescence...）。
- `Runtime/Lighting/LightLoop/LightLoop.hlsl:212-325`：按 feature flags 分支 directional/punctual/env。

M10 只做 **tile flags + 运行时 category gating**：

- GPU producer 把 tile feature flags 写入独立 buffer/texture；
- shader 每像素只读一次 tile flags；
- 若 tile 没有 Area，不进入 area 循环；没有 Punctual，不进入 punctual 循环；
- 暂不生成 29 个 Lit 静态 variants。完整 variants 需要单独评估编译时间和内存，留 M11。

### 2.7 Area shadow

- `Runtime/Lighting/LightEvaluation.hlsl:587-646`：`EvaluateShadow_RectArea`；需要 area shadow atlas、PCSS/area filter。
- `HDShadowManager.cs:805-842`：独立 `AreaLightShadowAtlas`，与 punctual/directional 分开。
- `HDShadowLoop.hlsl:185`：area 的 shadow loop。

因此 area shadow 不是“给 spot shadow 改个 type”：

- 需要独立 area atlas、rectangle 的 spot-like 视锥、area PCSS；
- 需要额外 sanity/内存预算。

M10 核心 **不承诺 area shadow**；M10.7 作为 stretch，只有在 M10.1–M10.5 已稳定后才做 rectangle-only 近似。

### 2.8 HDRP 没有 GPU shadow atlas packing

- HDRP 的 shadow 仍是 CPU/Burst request + dynamic/cached atlas（`HDShadowCullingUtils.cs`、`HDShadowManager.cs`）。
- M10 不把 GPU atlas packing 写成 HDRP 对齐项；若要做，另立 M11 研究项，验收只能是“实验路径”而非默认。

---

## 3. M10 范围与关键决策

### 3.1 范围表

| 项 | M10 核心 | M10 stretch | 说明 |
|---|---|---|---|
| Rectangle area light | `[x]` | | LTC GGX spec + Lambert diffuse |
| Tube area light | | `[x]` | HDRP line LTC；height 忽略 |
| Disc area light | | `[x]` | sidecar warning + intensity=0；不冒充 rectangle |
| Area cookie | `[x]` | | 8×8，clamp/repeat/scaleOffset |
| Directional cookie | `[x]` | | 2D orthographic |
| Point IES | `[x]` | | 8×8 octa，复用 cookie 样本 |
| Spot IES cutoff | `[x]` | | 现在只有 8 角度样本 |
| Tile feature flags | `[x]` | | GPU producer + shader gating |
| 29 Lit variants | |  | M11；M10 只做 flags |
| Area shadow | | `[ ]` | rectangle-only，独立 atlas |
| GPU shadow atlas/async | |  | M11 研究 |
| 256 lights | |  | M11，16-bit 上限另有决策 |

### 3.2 关键决策

1. **Area authoring 不新增 glTF extension**：用现有 point/spot node 提供 transform/color/intensity，sidecar `lights[].type="area"` 覆盖类型；`shape/width/height/range/areaShadowCone` 由 sidecar 提供。
2. **LTC 只做 GGX + Lambert**：不引入 HDRP 的 8 个 BRDF model；只生成 GGX 64×64 表。
3. **LTC 使用 buffer，不占新 sampler**：D3D11 16 sampler 已满；使用 `StructuredBuffer<float4>`/`Buffer<float4>` + 手动双线性。若某后端不支持 fragment buffer，必须显式 fallback 并记录，不静默把 area 变成黑色。
4. **Point IES 复用 cookie texel**：8×8 octa 数据与 point cookie 合并，不增加 `kGpuLightTexels`。
5. **M9 的 8×8 cookie 继续用**：M10 不做 atlas/16×16；提高 cookie 质量另立 M11。
6. **Area shadow 延后**：核心 area light 可以 `castShadows=false`；M10.7 才做 rectangle area shadow。
7. **M10 不做 29 variants**：tile flags 先把 category 循环裁掉；静态 variants 另立。
8. **默认值不变**：M10 新增能力默认不改变 M9 场景；area 只在 sidecar 显式指定时生效。

---

## 4. 数据契约

### 4.1 GpuLightData 扩展（建议值）

M9 当前 80 texels。M10 增加 3 个 base texel（right/up/cookieScaleOffset），总 **83 texels**：

| texel | 内容 | 说明 |
|---|---|---|
| 0 | position.xyz, range | 不变 |
| 1 | color.rgb, intensity | 不变 |
| 2 | direction.xyz, cosOuter | area 用 direction 作 forward |
| 3 | shadowIndex, cosInner, contactMask, flags | 不变 |
| 4 | shadowTint.rgb, shadowDimmer | 不变 |
| 5 | shadowFadeDistance, penumbraTint, iesCut, areaShadowCone | z/w 新用 |
| 6 | lightLayerMask, category, featureFlags, unused | area 写 category=Area、feature=Area |
| 7 | cookieMode, cookieType, hasIes, areaShape | w 可用；areaShape 也可只靠 type |
| 8 | right.xyz, halfWidth | directional 用正交基；area 用 half width |
| 9 | up.xyz, halfHeight | tube 的 halfHeight=0；directional 可写 0 |
| 10 | cookieScale.xy, cookieOffset.xy | scale/offset |
| 11–74 | 8×8 cookie 样本 | 原 texel 8–71 后移 |
| 75–82 | 8 个 IES 角度样本 | 原 texel 72–79 后移 |

常量更新：

```text
kLightCookieBaseTexel = 11
kLightIesBaseTexel    = 75
kGpuLightTexels       = 83
```

注意：

- 旧场景 0 diff：cookie/IES 偏移、shader 解包、CPU writer、debug 同时改；不能只改一边。
- texture 宽度仍按 4096；128×83=10624，高度增加，4K/cap128 仍需验证 pitch/带宽。
- `texel7.w` 当前 unused；如果后续要 barn door，可在这里或 texel5 预留，不在 M10 核心。

### 4.2 Light type / sidecar

建议枚举：

```text
LightType: Directional, Point, Spot, Area
AreaShape: Rectangle, Tube, Disc
GpuLightType: Directional=0, Point=1, Spot=2,
              AreaRectangle=3, AreaTube=4, AreaDisc=5
```

Sidecar 草案（用 point/spot node 提供 transform）：

```json
{
  "lights": [
    {
      "name": "areaLamp",
      "type": "area",
      "shape": "rectangle",
      "width": 2.0,
      "height": 1.0,
      "range": 8.0,
      "intensity": 200.0,
      "cookie": "area_cookie.png",
      "cookieMode": "clamp",
      "cookieScale": [2.0, 2.0],
      "cookieOffset": [0.0, 0.0],
      "ies": "area.ies",
      "areaShadowCone": 120.0
    }
  ]
}
```

规则：

- `type:"area"` 只覆盖已有 light 的 type；node 的 world transform 提供 position/right/up/forward。
- Area shadow 仅对 rectangle 且 sidecar 显式提供 `shadow` 对象时启用；`areaShadowCone` 单位为度，默认 120、clamp 10–179；tube/disc 的 `shadow` 会被禁用并 warning。
- `shape` 缺省 rectangle；`width/height` 米；Tube 用 width 作长度，height=0；Disc 只在 M10.6/stretch。
- `cookie` 对 area/directional 也可用；directional 不再 warning。
- `ies` 对 point/spot/area：point 走 8×8 octa，spot/area 走 8 角度。
- 缺失/无效 cookie/IES：warning，回退 None；不影响其他灯。
- `cookieScale/Offset` 默认单位变换；repeat 配合 scale>1 才产生可见 tiling。

### 4.3 LTC buffer

- 表内容：GGX 的 64×64×4 系数（`m00,m02,m11,m20`），来自 HDRP `LtcData.BRDF_GGX.cs` 的 FP16 数据；生成脚本 `tools/gen_ltc_lut.py`，输出 `assets/ltc/ggx_64x64.bin` 或生成头文件。
- 运行时：`StructuredBuffer<float4> s_ltcData`（或后端等价 buffer），`hbgSampleLtcMatrix(roughness, ndotv)` 手动双线性；不使用 `SAMPLE_TEXTURE2D_ARRAY_LOD`。
- 绑定：不占 texture sampler；具体 binding slot 在实现时锁定，必须与现有 light-list buffer 不冲突。
- 校验：LTC 表的 4 个系数每个网格点与 HDRP 表一致；双线性边界 clamp；不能读取越界。
- 许可/来源：文档记录表来源与生成命令；不把 HDRP 的 C# 生成器作为运行时依赖。

### 4.4 Shader 入口

- `direct_lighting.sc` / `light_loop.sc` 增加 `hbgEvaluateAreaLight(lightIndex, surfaceData, worldPosition, ...)`。
- `light_loop.sc` 按 category 循环：
  - directional：现有；
  - Punctual：现有 `hbgEvaluateLightData`；
  - Area：LTC，只对 `AreaRectangle`（M10 核心）；
  - 其他 category 空层不进入。
- `HbgLightData` 增加 `rightUp0/rightUp1/cookieScaleOffset`。
- cookie 采样：
  - spot：沿用现有 2D/IES；
  - point：octa；
  - directional：正交投影；
  - area：LTC hit UV + scale/offset。

---

## 5. 任务拆分

| 任务 | 产出 | 状态 |
|---|---|---|
| **M10.0 契约/基线/authoring** | sidecar type 覆盖、area shape、83 texel 布局、LTC 数据生成方案、M9 基线冻结 | `[x]` |
| **M10.1 Area 数据/裁剪/list** | `LightType::Area`、`GpuLightType` area、screen AABB、CPU/GPU Area category 双层 | `[x]` |
| **M10.2 Rectangle LTC** | LTC table、`PolygonIrradiance`、Lambert diffuse + GGX spec、Lit forward/deferred 接入 | `[x]` |
| **M10.3 Area/Directional cookie** | area/directional UV、cookieScaleOffset/repeat/clamp、sidecar | `[x]` |
| **M10.4 Point IES / spot cutoff** | LM-63 二维重采样、octa 8×8、与 point cookie 合并、spot IES cutoff、fallback | `[x]` |
| **M10.5 Tile feature flags** | GPU producer 写 flags、shader 绑定、category loop gating、无 29 variants | `[x]` |
| **M10.6 Tube/Disc（stretch）** | Tube line LTC；Disc 明确 unsupported | `[x]` |
| **M10.7 Area shadow（stretch）** | rectangle area atlas、area shadow request/PCSS、fallback | `[x]` |
| **M10.8 M10 验收** | 0 diff 基线、双后端、forward/deferred、测试/IDL/docs | `[x]` |

### 5.1 M10.0 契约/基线

**产出**

- sidecar `type/shape/width/height/range/cookieScale/cookieOffset/areaShadowCone` 解析与校验。
- `GpuLightType`/`LightType`/`AreaShape` 枚举和 `static_assert`。
- 83 texel 布局常量、CPU writer、shader 解包、debug view 同步。
- LTC 数据生成脚本/资产；记录来源、hash、生成命令。
- M9 旧场景基线截图（M9 关闭 area/cookie scale 时 0 diff）。
- 单测：
  - area sidecar 解析/非法 shape/缺 width；
  - 83 texel offset 与 cookie/IES 边界；
  - LTC 表尺寸/索引/边界 clamp。

**不做**：area 光照、area shadow、directional cookie 行为；M10.0 只把契约和基线锁定。

### 5.2 M10.1 Area 数据/裁剪/list

**产出**

- `Light`：`AreaShape`、`width/height`、`areaCookie`（复用 cookie）、`areaShadowCone`。
- `LightBuilder`：area 写 `GpuLightType::AreaRectangle`、`LightCategory::Area`、`kLightFeatureArea`；point/spot/directional 不变。
- `LightListBuilder`：Area category 的 FPTL/big-tile/cluster 行；list 只收集 category 匹配的灯。
- screen bounds：M10 收口后已按 HDRP `HDGpuLightsBuilder.Jobs.cs` 的 Rectangle/Tube OBB 实现；Point/Spot/Disc 保持保守球 fallback。
- GPU producer：`light_screen_aabb`、`light_fptl`、`light_big_tile`、`light_cluster` 认 area type；area 不进入 point 6-face shadow 逻辑。
- debug：`--debug-view=light-data` 显示 area shape/category；`--debug-view=light-cluster` 能选中 Area slice。
- 测试：area on/off 的 list 数量、category offset、CPU/GPU count/index 一致；旧场景 0 diff。

**验收**

- 只有 sidecar 指定 area 时才出现 area list；旧 scene 不分配额外 list 或只分配空层，结果 0 diff。
- Area category 的 FPTL/big-tile/cluster offset/count 与 CPU reference 一致。

### 5.3 M10.2 Rectangle LTC

**产出**

- `tools/gen_ltc_lut.py`：从 HDRP `LtcData.BRDF_GGX.cs` 生成 64×64 GGX 表。
- `src/render/lighting/ltc_lut.h/.cpp`：buffer 创建/销毁、绑定、静态数据。
- shader：`hbgSampleLtcMatrix`、`hbgPolygonIrradiance`、`hbgEvaluateAreaLight`。
- Lit forward/deferred：area 只走 `EvaluateBSDF_Area` 的最小实现（diffuse Lambert + GGX spec）。
- 边界：roughness clamp、NdotV clamp、light 在表面后/下方、退化矩形、NaN 防护。
- 测试：
  - LTC 表单点/双线性与 HDRP 表一致；
  - 矩形 irradiance 对 CPU reference（同一多边形积分）误差阈值；
  - D3D11/Vulkan、forward/deferred、buffer/texture/CPU 0 diff；
  - area on/off 截图有差异，不能全黑/全白。

**非目标**：clear coat、SSS/transmission、anisotropy、fabric/eye/hair。

### 5.4 M10.3 Area/Directional cookie

**产出**

- area cookie：用 LTC 变换后的角点求 hit UV，`cookieScaleOffset` 变换后 clamp/repeat；8×8 nearest 采样。
- directional cookie：`right/up` 正交投影、`frac`/`saturate`、scale/offset。
- loader：允许 directional `cookie`；area `cookie`；`cookieMode` 默认 clamp；`cookieScale/Offset` 解析。
- shader：`hbgSampleAreaCookie`、`hbgSampleDirectionalCookie`；无 cookie 时 1.0。
- 测试：
  - directional cookie on/off、rotate light 后有差异；
  - area cookie on/off、scale=2 + repeat 有可见 tiling（补 M9 的 repeat 0 diff 缺口）；
  - 缺文件/损坏图片 warning + 回退，0 diff；
  - 双后端、forward/deferred。

### 5.5 M10.4 Point IES / spot cutoff

**产出**

- `loadIesSamplesPoint`：读取 LM-63 的 vertical/horizontal 二维数据；按 8×8 octa 方向插值；归一化。
- 与 point cookie 合并：`cookieSamples[i] *= iesSamples2D[i]`；`cookieMode=Clamp`；`cookieType=PointOctahedral`。
- spot：保留 8 个角度样本；新增 `iesCut`（从 IES/光锥计算）并在 shader 中提前截断。
- 测试：
  - 水平非对称 IES 的 point 场景，旋转 light 后截图差异符合方向；
  - point cookie + IES 叠加差异；
  - 无效/缺文件回退 0 diff；
  - 不新增 texel，83 布局不变。

**非目标**：cube atlas、point IES 的高分辨率/多 mip；8×8 结果与 HDRP cube 近似即可。

### 5.6 M10.5 Tile feature flags

**产出**

- GPU producer 输出 `tileFeatureFlags`（FPTL/big-tile/cluster），与 CPU `LightListBuilder::tileFeatureFlags` 对齐。
- 绑定到 shader（buffer/texture；不新增 sampler）。
- `hbgEvaluateLightLoop`：读一次 tile flags；无 Punctual 跳过 punctual，无 Area 跳过 area；directional 保留。
- 测试：
  - CPU/GPU flags 在 32/64/128 灯、area on/off 下一致；
  - gating 前后截图 0 diff；
  - 性能：area off 不慢于 M9；area 1 灯记录增量。

**非目标**：29 个 `kFeatureVariantFlags` 静态 variants；M11 再评估。

**实现记录（2026-11-15）**

- FPTL 把每个 16×16 tile 的 flags 写到 category0 list 行最后一个 dword 的高 16 位；CPU `LightListBuilder` 同步写入，shader 用同一个 `s_lightList` 读取，不新增 sampler/buffer。
- Big-tile 把 64×64 flags 写到 category0 list 行最后一个 dword 的高 16 位；cluster producer 再把它复制到 cluster list 尾部（每个 cluster tile 一个 dword）。
- `hbgEvaluateLightLoop` 每像素读一次 flags：directional 保留，punctual/area 按 category 位跳过；FPTL 与 cluster 两条路径都生效。
- 验证：area forward D3D11/Vulkan 与 gating 前 **0 diff**；area deferred D3D11 GPU/CPU **0 diff**；transparent D3D11 GPU/CPU **21 像素 1 LSB**、Vulkan **0 diff**；M9 64-light 当时为 **49 像素 1 LSB**；cap32 **0 diff**、cap128 **1 像素 1 LSB**。M10.8 最终复跑 M9 64-light/transparent 三路径均 **0 diff**，见 5.9。
- 720p Debug 60 帧：area on 约 **33.8 fps**、area off 约 **35.8 fps**；gating 有收益但该场景仍受 CPU submit 限制。
- **GPU flags readback（2026-11-15）**：`--cluster-stats-readback` 现在同时读回 FPTL/big-tile 聚合后的 cluster feature flags，并与 CPU `LightListBuilder` reference 逐 tile 比较；D3D11/Vulkan × buffer/texture 在 area16 场景均 `feature_flags_checked=240`、`feature_flag_mismatch=0`，64-light 场景同样 `checked=240/mismatch=0`。readback 只用于诊断，不参与正常渲染。

### 5.7 M10.6 Tube/Disc（stretch）

- Tube：line LTC（`I_diffuse_line`/`ComputeLineWidthFactor`），`halfHeight=0`；HDRP line 分支不采 area cookie。
- Disc：先明确 unsupported；若做，按 HDRP 实际路径确认（不能把 line 当 disc 冒充）。
- 验收：Tube sidecar/list/LTC 通过；Disc 至少 warning，不崩溃；不把 Disc 当 rectangle。
- 不影响 M10 核心验收。

**实现记录（2026-11-15）**

- Tube：沿 HDRP core `AreaLighting.hlsl` 的 line 路径实现 `hbgDiffuseLine`、`hbgLtcLineWidthFactor`、`hbgLineDiffuseIrradiance`、`hbgLtcLineIrradiance`；diffuse 使用 identity LTC，specular 使用 GGX LTC，line 无 area cookie（与 HDRP `EvaluateLTC_Area` 的 line 分支一致）。
- Tube sidecar 的 `width` 作为 line 长度，`height` 忽略并 warning；`LightBuilder` 写 `AreaTube`、`halfHeight=0`，仍走 Area category 与 `kLightFeatureArea`。
- Disc 不做 rectangle 冒充：sidecar 保留 `LightAreaShape::Disc`，warning 后 intensity=0；shader 的 `lightType==5` 直接返回 0，避免误走 rectangle branch。
- 验证：tube forward D3D11 GPU/CPU **0 diff**；Vulkan 与 D3D11 有 **934 像素、最大 2 LSB**；tube on/off 差异 547429 像素；tube deferred D3D11/Vulkan 同样 934 像素最大 2 LSB；disc 与无灯截图 **0 diff**；area rectangle cookie 回归 **0 diff**。
- `hbg_tests` **221 cases / 29,010 assertions**、`ctest` 2/2 通过；D3D11/Vulkan gfx-test 通过。
- 变体覆盖：forward/buffer dxbc 0–15、spirv 0–15 与 deferred dxbc/spirv 已全部重编译（O0/O1 手动 shaderc；官方 `tools/shader_build.py` 全量 O3 仍会挂起，manifest 未重新生成）。

### 5.8 M10.7 Area shadow（stretch）

**实现记录（2026-11-15）**

- **HDRP 参考**：`LightEvaluation.hlsl:587-646` 的 `EvaluateShadow_RectArea`；`HDShadowSampling.hlsl:306` 的 `SampleShadow_PCSS_Area`；`HDShadowManager.cs:805-842` 的独立 `AreaLightShadowAtlas`；`HDShadowUtils.cs:143` 的 `ExtractRectangleAreaLightData`；`HDAdditionalLightData.cs:2452` 的 `GetAreaLightOffsetForShadows`。
- **独立 atlas**：`ShadowAtlasType::Area` + `m_areaTexture`，默认 `areaAtlasSize=1024`、`areaResolution=512`、`areaFilter=Pcss`；与 directional/punctual atlas 分开分配、单独 cursor packing，不复用 punctual atlas。1024 D24S8 约 4 MB，2048/4096 分别约 16/64 MB。
- **Request**：`ShadowRequestType::Rectangle`；使用 light 本地 +Y/+X 方向、`areaShadowCone`（度，默认 120，clamp 10–179）和 `GetAreaLightOffsetForShadows` 等价的后移 spot-like cone；projection 按 rectangle width/height 的较小维度适配，view/atlas rect 写入现有 `s_shadowContext`。tube/disc 不生成 area shadow。
- **采样**：`s_shadowMapPointPunctual`（stage 13）在 area request 存在时改绑 area depth atlas；`u_pass_lightListParams2.w` 标记 area atlas active。`shadows.sc` 新增 manual-depth-compare 的 9-tap PCF 与 16 blocker + 24 filter PCSS；`light_loop.sc` 对 rectangle area 应用 shadow dimmer/tint，diffuse/specular 一起乘 shadow color。
- **限制**：stage 13 与 point PCSS 共用；area shadow active 时 punctual PCSS 回退 5×5 tent（directional PCSS 不受影响）。不做 cached area atlas、EVSM、screen-space area shadow；area shadow 不采 area cookie。
- **Sidecar**：只有 rectangle area 且存在 `shadow` 对象时 `castShadows=true`；无 `shadow` 的旧 area 场景保持 `castShadows=false`。tube/disc 的 `shadow` warning 后禁用。
- **Validation**（m10_area_shadow 场景，512 shadow；on/off）：
  - forward D3D11：**20,499 像素、max 39**；Vulkan：**20,502 像素、max 39**；D3D11 vs Vulkan：**186 像素、max 13**。
  - deferred buffer path D3D11：**20,514 像素、max 41**；Vulkan：**20,515 像素、max 41**；D3D11 vs Vulkan：**199 像素、max 11**。
  - double-sided 材质 mask8：D3D11 **43,119 像素、max 71**；D3D11 vs Vulkan **340 像素、max 8**。
  - GPU shadow culling/indirect deferred 与 CPU culling **0 diff**（2 dispatches / 2 indirect draws）。
  - GPU light-list buffer 与 `--no-gpu-light-list` CPU list **0 diff**。
  - 旧 area cookie 截图与 M10.6 **0 diff**；tube 截图 **0 diff**；M9 64-light/transparent 当次为 48/35 像素 1 LSB，M10.8 最终复跑均为 **0 diff**（见 5.9）。
  - 720p forward 30 帧：area on GPU **1.566 ms**、off **0.930 ms**（area pass 3 draws；CPU submit 噪声较大）。
- **附带修复**：`FrameBuffer::create` 改为显式 `bgfx::Attachment` + `BGFX_ATTACHMENT_NONE`，避免带 mip chain 的 color atlas 被 bgfx 自动生成 mip，导致 D3D11 `Depth Pyramid Copy` framebuffer 创建失败。

### 5.9 M10.8 M10 验收

**实现记录（2026-11-15）**

- **Area rect 矩阵**：`m10_area.gltf`（rectangle area + cookie），1280×720 验收之外的 1080/1440/4K 三档 × D3D11/Vulkan × forward/deferred × GPU buffer/GPU texture/CPU reference。1080 实测 1920×1080；1440/4K 受桌面窗口高度限制分别为 2560×1141、3840×1141（宽度按目标分辨率），12 组 × 3 路径对比**全部 0 diff**（共 36 组截图对比）。
- **区域光 cookie / directional cookie / point IES**：D3D11/Vulkan 各自 on/off 差异明显；area on/off **275,722 像素、max 116**，directional **2,231 像素、max 79**，point IES **545,062 像素、max 99**。D3D11 vs Vulkan on 图分别为 area **5 像素 max 2**、directional **0 diff**、IES **79 像素 max 2**。
- **4K/cap128**：`m10_area16.gltf`（16 area lights）在 3840×1141、forward、D3D11/Vulkan 下 buffer/texture/CPU 三路径均 **0 diff**；最终 `capacity=128/128`，FPTL `15600×288`、big-tile `3900×72`，无 clamp；buffer 日志 `cluster_indices=17,694,720 uints (70,778,880 B)`，light-list buffer 总计 **90,430,336 B（约 86.2 MiB）**（3840×1141 桌面高度上限下的实测）。83×192 RGBA32F light-data texture 约 **249 KiB**（含 LTC 追加行）。
- **4K/cap64**：同上矩阵的默认容量路径全部运行通过，buffer/texture/CPU 0 diff。
- **Tile flags readback**：`--cluster-stats-readback` 读取 GPU cluster feature flags，并与 CPU reference 逐 tile 比较；area16 场景 D3D11/Vulkan × buffer/texture 均为 `clusters=7680 active=2400 max_lights=2 offset_mismatch=0 flags_checked=240 flag_mismatch=0`；64-light 场景为 `clusters=7680 active=1680 max_lights=64 offset_mismatch=0 flags_checked=240 flag_mismatch=0`。
- **M8.7/M9 旧场景**：M8.7 `m5_shadow_stress` 32-light 对历史基线 **0 diff**；M9 64-light forward buffer/texture/CPU 在 D3D11/Vulkan 均 **0 diff**（D3D11 vs Vulkan **112 像素 max 1**）；transparent64 buffer/texture/CPU 在双后端均 **0 diff**（D3D11 vs Vulkan **59 像素 max 2**）。此前记录的 48/35 像素 1 LSB 在当前构建、相同命令下未复现。
- **Area shadow 1080p**：on/off D3D11 forward **46,063 像素 max 40**、deferred **46,106 max 40**；Vulkan forward **46,068 max 40**、deferred **46,098 max 40**；D3D11 vs Vulkan forward **420 像素 max 15**、deferred **471 max 14**。720p 数据保留在 5.8。
- **性能（720p forward，D3D11，5 帧 ×20 次中位数）**：area off GPU **1.273 ms**；1 area **1.448 ms**；4 area **1.624 ms**；16 area **2.717 ms**。area 关闭未回归，16 灯增量约 **+1.44 ms**。M9.7 的 1080p full-post cap64 55.2 fps / cap128 29.2 fps 仍是 M9 已知性能限制，M10 不伪造通过。
- **跨后端 deferred area**：1080 14 像素 max 102、1440 435 像素 max 101、4K 7 像素 max 1（均为极少边缘像素的 D3D11/Vulkan 精度差异）；同后端 buffer/texture/CPU 仍 0 diff，不作为 M10 的路径一致性失败。
- **测试/工具**：`hbg_tests` **221 cases / 29,010 assertions**、`ctest` 2/2、IDL check、D3D11/Vulkan gfx-test、`git diff --check` 通过；M10 文档已同步。
- **已知限制**：M9.7 性能未达 1080p full-post cap64 >60；官方 `tools/shader_build.py` 全量 O3 仍会挂起，forward/buffer 0–15、spirv 0–15 与 deferred 变体为手动编译覆盖；area shadow 与 point PCSS 共用 stage 13；未做 cached area atlas/EVSM/screen-space area shadow。

---

### 5.10 实现/验证记录（2026-11-14）

- **布局**：`kGpuLightTexels=83`，texel0–10 为 base，texel11–74 为 8×8 cookie，texel75–82 为 8 个 IES；light-data texture 固定 83×192（light rows 0–127，LTC 64×64 在 rows 128–191）。
- **LTC**：`tools/gen_ltc_lut.py` 从 HDRP `LtcData.BRDF_GGX.cs` 生成 `src/render/lighting/ltc_data.h`；运行时转换为 float，追加到 light-data texture。没有新增 sampler。
- **Area**：`LightType::Area`、`GpuLightType::AreaRectangle/AreaTube/AreaDisc`、sidecar `type:"area"`、`shape`、`width`、`height`、`intensity`、`color`、`areaShadowCone` 已落。Rectangle 使用 LTC GGX + `PolygonIrradiance`（Lambert diffuse）；因 HBG 的 area `right/up` 与 HDRP 方向相反，边序按 `v0→v3→v2→v1` 反转，避免 `F.z < 0` 导致全黑。LTC 表采样为手动 4-tap bilinear。
- **Category/list**：Area 写入 `LightCategory::Area`、`kLightFeatureArea`；GPU producer 在 AABB 的 `max.w` 写 category，并在 big-tile/FPTL/cluster 按 category 行输出。CPU/GPU FPTL 与 cluster 的 count/order/indices 已用 readback 对比为 0 mismatch；area GPU vs CPU reference 截图 0 diff。
- **Cookie**：area/directional cookie、`cookieScale`/`cookieOffset`、repeat/clamp 已接通；area cookie 使用 LTC 四角求 `hitUV`（不是中心投影），directional 使用 world position 的正交投影（0.1 世界单位缩放）。
- **Point IES**：LM-63 的 horizontal/vertical 二维数据按 8×8 octa 重采样并乘进 point cookie；spot IES cutoff 写入 texel5.z。缺文件/解析失败回退。
- **验证**：M10.5 改动后的 forward/deferred/light-list compute dxbc+spirv 变体均已编译；area forward D3D11/Vulkan 与 gating 前 0 diff；deferred area D3D11 GPU/CPU 0 diff（历史双后端少量边缘差异）；area cookie on/off、point IES on/off 差异明显；M9 64-light 当时 D3D11 buffer/texture/CPU 有 49 个像素 1 LSB 差异，M10.8 最终复跑为 0 diff；`hbg_tests` **221 cases / 29,010 assertions**、`ctest` 2/2、D3D11/Vulkan gfx-test 通过。
- **已知限制（M10.8 收口后）**：跨后端 deferred area 仍有极少边缘像素精度差异（1080 14 px max 102、1440 435 px max 101、4K 7 px max 1），同后端三路径均 0 diff；M9.7 的 1080p full-post cap64 55.2 fps/cap128 29.2 fps 未达 >60 门禁。官方 `tools/shader_build.py` 全量 O3 仍会挂起，shader variants 以手动 shaderc 覆盖。Area shadow 与 point PCSS 共用 stage 13，area active 时 point PCSS 回退 tent；不做 cached area atlas/EVSM/screen-space area shadow。Tube 的 area cookie 未实现（HDRP line 分支同样不采 cookie）。

### 5.11 Area 屏空间 OBB（2026-11-15）

- **HDRP 参考**：`HDGpuLightsBuilder.Jobs.cs:494-508` Rectangle 的单侧 OBB（`center = position + 0.5*range*forward`，`extent = (halfWidth+range, halfHeight+range, 0.5*range)`）；`HDGpuLightsBuilder.Jobs.cs:470-485` Tube 的全向 OBB（`extent = (halfWidth+range, range, range)`）。
- **CPU/GPU 一致**：`LightListBuilder::projectLightScreenBounds` 新增 `LightBounds` 路径；`light_screen_aabb.cs.sc` 与 `light_screen_aabb_buffer.cs.sc` 同步生成 Rectangle/Tube 的 8 个 OBB 角点，并按角点 view depth 计算 cluster 深度范围。Point/Spot/Disc 及退化轴继续走原来的 sphere AABB，旧场景语义不变。
- **验证**：`m10_area` 1080 forward/deferred × D3D11/Vulkan × buffer/texture/CPU 对 M10.8 基线 **0 diff**；`m10_area16` 双后端 buffer/texture cluster readback 均 `offset_mismatch=0`、`feature_flags_checked=240`、`feature_flag_mismatch=0`；M9 64-light 与 area shadow 1080 基线 **0 diff**；`hbg_tests` **222 cases / 29,023 assertions**，`ctest` 2/2。

## 6. 验收标准（出口）

M10 核心完成必须满足：

- **M9 基线**：M9 features off / M8.7 旧场景 0 diff。
- **Area rect**：sidecar 可创建 rectangle；LTC 光照在 forward/deferred、D3D11/Vulkan 可见；无 NaN/黑屏。
- **Area list**：Area category 的 CPU/GPU count/index 一致；旧场景不产生 area list。
- **LTC**：GGX 表与 HDRP 生成表逐点一致；双线性边界正确；不在 fragment 读取越界。
- **Cookies**：area/directional cookie on/off 可区分；repeat + scale>1 可见 tiling；缺失/损坏回退 0 diff。
- **Point IES**：水平非对称测试可区分；与 point cookie 叠加正确；不增加 texel。
- **Tile flags**：CPU/GPU flags 一致；gating 前后 0 diff。
- **矩阵**：area rect 至少 1080p/1440p/4K、双后端、双路径；buffer/texture/CPU 0 diff。
- **资源**：4K/cap128 不 clamp；记录 texture 高度/内存；无 validation error。
- **测试**：`hbg_tests` 新增用例通过，`ctest`/IDL/gfx-test 通过。
- **文档**：未实现项明确写“未实现”，不能把 Disc real-time LTC、cached area atlas/EVSM 写成已完成。

M10 stretch（不阻塞核心）：

- Disc 的 real-time LTC；area shadow 性能/缓存优化。

---

## 7. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| LTC buffer 在 fragment 不可用 | area 无法在 D3D/VK 同时工作 | M10.0 先做最小 buffer 测试；失败则回退 texture atlas 并重新审计 sampler |
| 增加 3 texel 使 light-data 变大 | 4K/cap128 带宽/内存上升 | 记录 80→83 增量；4K 默认 cap64；避免再加 cookie atlas |
| area 保守 AABB 过大 | 过绘制/性能下降 | 先 sphere/AABB，后精确 OBB；tile flags gating |
| cookie repeat/scale 拉伸 | 视觉质量差 | M10 只做 8×8 nearest；高分辨率留 M11 |
| point IES 8×8 块状 | 与 HDRP 有差异 | 明确近似；用水平非对称测试；高分辨率 atlas 另立 |
| 29 variants 编译爆炸 | 构建时间/包体 | M10 只做 tile flags；完整 variants M11 |
| area shadow atlas 内存 | 4096 面积 atlas 过大 | 默认 1024；2048/4096 opt-in，需记录内存 |
| M9 性能未解决 | M10 性能预算被掩盖 | 先冻结/记录 M9.7；M10 只承诺 area 增量 |

---

## 8. 待确认决策

1. **Area authoring**：确认用“现有 point/spot node + sidecar `type:"area"`”，还是新增 glTF extension。
2. **LTC 存储**：确认 buffer 方案；若 buffer 在 fragment 不兼容，是否允许新增一个 texture sampler（需要腾出 sampler）。
3. **Area 形状**：M10 核心是否只 Rectangle；Tube 是否必须；Disc 是否明确 unsupported。
4. **Area shadow**：M10 是否必须包含 rectangle area shadow；若必须，M10.7 从 stretch 提升为核心，需增加时间。
5. **Point IES**：确认复用 8×8 cookie texel（推荐）；若要求更高分辨率，需要额外存储/atlas 方案。
6. **Tile variants**：确认 M10 只做 flags + runtime gating，29 variants 延后 M11。
7. **M9 性能**：确认 M9.7 的 55.2 fps 作为已知限制冻结，还是先修复再开 M10。

---

## 9. 实现顺序

1. **M10.0**：锁 sidecar/枚举/83 texel/LTC 生成脚本；M9 基线截图。
2. **M10.1**：Area 数据、culling、category/list；先不评估光照。
3. **M10.2**：Rectangle LTC（先 diffuse，后 specular）；forward/deferred。
4. **M10.3**：Area/directional cookie + scaleOffset/repeat。
5. **M10.4**：Point IES + spot cutoff。
6. **M10.5**：Tile feature flags/gating。
7. **M10.7**：area shadow（stretch）；Disc real-time LTC 不在 M10 承诺。
8. **M10.8**：全矩阵、性能、文档收尾。

**开始制作条件**：用户确认 M10 范围和上述待确认决策后，从 M10.0 开始；在此之前只维护本文档，不改代码。
