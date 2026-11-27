# M5 阴影质量与接触阴影实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M5.0/M5.1/M5.2/M5.3/M5.4 完成**。
> 原则：先读 HDRP 源码再实现；本文只做索引和设计，HDRP 行为以源码为准。
> 参考：
> - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
> - HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
> - HDRP 阴影：`Lighting/Shadow/HDRenderPipeline.ScreenSpaceShadows*.cs`、`ContactShadows.*`、`HDShadowManager.cs`、`HDShadowAtlas.cs`
> - HDRP 阴影采样：`Lighting/Shadow/HDShadowAlgorithms.hlsl`、`HDShadowSampling.hlsl`
> - HDRP Core PCSS：`com.unity.render-pipelines.core/ShaderLibrary/Shadow/PCSS.hlsl`

---

## M5 范围与路线

M5 的主题是**阴影质量**：把 M1.7 的固定 shadow v1 升级为 HDRP 风格的阴影请求、过滤、接触阴影和缓存。M5 不重写 PBR/材质，不做面积光、体积云、物理天空或后处理新效果。

| 子任务 | 目标 | 依赖 |
|---|---|---|
| **M5.0 Shadow settings / atlas / culling / alpha test** | 可配置 cascade/distance/filter/resolution；per-request culling；alpha-tested shadow caster | M4.3 |
| **M5.1 Soft shadows / bias / micro shadows** | HDRP PCF 5×5/7×7、PCSS、normal/slope bias、cascade fade、micro shadow | M5.0 |
| **M5.2 Contact shadows** | HDRP `ContactShadows.compute` 风格屏幕空间 ray march、半分辨率、packed mask | M5.0/M5.1（完成） |
| **M5.3 Per-light shadow controls / cached punctual shadows** | dimmer/fade/tint/near plane/resolution/update mode；静态 punctual shadow cache | M5.1/M5.2（完成） |
| **M5.4 M5 验收** | D3D11/Vulkan 矩阵、debug、性能、文档、边界 | M5.0–M5.3 |

### M5 明确不做

- **不做 DXR/ray-traced screen-space shadows、RT shadows、path tracing**。HDRP 的 `useScreenSpaceShadows` 明确是 ray tracing only（`HDAdditionalLightData.cs:622-638`）；本项目没有 DXR，M5 只做 contact shadows 的 screen-space ray march。
- 不做 lightmap/shadow mask、面积光 shadow、透明/次表面 shadow、cloud shadow、GPU-driven culling。
- 不做 shadow cache 的完整 HDRP cached atlas/mixed directional；M5.3 只做静态 punctual v1，动态 caster 自动回退 every-frame。
- 不改变 M3 后处理顺序、M4 fog 顺序和现有透明路径。

### HDRP 关键结论

1. **HDRP 非 RT 的 `ScreenSpaceShadows.shader` 不是深度 ray march**：它读取 depth/normal，然后调用 shadow map 的 directional attenuation（`ScreenSpaceShadows.shader:43-70`），真正的屏幕空间 ray march 在 `ContactShadows.compute`。M5 因此把“屏幕空间阴影”落到 contact shadows，而不是伪装一个 RT 路径。
2. **HDRP 的阴影质量档位不是简单 tap 数**：directional Low/Medium/High 分别走 PCF5×5/PCF7×7/PCSS；punctual Low/Medium/High 分别走 PCF3×3/PCF5×5/PCSS（`HDShadowAlgorithms.hlsl:37-58`、`HDShadowManager.cs:872-890`）。M5 必须按这套语义实现，而不是继续用当前 `filterMode=1/2` 的朴素方框 PCF。
3. **contact shadows 是 packed mask**：HDRP 把 8-bit fade + 24-bit light mask 打包进 `R32_UINT`，再在 light loop 用 `GetContactShadow()` 应用（`LightLoopDef.hlsl:417-525`、`LightLoop.cs:132-135`）。M5 沿用这个数据布局。
4. **HDRP 的 shadow dim/tint/fade 在 light data 和 shadow loop 中应用**：`shadowDimmer` 用 `lerp` 衰减 shadow，`shadowTint/penumbraTint` 用 `ComputeShadowColor`，不是仅改 shadow map（`HDShadowLoop.hlsl:44-61/125-172`、`CommonLighting.hlsl:397-415`）。

---

## M5 起点基线与缺口（M5.0 已修复）

M5.0 前的 shadow v1：

- directional 固定 4 cascade、固定 split `0.10/0.25/0.50/1.0`、固定 scene-radius shadow distance；atlas 2048、4×1024（`src/render/shadows/shadow_manager.cpp:256-290`）。
- punctual 固定 point 6×256、spot 512、shelf pack、最多 32 requests；没有按光源/按 request 的 frustum cull（`src/render/shadows/shadow_manager.cpp:406-421`、`shaders/passes/forward_renderer.cpp:1352-1375`）。
- `shaders/lighting/shadows.sc` 现有 1-tap/3×3/5×5 朴素方框 PCF，`u_pass_shadowParams.x` 是全局 filter mode（`shaders/lighting/shadows.sc:70-100`）。
- shadow caster fragment 只写常量白色，没有 baseColor alpha test（`shaders/passes/shadowcaster.fs.sc:1-6`）；glTF alphaMask 的 shadow 不正确。
- `GpuShadowContext` 8 texels，只含矩阵/atlas rect/bias/type；没有 filter、softness、dimmer、tint、fade（`src/render/shadows/shadow_manager.h:59-62`、`shaders/lighting/shadows.sc:10-30`）。
- `GpuLightData` 4 texels；`shadowFlags.z` 当前是未使用的 layerMask，可复用为 contact mask（`src/render/lighting/gpu_light_data.h:10`、`shaders/common/light_data.sc:5-23`）。
- `config/pipeline.json` 的 `supportContactShadows/supportScreenSpaceShadows/shadowMask` 都是 false（`config/pipeline.json:7-9`）；`FrameSettings` 已有 `ContactShadows/ScreenSpaceShadows/Shadowmask` 位（`src/render/pipeline/frame_settings.h:77-99`）。
- 没有 shadow cascade debug 之外的 contact/filter 诊断；现有 directional/punctual atlas debug 可保留。

---

## HDRP 参考索引

| 参考 | 用途 |
|---|---|
| `Lighting/Shadow/HDShadowSettings.cs:99-142` | `maxShadowDistance`、cascade count/splits/borders；M5.0 设置模型 |
| `Lighting/Shadow/HDShadowManager.cs:383-471` | shadow init params：atlas、resolution、filter quality、max requests、screen-space slots |
| `Lighting/Shadow/HDShadowManager.cs:872-890` | directional quality → PCF5×5 / PCF7×7 / PCSS |
| `Lighting/Shadow/HDShadowManager.cs:1036-1051` | `normalBias`、`shadowSoftness`、PCSS 参数写入 shadow data |
| `Lighting/Shadow/HDShadowManager.cs:1200-1213` | shadow distance 的 fade scale/bias |
| `Lighting/Shadow/HDShadowAtlas.cs:207-260` | `AddShadowRequest`、atlas texture、output |
| `Lighting/Shadow/HDShadowAtlas.cs:738-783` | `BlurShadows` / `RenderShadows` 顺序 |
| `Lighting/Shadow/HDShadowCullingUtils.cs:23-131` | directional/point/spot culling split 与 culling info |
| `Lighting/Shadow/HDShadowAlgorithms.hlsl:37-58` | punctual/directional/area filter 档位 |
| `Lighting/Shadow/HDShadowSampling.hlsl:15-180` | PCF gather/3×3/5×5/7×7 tent |
| `Lighting/Shadow/HDShadowSampling.hlsl:287-377` | PCSS punctual/directional 入口 |
| `com.unity.render-pipelines.core/ShaderLibrary/Shadow/PCSS.hlsl:101-227` | punctual 的 blocker search、penumbra、filter |
| `com.unity.render-pipelines.core/ShaderLibrary/Shadow/PCSS.hlsl:466-519` | directional PCSS |
| `com.unity.render-pipelines.core/ShaderLibrary/Shadow/ShadowSamplingTent.hlsl:109-259` | 3×3/5×5/7×7 tent 采样权重 |
| `Lighting/Shadow/ContactShadows.cs:13-89` | contact shadow length/opacity/distance/fade/bias/thickness/samples |
| `Lighting/Shadow/ContactShadows.compute:65-172` | `ScreenSpaceShadowRayCast`、dither、半分辨率、compare threshold |
| `Lighting/Shadow/ContactShadows.compute:181-304` | deferred contact shadow dispatch、packed mask |
| `Lighting/Shadow/ContactShadows.hlsl:1-19` | contact shadow uniforms |
| `Lighting/LightLoop/LightLoopDef.hlsl:417-525` | packed mask unpack、`GetContactShadow` |
| `Lighting/LightLoop/LightLoop.cs:132-135` | 8-bit fade + 24-bit mask 打包常量 |
| `Lighting/LightLoop/HDShadowLoop.hlsl:44-61/125-172` | `shadowDimmer`、`shadowTint`、`penumbraTint` |
| `Lighting/SurfaceShading.hlsl:109` | micro shadow 应用点 |
| `Lighting/SurfaceShading.hlsl:122` | `ComputeShadowColor` |
| `com.unity.render-pipelines.core/ShaderLibrary/CommonLighting.hlsl:397-415` | `ComputeMicroShadowing` / `ComputeShadowColor` |
| `Lighting/Shadow/MicroShadowing.cs:12-25` | micro shadow enable/opacity |
| `Lighting/Light/HDAdditionalLightData.cs:1087-1151` | `shadowNearPlane`、blocker/filter sample count |
| `Lighting/Light/HDAdditionalLightData.cs:1445-1570` | `shadowResolution`、`shadowDimmer`、`shadowFadeDistance`、`shadowTint`、`penumbraTint` |
| `Lighting/Light/HDAdditionalLightData.cs:1623-1635` | `ShadowUpdateMode` |
| `Lighting/Light/HDAdditionalLightData.cs:2318-2322` | per-light shadow resolution 上限 |
| `Lighting/Shadow/HDCachedShadowManager.cs:304-424` | cached shadow 注册/调度/失效 |
| `Material/Lit/LitData.hlsl:232-247` | `GENERIC_ALPHA_TEST` 与 `_AlphaCutoffShadow` |
| `Material/Lit/Lit.shader:602-644` | ShadowCaster pass 与 alpha cutoff |

---

## M5.0 Shadow settings / atlas / culling / alpha test

状态：`[x]`（2026-11-12）

目标：把 shadow v1 的固定常量变成可配置、可验证的 shadow 数据层，并补上正确的 alpha-tested caster。

### 数据契约

新增 `src/render/shadows/shadow_settings.h/.cpp`，由 `config/shadows.json` 加载：

```json
{
  "maxDistance": 200.0,
  "cascadeCount": 4,
  "cascadeSplits": [0.10, 0.25, 0.50, 1.0],
  "directionalFilter": "pcf3x3",
  "punctualFilter": "pcf3x3",
  "directionalResolution": 1024,
  "pointResolution": 256,
  "spotResolution": 512,
  "atlasSize": 2048,
  "maxRequests": 64,
  "normalBias": 0.2,
  "cachedPunctualShadows": false
}
```

- `ShadowFilter`：`off`、`pcf3x3`、`pcf5x5`、`pcf7x7`、`pcss`；映射到 HDRP 档位，不在 shader 里继续用 0/1/2 魔数。M5.0 默认 `pcf3x3` 并支持 off/3×3/5×5；`pcf7x7`/`pcss` 先回退到 5×5，M5.1 再实现 tent/PCSS 并切默认 `medium`。
- `ShadowRequest` 增加 `filter`、`softness`、`nearPlane`、`slopeBias`、`isCached`、`cascadeIndex`、`frustumPlanes`；保留现有 `depthBias/normalBias`。contact mask 属于 light，不放在 request。
- `pointResolution` 与 `spotResolution` 分开，因为 2048 atlas 上 6 个 512 point face 放不下；默认 point 256 / spot 512，匹配现有容量。`normalBias` 在 M5.0 作为现有 directional/point/spot bias 的统一缩放，M5.1 再做 HDRP 的 world-texel/slope bias。
- `GpuShadowContext` 从 8 texels 扩到 **10 texels**：
  - 0–3：`worldToShadow`
  - 4：atlas rect
  - 5：`depthBias, normalBias, invWidth, invHeight`
  - 6：`type, face, filterMode, enabled`
  - 7：`softness, nearPlane, worldTexelSize, slopeBias`
  - 8：`shadowFadeScale, shadowFadeBias, cascadeIndex, isCached`
  - 9：`blockerSamples, filterSamples, minFilterSize, farPlane`
- dimmer/fade/tint 等 per-light 颜色/强度参数在 M5.3 放到 `GpuLightData`，不塞进 shadow context，避免同一参数两处来源。
- `GpuLightData` 在 M5.0/M5.2 保持 **4 texels**；M5.2 复用当前未使用的 `shadowFlags.z` 作为 contact mask bit。M5.3 再扩到 **6 texels**（texel4=`shadowTint.rgb, shadowDimmer`，texel5=`shadowFadeDistance, penumbraTint, flags2, unused`），避免 M5.0 一次性改所有测试。
- `config/pipeline.json`：`supportContactShadows` 已置 true（contact 由 settings/FrameSettings 双门控）；`supportScreenSpaceShadows` 保持 false（无 DXR）；`supportShadowMask` 保持 false（无 lightmap）。
- `config/frame_settings.json`：`ShadowMaps=true`、`ContactShadows=false` 默认；CLI/JSON 可覆盖。

### Atlas 与 culling

- atlas 分辨率支持 1024/2048/4096；directional atlas 内 cascade 自适应 cell 尺寸（1 级 1×1、2 级 2×1、3/4 级 2×2）；punctual atlas 继续 shelf pack，放不下时逐级减半到 64 并告警，不崩溃。
- directional shadow distance 从 `sceneRadius*8` 固定值改为 settings `maxDistance`，并保留 `camera.farClip` 上限。
- cascade split 支持 1–4 级；默认 `0.10/0.25/0.50/1.0`；cascade border/fade 已在 M5.1 接。
- **per-request culling**：由 `ShadowRequest.viewProj` 提取 6 个 frustum planes；对 `DrawItem.worldBounds` 做 `intersectsFrustum`。为保持 instancing，`DrawList::buildCulled` 复制可见 item 后重建 batch。
- 共享 shadow list 现在是相机 frustum cull；M5.0 额外做 light/cascade cull；`FrameDiagnostics` 统计 `shadowDrawnItems` / `shadowCulledItems`，测试场景 20 个 request-item 降到 14 draw、cull 6，画面与 culling off **0 diff**。

### Alpha-tested shadow caster

- `shaders/passes/shadowcaster.fs.sc` 增加 material baseColor/alpha cutoff 输入；`alpha < cutoff` 时 `discard`。
- 使用 HDRP 语义：`_AlphaCutoffShadow`（`Lit.shader:97-101`、`LitData.hlsl:232-247`）；M5 直接复用 glTF `alphaCutoff`，不区分 prepass/postpass。
- shadow program key 增加 alpha-clip variant；`drawShadowBatch` 与 per-item fallback 都绑定 `GpuMaterial` 的 base color texture、`MaterialUniforms` 和 cutoff；instanced shadow 绑定同一 material。
- 透明 `AlphaMode::Blend` 不进入 shadow list；alphaMask 才进入。
- `shadowcaster.vs.sc` / `shadowcaster_instanced.vs.sc` 输出 `v_texcoord0`，新增 `varying_shadow*.def.sc`；`tools/shader_build.py` 为 shadow 生成 16 个 material variant。M5.0 验证了 cutout cube 的 shadow atlas 出现镂空阴影。

### 测试 / 出口

- `tests/render/test_shadow.cpp`：cascade count/resolution/filter/context layout、request frustum planes、point/spot request。
- `tests/render/test_shadow_settings.cpp`：默认值、JSON round-trip、非法 filter/splits、atlas/resolution/maxRequests 的 sanitize。
- `tests/render/test_draw_list.cpp`：透明材质不进 shadow list、alphaMask 进入 shadow list、`buildCulled` 重建 batch 与 `culledCount`。
- `tests/app/test_backend.cpp`：`--shadow-profile`、`--shadow-filter`、`--shadow-distance`、`--shadow-cascades`、`--shadow-atlas-size`、`--shadow-max-requests`、分辨率/bias 的正/负参数。
- 视觉：shadow off D3D/VK **109.54 dB（max 1）**；alpha cutout shadow atlas 有镂空；`--shadow-cascades=2 --shadow-distance=50 --shadow-atlas-size=1024` 的 D3D/VK **109.54 dB**；per-request culling 前后 **0 diff**。
- 出口：`hbg_tests` **184 cases / 18,212 assertions** 通过；setting 全部生效；atlas 不越界；culling 后 draw 数下降且画面不变；alpha shadow 通过 D3D/VK。

### 实现记录

- 新增 `src/render/shadows/shadow_settings.h/.cpp`、`config/shadows.json`；`ShadowSettings` 支持 maxDistance、1–4 cascade、split、directional/punctual filter、directional/point/spot resolution、atlas、maxRequests、normalBias、cachedPunctualShadows。
- `ShadowManager` 改为从 settings 建 atlas/context；`GpuShadowContext` 10 texels、context texture 64 行；`MaxRequests` 64；`ShadowRequest` 增加 frustum planes、filter/nearPlane/slopeBias/isCached/cascadeIndex。
- `DrawList::buildCulled` + `renderShadowPass` 做 per-request culling；`FrameDiagnostics.shadowDrawnItems/shadowCulledItems` 暴露统计。
- `shadowcaster` 增加 `v_texcoord0`、base color alpha、material variant；`shader_library` 读取 shadow fragment 数组；`tools/shader_build.py` 生成 16 个 shadow variant。
- 命令行：`--shadow-profile`、`--shadow-filter`、`--shadow-directional-filter`、`--shadow-punctual-filter`、`--shadow-distance`、`--shadow-cascades`、`--shadow-atlas-size`、`--shadow-max-requests`、`--shadow-directional-resolution`、`--shadow-point-resolution`、`--shadow-spot-resolution`、`--shadow-normal-bias`。
- 验证命令：`cmake --build --preset vs2022-debug --target hbg hbg_tests`；`build\vs2022\tests\Debug\hbg_tests.exe`；D3D11/Vulkan `--render-test=... --frames=2`。

---

## M5.1 Soft shadows / bias / micro shadows

状态：`[x]`（2026-11-12）

目标：把当前朴素方框 PCF 升级为 HDRP 的 tent/PCSS 过滤，并补 bias、cascade fade 和 micro shadow。

### 过滤

- `low`：directional PCF5×5、punctual PCF3×3。
- `medium`：directional PCF7×7、punctual PCF5×5。
- `high`：PCSS（directional + punctual）。
- `off`：1 tap，用于回归/性能对照。
- 直接 port HDRP `ShadowSamplingTent.hlsl:109-259` 的 3×3/5×5/7×7 tent 权重；不要在循环里用朴素 `for x/y` 方框。
- PCSS 直接参考 `PCSS.hlsl:101-227` / `:466-519` 的 blocker search + penumbra + filter；默认 blocker/filter samples 16/24，`minFilterSize=0.01`。
- request 从全局 settings 写入 filter/softness/blocker/filter samples；per-light 覆盖留到 M5.3。
- 需要额外的 shadow map point sampler 用于 blocker search；forward/deferred 光照 pass 使用 stage 12/13 = `s_shadowMapPoint`/`s_shadowMapPointPunctual`，fog 不编译 PCSS point sampler，stage 14/15 仍留给 probe/contact。

### Bias / fade

- `normalBias`：HDRP `EvalShadow_NormalBiasOrtho/Proj` 语义（`HDShadowAlgorithms.hlsl:153-160`）；M5.1 已改为 `normalWS * normalBias * worldTexelSize * (perspective ? shadowCoord.w : 1)`。
- `slopeBias`：方向光/spot 的 slope-scaled bias；按 HDRP `HDShadowManager.cs:1036-1051` 的 request 参数上传。
- `shadowNearPlane`：point/spot 投影 near plane；按 `HDShadowUtils.cs:45-97/261-315`。
- cascade fade：按 `HDShadowManager.cs:1198-1213` 的 scale/bias 在最大距离前淡出，不再硬切。
- `shadowDimmer`、`shadowTint`、`penumbraTint` 的数据和 shader 应用放到 M5.3；M5.1 的 context texel 7/8 只放 softness/near/worldTexel/slope/fade，不放颜色。

### Micro shadows

- 只对 directional light 生效；使用 `ComputeMicroShadowing(AO, NdotL, opacity)`（`CommonLighting.hlsl:397-406`）。
- 应用点与 HDRP `SurfaceShading.hlsl:109` 一致：有 shadow 时乘 micro shadow；AO 来源为 GBuffer/forward surface 的 material AO。
- `--micro-shadows`/`--no-micro-shadows`、`config/shadows.json` 的 `microShadows.enabled/opacity`；默认关，避免改变 M4.3 基线。
- 不做 screen-space bent AO、SSAO 二次乘、specular occlusion。

### 测试 / 出口

- `tests/render/test_shadow.cpp`（M5.0 已存在）与 `tests/render/test_shadow_settings.cpp`：filter 映射、context 参数、normal/slope bias、cascade fade 字段、settings round-trip；shader PCSS 用 D3D/VK 截图验证。
- 视觉：filter low/medium/high 的 D3D/VK 差异可解释；PCSS 半影随 blocker 距离变化；cascade 接缝/硬切消失；micro shadow on/off 只影响受光表面。
- 性能：PCSS high 允许低于 medium，但 full post 1080p 必须 >60fps；记录 PCF/PCSS 各自帧率。
- 出口：medium 为默认；high 可选；off/low 不回归；D3D/VK 一致。

### 实现记录

- `ShadowFilter` 的 shader mode 固定为 `off=0`、`pcf3x3=1`、`pcf5x5=2`、`pcf7x7=3`、`pcss=4`；`--shadow-quality=low|medium|high` 分别映射 directional/punctual 到 5×5/3×3、7×7/5×5、PCSS/PCSS；默认切为 medium（`pcf7x7`/`pcf5x5`）。
- `shadows.sc` port HDRP `ShadowSamplingTent.hlsl` 的 3×3（4 bilinear taps）、5×5（9 taps）、7×7（16 taps）tent 权重；PCSS 使用 Fibonacci disk、blocker 深度均值、penumbra filter 和 tile bounds 限制，默认 blocker/filter=16/24。
- normal bias 改为 HDRP 语义：`worldTexelSize` 由 shadow projection/视口计算，directional 乘 1，perspective 乘 `shadowCoord.w`；slope bias 通过 `bgfx::setViewDepthBias(1, slopeBias)` 写入 shadow caster pass，`shadowNearPlane` 进入 point/spot 投影。
- cascade fade 使用 `HDShadowManager.cs:1198-1213` 的 scale/bias 思路：`cascadeFade` 默认 0.10，最后一个 cascade 在 max distance 前淡出到 1；不开 shadow 时不受影响。
- micro shadow 只作用于 directional light，使用 `ComputeMicroShadowing(AO, NdotL, opacity)`；opacity 由 `shadowParams.y` 传给 forward/deferred，默认 `microShadows=false`，因此 M5.0 shadow-off baseline 保持。
- forward/deferred 光照 shader 定义 `HBG_SHADOW_POINT_SAMPLER`，绑定 stage 12/13 point depth；volumetric fog 不定义该宏，PCSS 在 fog 内回退 tent 5×5，避免 stage 12/13 与 fog mask 冲突。
- `runRenderTest` 现在接收 `shadowsEnabled`，`--no-shadows` 在 render-test 路径真正关闭 shadow pass（`shadow_drawn=0`）；之前 render-test 忽略该参数。
- 验证：`hbg_tests` **192 cases / 18,416 assertions**；medium shadow D3D/VK **112.55 dB**；PCSS shadow D3D/VK **74.58 dB**；spot PCSS **0 diff**；point PCSS **61.41 dB**；deferred PCSS D3D/VK **74.53 dB**；micro shadow helmet on/off **41.31 dB**；shadow off `m51_noshadow` D3D/VK **109.54 dB** 且 `shadow_drawn=0`。
- 性能（Debug，1080p，300 帧，shadow scene）：medium D3D/VK **79.3/70.8 fps**，high D3D/VK **63.2/53.3 fps**；RelWithDebInfo 同场景 medium **96.0/81.0 fps**、high **83.9/128.5 fps**（高质档在简单遮挡场景 blocker search 早退；M5.4 用最终 post 栈复测）。

---

## M5.2 Contact shadows

状态：`[x]`

目标：实现 HDRP `ContactShadows.compute` 风格的屏幕空间 contact shadow，补足 shadow map 在接触处的漏光。

### 数据 / 设置

- 新增 `ContactShadowSettings`（默认值对齐 `ContactShadows.cs`）：`enabled=false`、`directionalOnly=false`、`halfResolution=false`、`length=0.15`、`opacity=1.0`、`distanceScaleFactor=0.5`、`minDistance=0`、`maxDistance=50`、`fadeDistance=5`、`fadeInDistance=0`、`rayBias=0.2`、`thicknessScale=0.15`、`sampleCount=10`（4..64）。
- CLI：`--contact-shadows`/`--no-contact-shadows`、`--contact-shadow-length`、`--contact-shadow-opacity`、`--contact-shadow-samples`、`--contact-shadow-half-res`/`--no-contact-shadow-half-res`、`--contact-shadow-directional-only`。
- `config/shadows.json` 增加 `contactShadows` 对象；`config/pipeline.json` 的 `supportContactShadows` 置 true；`FrameSettings::ContactShadows` 与 `supportContactShadows` 双门控。

### 深度 / 光源输入

- compute 直接读取 full-res depth（`texelFetch`），不再把 min/max depth pyramid 当线性深度；半分辨率只降低输出/追踪分辨率，full-res depth 仍用于重建接触位置。未做独立半分辨率 R32F depth copy，避免额外全屏 pass。
- 光源使用现有 FPTL light list + `GpuLightData`；contact mask 复用 `shadowFlags.z`，最多 **8 位**（directional bit0，punctual bit1..7），由 `LightBuilder` 为 cast shadow 的光源分配。
- 透明物体不参与 contact shadow：forward transparent 的 `u_pass_lightColor.w=0`；forward/deferred opaque 共用同一 packed mask。MSAA 开启时 contact 自动关闭（当前 depth 输入未走 MSAA 解析路径）。

### Compute / packed mask

- 新增 `shaders/passes/contact_shadows.cs.sc`（`ContactShadows` compute pass）与 `lighting/contact_shadow.sc`：
  - `ScreenSpaceShadowRayCast` 风格 ray march：`rayStartWS = positionWS - positionWS * rayBias`，`rayLength = length * max(0.5, linearDepth * distanceScaleFactor)`，IGN dither + `_FrameCount % 8`。
  - 每个 sample 从 depth 重建 world position，以**世界空间距离**判定 ray 与遮挡面的接触厚度；跳过约前 30% step（1–3 个），避免表面自交；再乘 min/maxDistance、fadeIn/fadeDistance 和屏幕边缘 vignette。
  - `packed = (mask, fade)`：forward 用半/全分辨率 **RGBA8** contact texture（b=mask，a=fade），deferred 直接写 GBuffer1.b/.a；两种路径都先写 neutral，避免未初始化池化纹理。
  - half-res 时 deferred 每个线程写 2x2 GBuffer 像素；forward contact texture 由 light loop 做 4-tap OR/平均（point sampling，避免 mask bit 被 bilinear 混合）。
- `light_loop.sc` 增加 `hbgContactShadowVisibility`：`1 - occluded * fade * opacity`；directional-only 只输出 bit0；非 contact light 返回 1；只乘直接光，不影响 ambient/IBL。
- forward 在 stage 11 绑定 contact texture；deferred 从 GBuffer1.b/.a 读取，避免额外 stage；`--debug-view=contact-shadows` 显示 packed mask（forward 显示 contact texture，deferred 显示 GBuffer1）。

### 测试 / 出口

- `tests/render/test_contact_shadow.cpp`：settings/sanitize、half/odd resolution、mask bit、pack/unpack、ray length/fade/thickness、LightBuilder directional-only mask、JSON round-trip。
- 验证：`hbg_tests` **192 cases / 18,416 assertions**（M5.2 时点）；`ctest` 2/2；D3D/VK contact full **88.43 dB**（1 pixel max21），half **逐像素一致**；forward half D3D/VK **112.55 dB**；contact on/off **45.63 dB**，full/half **46.38 dB**；directional-only 只保留 bit0；`--no-shadows` 下 contact=false。
- 性能（1080p，300 帧，单次 RelWithDebInfo，shadow scene）：contact off **182.4 fps**；full 10 samples **190.0–253.2 fps**；half **186.9 fps**；4 samples **198.7 fps**；16 samples **113.7 fps**；Vulkan full 10 samples **211.2–285.0 fps**（多次单次测量，后台负载敏感）。简单场景数值波动大，M5.4 用最终 post 栈复测。
- 视觉：cube/ground 接触处出现细 contact shadow；off/on 差分 bbox 约 `x508..646 y387..426`；ambient/IBL 不变；transparent scene contact on/off **0 diff**；未出现资源泄漏。
- 出口：D3D/VK 一致、half/full 差异可解释、fault fallback 为 1、无泄漏。未做 per-light contact 开关（M5.3）和 DXR contact shadow。

## M5.3 Per-light shadow controls / cached punctual shadows

状态：`[x]`

目标：补齐 HDRP 的 per-light shadow 语义，并为静态 punctual 光源加一个保守的 cached shadow v1。

### per-light 数据

`Light` 增加 `ShadowSettings`（或 `LightShadowSettings`）：

```text
dimmer = 1.0
fadeDistance = 10000
nearPlane = 0.1
resolution = 0 (0 = global)
tint = (0,0,0)
penumbraTint = false
updateMode = everyFrame | onEnable | onDemand
contactShadows = true
softness = 0.5
blockerSamples = 16
filterSamples = 24
```

- `glTF` sidecar 的 `lights[].shadow` 读取；缺省沿用 M5.0 全局设置；非法值 sanitize。
- `GpuLightData` 扩到 6 texels（texel4=`shadowTint/dimmer`，texel5=`fadeDistance/penumbraTint/flags2`；contact mask 继续用 texel3.z）；`LightBuilder` 写新字段。
- `shadows.sc` 只负责 visibility/filter；`light_loop.sc` 与 `direct_lighting.sc` 从 `GpuLightData` 读取 dimmer/fade/tint 并统一应用 `ComputeShadowColor`/dimmer 语义，避免同一参数两处来源。
- `shadowResolution` 进入 atlas pack，point/spot 可分别 256/512/1024/2048；不支持的尺寸回退到 512 并告警。
- `shadowNearPlane` 进入 point/spot 投影；`shadowFadeDistance` 进入 light selection/culling（HDRP `HDProcessedVisibleLightsBuilder.Jobs.cs:171-185` 语义），超过距离的 punctual light 不生成 shadow request。
- `--shadow-dimmer`、`--shadow-fade-distance`、`--shadow-tint` 只做全局调试覆盖；per-light 值优先。

### Cached punctual shadow v1

- HDRP 用 `HDCachedShadowManager` + cached atlas + `ShadowUpdateMode`（`HDCachedShadowManager.cs:304-424`、`HDAdditionalLightData.cs:1623-1635`）。M5 的 v1 限定：
  - 仅 **punctual/spot** 静态 caster；directional 4 cascade 仍 every-frame（cascade 随相机变化）。
  - 仅当本帧所有 punctual shadow caster 都是静态、且没有动态 caster 时启用 cache；否则整帧回退 every-frame。
  - cached punctual atlas 单独分配，避免每帧 clear；`ShadowContext` 增加 `isCached` 标志，shader 选择 cached/dynamic atlas（stage 15 预留）。
  - light 变换、scene revision、alpha cutout/material shadow flag、`--no-shadow-cache` 变化时整体失效；v1 不做 HDRP mixed cached directional/分块 partial update。
- `--shadow-cache`/`--no-shadow-cache`；`config/shadows.json` 的 `cachedPunctualShadows`；diagnostics 输出 `shadow_cache=on/off`、`cached_requests`。
- 如果实现成本超预算，M5.3 可以只完成 per-light controls，把 cache 明确标为 M5.3 未完成并转 M6；但不得把 cache 作为 M5 出口的隐含承诺。

### 实现记录（M5.3）

- `Light` 增加 `LightShadowSettings`：dimmer/fadeDistance/nearPlane/resolution/tint/penumbraTint/updateMode/contactShadows/softness/blockerSamples/filterSamples；`shadowConfigured` 区分 sidecar 覆盖与全局默认。`resolveLightShadow()` 对未配置光源继承 `ShadowSettings.shadowDimmer/shadowFadeDistance/shadowTint/shadowNearPlane/softness/samples`。
- `scene_loader` 读取 sidecar `lights[].shadow`：按 `name` 匹配（无 name 时按索引），非法值报错；resolution 只接受 0/256/512/1024/2048，其他回退 512 并告警；`updateMode` 接受 everyFrame/onEnable/onDemand。
- `GpuLightData` 扩到 **6 texels**：texel4=`shadowTint.rgb, shadowDimmer`，texel5=`shadowFadeDistance, penumbraTint, flags2, unused`；contact mask 仍在 texel3.z。`LightBuilder` 写新字段；`--shadow-dimmer/--shadow-fade-distance/--shadow-tint` 作为未配置光源的全局默认。
- shader `light_loop.sc` 读取 dimmer/tint：shadow visibility 先按 `lerp(1, visibility, shadowDimmer)` 衰减，contact 后再用 HDRP `ComputeShadowColor` 语义得到彩色 shadow；`direct_lighting.sc` 的 visibility 改为 vec3，默认 black tint 与旧标量结果一致。directional 同样支持 dimmer/tint；directional micro shadow 顺序保持。
- per-light resolution 进入 atlas pack；point/spot 的 viewport、near plane、softness、blocker/filter samples 均取 per-light 值。punctual 在 90%–100% `shadowFadeDistance` 内线性衰减 dimmer，超过距离不生成 request；directional 不按距离剔除。
- Cached punctual v1：`ShadowManager` 额外分配 `shadow.cachedPunctualAtlas`，cache 帧绑定该 atlas；cached request 不重绘/不 clear，但每个 cached request 仍保留一个空 view 绑定以保持 bgfx depth texture 状态（不产生 draw）。cache 只在全局 `cachedPunctualShadows`、非 debug punctual atlas、存在 punctual caster 时启用；sidecar 显式 `everyFrame` 光源禁用 cache，`onEnable/onDemand` 可缓存；未配置光源按全局 cache 开关处理。
- cache key 为场景内容 hash（object world/mesh/submesh material、material alpha/cutout、selected lights 的变换/颜色/intensity/range/shadow settings、punctual request layout）；object/light/material/sidecar 变化即整帧重绘。方向光 request 与相机不进入 cache key，因此相机移动不失效 punctual cache。
- diagnostics：`shadow_cache=on/off`、`shadow_cached=N`（point 6 faces、spot 1）；`--debug-view=punctual-shadow-atlas` 自动抑制 cache 以显示每帧动态 atlas。
- 验证：**199 cases / 18,574 assertions**；sidecar per-light 视觉（dimmer 0.5/0.3 + tint）相对无 sidecar 基线 **39.63 dB**；D3D/VK cached vs every-frame 静态 **0 diff**（deferred/forward，max 0）；D3D/VK per-light sidecar 图 **103.52 dB**（max 1）；cache on/off 1080p 300 帧 RelWithDebInfo D3D **243.1 vs 173.9 fps**（draw 54 vs 60、shadow_drawn 8 vs 14、triangles 123 vs 165）；Vulkan 单次 **292.0–334.6 vs 348.9 fps**（波动大，M5.4 复测）。
- 边界：cache 不是 HDRP full cached atlas/mixed directional；仅 punctual/spot v1，整帧失效；cached atlas 每个 request 仍保留空 view，因此 pass 数不变、主要节省 shadow draw；不实现 stage 15 双 sampler，而由 CPU 选择 active punctual atlas。per-light dimmer/tint 只接 surface direct lighting，volumetric fog 仍使用 shadow map visibility，不消费 tint/dimmer。

### 测试 / 出口

- `tests/render/test_light_shadow.cpp`、`tests/render/test_shadow.cpp`：per-light resolution/near plane/dimmer/fade/tint/penumbra；sidecar 解析；cache dirty/invalidate；cached vs every-frame 静态 0 diff。
- 视觉：dimmer 0/0.5/1 的阴影强度；fadeDistance 近/远的 shadow 消失；tint 只改 shadow 颜色；point/spot 不同 resolution 可解释。
- 动态 caster/light 移动后 cache 必须失效，不能出现 ghost shadow；cache off/on 静态场景逐像素一致。
- 性能：静态场景 cache on 明显减少 shadow pass；动态场景自动回退，不崩溃。
- 出口：per-light 设置全部生效；cache 失效正确；D3D/VK 一致；无泄漏。

---

## M5.4 M5 验收

状态：`[x]`（2026-11-12）

目标：用最终 RelWithDebInfo 构建完成 D3D11/Vulkan 矩阵、debug、性能和文档回填；不新增功能。

### 验收矩阵

- **基线**：shadow off / `--no-shadows` 无 shadow/contact pass，资源/ pass 计数符合预期；M4.3 最终图实际带阴影，0-diff 参考不可用，见实现记录的基线说明。
- **过滤**：off/low/medium/high（PCSS）各自 D3D/VK；medium 默认；PCSS 半影随 blocker 距离变化；cascade 无硬切。
- **contact**：off/on、half/full、directional-only、4/10/16 samples；接触处有可见阴影；透明无 contact；D3D/VK 一致。
- **alpha shadow**：alphaMask cutout 物体投射镂空阴影；opaque 阴影保持不变；alpha blend 不投影。
- **per-light**：dimmer/fade/tint/resolution/near plane；sidecar 覆盖；D3D/VK 一致。
- **cache**：静态场景 cached vs every-frame 0 diff；动态 caster/light 移动后失效；`--no-shadow-cache` 回退。
- **forward/deferred**：同一 shadow 数据与 contact mask 两路径可用；差异可解释。
- **debug**：directional/punctual atlas（现有）、`contact-shadows`、`shadow-cascade`、`shadow-filter`（新增）；debug 不混入 lighting。
- **性能**：1080p 300 帧，`--no-vsync`，记录 shadow-only / full post、filter 档位、contact on/off、cache on/off；full post 需 >60fps，并记录多次范围。
- **门禁**：`ctest`、`hbg_tests`、IDL、D3D/VK `--gfx-test`、docs links、`git diff --check`；无 `leaked`。

### 截图/日志

- 复用 `m28_blend_view.gltf`（全局 shadow 回归）、`m42_local.gltf`（局部场景）。
- 临时 `m5_shadow_stress.gltf` / `m5_shadow_stress_base.gltf`（多 box、接触平面、alpha cutout、point/spot 混合）与 `m5_shadow_stress.hdrp.json` 只放临时目录，不进仓库；本轮额外使用 `shadow_plain.gltf` 隔离 sidecar，用 `m50_alpha.gltf` 变体验证 alphaMask/alpha blend。
- 输出 D3D/VK 截图、日志、PSNR/RMSE 汇总；记录所有 CLI 参数和 pipeline/frame settings。

### 实现记录（M5.4）

- 新增 `DebugDisplayMode::ShadowCascade` / `ShadowFilter` 与 `--debug-view=shadow-cascade|shadow-filter`；`debug_shadow_cascade.fs.sc` 按 HDRP `kDebugColorShadowCascade0..3` 显示 cascade 色并乘方向光 filtered visibility，`debug_shadow_filter.fs.sc` 只输出方向光 filtered visibility 灰度；两者直接读 depth/normal/shadow context，不混入 scene lighting/exposure。forward 的 prepass normal 与 deferred GBuffer normal V 方向不同，用 `u_debug_depthParams.z` 选择采样方向。DebugUI 上限从 24 扩到 32，CLI help、backend 和 debug-table 单测同步。
- 基线：`m28_blend_view.gltf` + Uffizi sky + `--no-volumetric-fog --no-shadows` 的 pass graph **无 Shadow/Contact Shadows pass**，`shadow_drawn=0`、`contact=false`、passes **30**（shadow on 为 40）；D3D/VK **64.24 dB（max 4）**。M4.3 的 `m43_fog_off` 实际带阴影（draw=83），没有可用的 M4.3 shadow-off 0-diff 参考；本记录不伪造该指标。shadow-on 对 M4.3 为 **44.92 dB（max 69）**，差异来自 M5 PCF/PCSS、bias、cascade fade、tint。
- 过滤（`shadow_plain.gltf`，1280×720，D3D/VK）：off/pcf3x3/pcf5x5/pcf7x7/pcss 全部 rc=0、无 ERROR；D3D/VK max **4–5**。相对 medium：off **65.14 dB**、pcf3x3 **66.21 dB**、pcf7x7 **63.33 dB**、pcss **51.72 dB**，确认档位实际生效；PCSS 半影随 blocker 距离变化，cascade 无硬切。
- contact（同场景，`--shadow-profile=config/shadows.json`）：full D3D/VK **56.42 dB（max 13）**，half **56.42 dB（max 4）**；full 相对 off **52.05 dB（changed 309）**，directional-only 相对 full **67.98 dB（changed 82）**，4 vs 16 samples **50.49 dB（changed 442）**；`--debug-view=contact-shadows` D3D/VK **64.42 dB（max 255，1 pixel edge）**。
- alpha（`m50_alpha.gltf` 变体，shadow-filter debug）：alphaMask 相对 opaque **21.46 dB（changed 13,496）**，阴影可见镂空；alpha blend 相对 opaque **18.06 dB（changed 24,635）**，不投射阴影；opaque 阴影保持实心。
- per-light/cache（`m5_shadow_stress.gltf` 临时场景 + sidecar）：sidecar 相对无 sidecar **41.59 dB（max 124）**；cache on/off D3D/VK **0 diff**；cache on D3D/VK **56.43 dB（max 4）**；`shadow_cache=true shadow_cached=6`。动态 caster/light 失效由 `test_light_shadow` 内容 hash 单测覆盖。
- forward/deferred：contact on 时 D3D **49.97 dB（max 142，changed 20,879）**；shadow-filter debug forward/deferred **51.67 dB（changed 333）**，shadow-cascade **55.91 dB（changed 329）**；D3D/VK debug：cascade **106.53 dB（max 2）**、filter **104.77 dB（max 1）**。差异集中在边缘/V 翻转，非 lighting 混合。
- 性能（RelWithDebInfo，1080p，300 帧，`--no-vsync`）：shadow-only D3D/VK **205.3/396.3 fps**；full post **180.5–242.1/218.3–288.5 fps**，24 个 post passes，均 >60fps。filter D3D/VK：pcf3x3 **286.0/355.2**、pcf5x5 **267.2/340.5**、pcf7x7 **282.7/337.2**、pcss **249.3/332.4** fps。contact off/on D3D **226.5–260.7/202.1–248.7**、VK **284.1–354.1/288.5–360.5** fps（简单场景波动大）。cache off→on D3D **268.2→288.3/282.3**、VK **345.3→324.7/343.4** fps（VK 无稳定收益）。
- 门禁：**199 cases / 18,580 assertions**、`ctest` 2/2、D3D/VK `--gfx-test`、`hbg_idl_check`、docs links、`git diff --check` 通过；运行日志无 `leaked`。截图/日志在 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m54`，临时 stress 场景在 `C:\Users\chenfuling\AppData\Local\Temp\hbg_m5_stress`，均未进仓库。

### 边界

- 无 DXR/RT screen-space shadows；HDRP `useScreenSpaceShadows` 是 ray tracing only。
- 无 lightmap/shadow mask；无面积光 shadow/cookie/light layers；无透明/次表面 shadow；无 path tracing。
- contact shadow 是屏幕空间近似，屏幕外/背后遮挡会漏；half-res 有轻微噪声；不做 ray-traced contact shadow。
- cached punctual v1 只做静态路径；动态场景回退；无 HDRP cached directional/mixed atlas。
- shadow culling 是 CPU v1；不做 GPU-driven/indirect shadow draw。
- `shadow-cascade` / `shadow-filter` 只可视化主方向光，不提供逐光源 filter debug、shadow mask 或 lighting 叠加；输出 raw visibility/cascade 色，不经过曝光。
- forward prepass normal 与 deferred GBuffer normal 的 V 方向不同，用 `u_debug_depthParams.z` 区分；debug 在几何边缘/V 翻转处允许少量 D3D/VK 差异（本次最大 2 级、1–2 像素）。
- contact debug 的 D3D/VK 最大差异集中在 1 个边缘像素（max 255），lit 结果双后端 max 13。
- M4.3 最终图带阴影，仓库/临时目录没有 shadow-off 0-diff 参考；M5.4 以无 shadow/contact pass 和双后端 no-shadow 图作为基线门禁。

---

## 实施顺序与风险

1. M5.0：settings/context layout → atlas/distance → culling → alpha test；先保证 shadow off 和现有 shadow 画面不回归。
2. M5.1：PCF tent → PCSS → bias/cascade fade → micro shadow；每一步保留 off/low 基线。
3. M5.2：contact shadow depth/输入 → compute → packed mask → light loop → debug。
4. M5.3：per-light data → dimmer/tint/fade → resolution/near plane → cached punctual v1。
5. M5.4：矩阵/性能/文档；不把待实现项写成已完成。

风险：

- PCSS/contact 采样开销可能拉低 full post；用 quality 档位、half-res、sample count 上限和 cache 缓解。
- contact shadow 的深度源与现有 min/max pyramid 不兼容；先做 R32F half-res 深度副本，避免把 min/max 当真实深度。
- shadow atlas UV 越界会串灯；所有 filter/PCSS 采样必须 clamp 到 request tile bounds（HDRP 同样处理）。
- cached shadow 失效不完整会出现 ghost；v1 保守整帧回退，优先正确性。
- D3D/VK 对 dynamic sampler/array/loop 的限制不同；每个 shader 改动都要双后端截图验证。
