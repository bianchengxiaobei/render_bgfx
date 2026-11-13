# M4 体积雾实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M4.0/M4.1/M4.2/M4.3 完成**。
> 原则：先读 HDRP 源码再实现；本文只做索引，HDRP 行为以源码为准。
> 参考：
> - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
> - HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
> - HDRP Volumetric Lighting：`Lighting/VolumetricLighting/HDRenderPipeline.VolumetricLighting.cs`
> - HDRP Fog Volume：`Lighting/AtmosphericScattering/Fog.cs`
> - HDRP 数学：`com.unity.render-pipelines.core/ShaderLibrary/VolumeRendering.hlsl`

---

## M4 范围与路线

M4 的目标是把 M2/M3 的“表面光照 + 后处理”扩展到相机到表面之间的参与介质。M4 不试图一次实现 HDRP 的全部 froxel/local fog，而是按可验证的垂直切片推进：

- **M4.0 Global Volumetric Fog v1**：全局指数高度雾；沿相机射线做 screen-space ray march；方向光阴影 in-scattering；Cornette-Shanks 相位；HDR 合成；设置、CLI、配置、单测和双后端验收。
- **M4.1 Fog quality/optimization**：低分辨率 fog + depth-aware upsample、步进抖动与 temporal reprojection、方向光之外的点/聚光 in-scattering。
- **M4.2 Local fog volumes**：sidecar 本地雾体积、OBB/球体 falloff、density mask；对齐 HDRP `LocalVolumetricFog` 的局部贡献，但不是完整 VBuffer。
- **M4.3 M4 验收**：D3D11/Vulkan 矩阵、性能、debug、文档、边界。

### M4.0 明确不做

- 不实现 HDRP 完整 3D VBuffer/froxel 管线（`VolumeVoxelization.compute` + `VolumetricLighting.compute`）。
- 不做局部 fog volume、VFX fog、fog volume material、体积云。
- 不做多方向光/点光/聚光体积光；v1 只处理 primary directional light。
- 不做 temporal reprojection、blue-noise/jitter 时间序列、低分辨率重构。
- 不做 PBR atmosphere、sky color mip fog、planet/altitude 大气。
- 不改变 M3 后处理顺序；fog 在 scene color 之后、postprocess 之前。

---

## HDRP 参考与 M4.0 取舍

HDRP 的完整链路是：

```text
Depth/Transparent Prepass
  -> Height Fog Voxelization (3D VBuffer density)
  -> Local Fog/VFX Voxelization
  -> Volumetric Lighting (3D VBuffer lighting + filtering/history)
  -> Opaque/Transparent Atmospheric Scattering (sample VBuffer + distant height fog)
  -> PostProcess
```

参考位置：

- `Fog.cs:17/45/47/50/55/58/61/63/72`：enabled、baseHeight、maximumHeight、meanFreePath、enableVolumetricFog、albedo、globalLightProbeDimmer、depthExtent、anisotropy 默认值。
- `Fog.cs:207-273`：`ScaleHeightFromLayerDepth(d) = d * 0.144765`，`H = max(0.01, maximumHeight - baseHeight) * 0.144765`，`_HeightFogExponents=(1/H,H)`，`extinction=1/meanFreePath`，`scattering=extinction*albedo`。
- `VolumeVoxelization.compute:51-99`：按 voxel 世界高度采 `ComputeHeightFogMultiplier`，写 `(scattering.rgb, extinction)`。
- `VolumetricLighting.compute:157-290`：方向光 in-scattering、shadow、Cornette-Shanks phase。
- `VolumetricLighting.compute:574-774`：沿 ray 积分 optical depth/transmittance/radiance，写 VBuffer。
- `AtmosphericScattering.hlsl:241-333`：opaque 采样 VBuffer，transmittance 与 in-scattering 合成；远处 height fog fallback。
- `com.unity.render-pipelines.core/ShaderLibrary/VolumeRendering.hlsl:128-193`：height fog multiplier/optical depth；`:271-304`：Cornette-Shanks。

M4.0 保留这些公式和语义，但把 3D VBuffer 替换为**单 pass screen-space ray march**：

- 在 render resolution 对每个像素沿相机射线做固定步数积分。
- 每个 sample 用 HDRP height fog multiplier、extinction、albedo、primary directional light 和 shadow。
- 输出 `sceneColor * transmittance + inScattering`，直接进入 M3 postprocess。
- 视觉上覆盖全局高度雾和方向光体积光；不声称等价于 HDRP VBuffer 的质量/性能。

---

## M4.0 数据契约

新增 `VolumetricFogSettings`：

```cpp
struct VolumetricFogSettings {
    bool enabled = false;
    float meanFreePath = 400.0f;   // HDRP Fog.meanFreePath
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    float baseHeight = 0.0f;
    float maximumHeight = 50.0f;
    float anisotropy = 0.0f;       // HDRP 默认；config profile 使用 0.5 以增强光柱
    float depthExtent = 64.0f;     // ray march 最大距离
    uint32_t stepCount = 16;
    bool directionalShadows = true;
    float globalLightProbeDimmer = 1.0f;
};
```

派生值：

```text
H          = max(0.01, maximumHeight - baseHeight) * 0.144765
rcpScaleH  = 1 / H
extinction = 1 / max(meanFreePath, 1)
scattering = albedo * extinction
phase      = CornetteShanks(anisotropy, dot(L, rayDir))
```

fog uniform（`config/uniforms/fog.idl`，4×vec4）：

```text
params  = (baseHeight, rcpScaleH, extinction, anisotropy)
params2 = (depthExtent, stepCount, enabled, globalLightProbeDimmer)
albedo  = (albedo.rgb, 1)
params3 = (directionalShadows ? 1 : 0, 0, 0, 0)
```

---

## M4.0 实现结构

```text
shaders/passes/volumetric_fog.fs.sc
config/uniforms/fog.idl
src/render/atmosphere/volumetric_fog_settings.h/.cpp
config/volumetric_fog.json
```

`ForwardRenderer`：

- 新增 `setVolumetricFogSettings()`；内部 `sanitize`。
- 在 `recordForwardFrame`/`recordDeferredFrame` 之后、`PostProcessChain::record` 之前调用 `addVolumetricFogPass`。
- 只有同时满足 `FrameSettings::AtmosphericScattering`、`FrameSettings::Volumetrics`、`PipelineSettings::supportVolumetrics`、`settings.active()` 且 fog shader 可用时才执行。
- debug view 为 `None` 时才应用；M4.0 不把 fog 混入 normal/depth/SSAO 等 debug 输出。
- fog pass 读 scene color、depth、directional shadow atlas、shadow context；输出新的 HDR color。
- `render performance` 增加 `fog={...}`、`fog_steps`；`postprocess_ms` 仍只统计 M3 postprocess，不包含 fog。

Shader 采样约定：

- scene color 用 `(u, 1-v)`；depth/world reconstruction 用 `(u,v)`（与 SSAO/DOF 一致）。
- raw depth 为远平面时按 `depthExtent` 积分，保证 sky 也受 fog 影响。
- directional shadow 用 `hbgSampleDirectionalShadow(worldPos, -rayDir)`；无阴影时 `u_pass_shadowParams.z=0` 直接返回 1。
- 方向光相位使用 HDRP 的 `cosTheta = dot(L, rayDir)`（rayDir 为相机到 sample），不是 `dot(L, -rayDir)`。
- phase 用 HDRP `CornetteShanksPhasePartConstant * CornetteShanksPhasePartVarying`；不要用 HG 替代。

---

## M4.0 设置与开关

- `config/volumetric_fog.json`：M4.0 配置（enabled=true）。
- `config/pipeline.json`：`supportVolumetrics=true` 作为能力；默认 `frame_settings.json` 的 `Volumetrics=false`，因此 baseline 画面不变。
- CLI：`--volumetric-fog`/`--no-volumetric-fog`、`--fog-profile=<path>`、`--fog-mean-free-path`、`--fog-base-height`、`--fog-maximum-height`、`--fog-anisotropy`、`--fog-depth-extent`、`--fog-steps`、`--no-fog-shadows`。
- `--volumetric-fog` 同时设置 pipeline `supportVolumetrics` 和 FrameSettings `AtmosphericScattering`/`Volumetrics`。
- M4.0 不增加 DebugUI fog 面板；CLI/JSON 足够验收。

---

## M4.0 验收

- 关闭 fog 时不产生 fog pass，1280×720 与 M3.8 基线 D3D11 **96.87 dB**（29 个像素 >0.5，来自 PNG/采样舍入）、Vulkan **0 diff**；`--no-volumetric-fog` / FrameSettings Volumetrics=false。
- 开启 fog 后，D3D11/Vulkan 同场景 PSNR ≥ 60 dB；方向光阴影产生可见光柱/遮蔽差异。
- `meanFreePath` 变小、`anisotropy` 变化、`baseHeight/maximumHeight` 变化均产生可解释变化；NaN/负值被 sanitize。
- 单测覆盖：settings defaults/sanitize、JSON 解析、CLI 开关与参数、FrameSettings gate、height fog 派生值（`scaleHeight`/`extinction`/`scattering`）。
- `ctest`、`hbg_idl_check`、D3D11/Vulkan `--gfx-test`、docs links、`git diff --check` 通过。
- 记录 1080p fog-only/full postprocess 的 `fps`、`postprocess_ms`、`fog` 状态和 render pass 数；`postprocess_ms` 不混入 fog GPU 时间。
- 边界：screen-space raymarch 不是 HDRP VBuffer；无 temporal/jitter/低分辨率重构/局部雾/点光体积光。

---

## M4.0 实现结果

实现：

- `VolumetricFogSettings` / JSON profile、`config/volumetric_fog.json`、`config/uniforms/fog.idl`。
- `shaders/passes/volumetric_fog.fs.sc`：height fog + 16-step screen raymarch + primary directional shadow + Cornette-Shanks + SH9 ambient。
- `ForwardRenderer::addVolumetricFogPass` 在 scene color 之后、postprocess 之前输出新的 HDR color；`FrameSettings::AtmosphericScattering/Volumetrics` 与 `PipelineSettings::supportVolumetrics` 门控。
- `render performance` 增加 `fog`/`fog_steps`；`postprocess_ms` 仍只统计 M3 postprocess。
- CLI `--volumetric-fog`、`--no-volumetric-fog`、`--fog-profile` 和 8 个 fog 参数；`--fog-profile` 单独使用也会开启对应 FrameSettings。
- 修复 sky 像素的 ray direction：用 `u_global_nonJitteredInvViewProj` 重建远平面方向；几何像素用实际 `worldDistance` 归一化，避免超过 `depthExtent` 后步进长度错误。

结果（RTX 2060 SUPER，RelWithDebInfo；主 scene `m28_blend_view.gltf` + `uffizi-large.exr`）：

| 用例 | 结果 |
|---|---|
| fog off vs M3.8 base | 1280×720 D3D11 **96.87 dB**（29 像素 >0.5），Vulkan **0 diff**；无 fog pass（40 passes） |
| fog on D3D/VK | 1280×720 **64.36 dB**；pass=41；`fog=true, fog_steps=16` |
| forward fog D3D/VK | **64.36 dB**；forward/deferred **56.75 dB** |
| fog + full post D3D/VK | 1080p **69.09 dB**；passes=64，`post_passes=24` |
| 0.5 render scale | fog 在 640×360、final 1280×720；D3D/VK **64.35 dB** |
| fog on/off | 1280×720 D3D11 **39.03 dB**（fog 有效） |
| directional shadows on/off | meanFreePath=50 时 **35.84 dB**（阴影对 in-scattering 有显著影响） |
| 1080p fog-only 300 帧 | D3D11 **203.5 fps** / Vulkan **199.6 fps**；无 fog 基线 **264.5 / 320.0 fps** |
| 1080p full post + fog 300 帧 | D3D11 **106.8–140.2 fps** / Vulkan **142.3–160.0 fps**；post wall **0.426–0.550 / 0.374–0.512 ms**，不包含 fog GPU 时间 |

验证：

- `hbg_tests` **170/170，18,002 assertions**；`ctest` **2/2**；`hbg_idl_check` 通过。
- D3D11/Vulkan `--gfx-test` 通过；render-test `liveResourceCount=0`。
- docs links、`git diff --check` 通过。

M4.0 边界：

- 不是 HDRP 3D VBuffer/froxel；是单 pass screen-space raymarch，全分辨率、默认 16 步（可 4..64）。
- 无 temporal reprojection/jitter、低分辨率重构、局部雾、VFX fog、点/聚光体积光、体积云。
- 只有 primary directional light 的 shadow；阴影为单 tap volumetric 采样，不是 HDRP ultra-low tile 邻域。
- 无 fog DebugUI/Debug view；CLI/JSON 验收；RenderDoc/F11 仍缺失。
- `postprocess_ms` 只统计后处理 graph，不包含 fog；fog 的 GPU 成本只能通过整帧 fps/`gpu_frame_ms` 对比估算。

截图/日志（`C:\Users\chenfuling\AppData\Local\Temp\codemaker`）：

- `m4_fog_final_d3d/vk.png`、`m4_fog_off_d3d.png`、`m4_fog_shadow_final_d3d.png`、`m4_fog_noshadow_final_d3d.png`。
- `m4_fog_forward_final_d3d/vk.png`、`m4_fog_scale05_final_d3d/vk.png`。
- `m4_perf_fog_full_final_d3d/vk.png`；性能日志 `m4_perf_*.log`。

---

## M4.1 低分辨率 / Temporal / 点聚光体积光

### HDRP 参考

- `Lighting/AtmosphericScattering/Fog.cs:84-166`：`screenResolutionPercentage`、`volumeSliceCount`、`resolutionDepthRatio` 和 `directionalLightsOnly`；M4.1 把 screen fraction 简化为 `resolutionScale`，不做 3D slice budget。
- `Lighting/VolumetricLighting/HDRenderPipeline.VolumetricLighting.cs:366-412`：screen fraction/slice count 与 viewport 计算；`:132-153`：7 帧 EMA，`historyWeight=(n-1)/n=6/7`；`:678-737`：reprojection、Pixar blend 和 phase 分离；`:1352-1512`：VBuffer lighting/reprojection/filtering pass。
- `Lighting/VolumetricLighting/VolumetricLighting.compute:132-153`：temporal EMA；`:678-737`：history 与 feedback；`:291-499`：point/spot light 的 ray-cone、重要性采样、shadow 和 transmittance。
- `Lighting/VolumetricLighting/VolumetricLightingFiltering.compute:32-157`：3D Gaussian filtering；M4.1 用 2D depth-aware bilateral upsample 代替。
- HDRP 的 VBuffer 是 3D froxel；M4.1 保持 screen-space 2D 中间纹理，因此不能声称等价于 HDRP 的 VBuffer 质量。

### 实现

```text
shaders/passes/volumetric_fog.fs.sc            低分辨率 raymarch，输出 (inScattering.rgb, transmittance)
shaders/passes/volumetric_fog_composite.fs.sc  全分辨率 depth-aware 4-tap upsample + scene composite
src/render/atmosphere/volumetric_fog_settings.h/.cpp
config/volumetric_fog.json
config/uniforms/fog.idl
```

- `VolumetricFogSettings` 新增：
  - `resolutionScale=0.5`（0.25..1.0）
  - `temporal=true`、`temporalHistoryWeight=0.85`、`temporalJitter=true`
  - `punctualLights=true`、`punctualShadows=true`、`maxPunctualLights=8`
- raymarch 在 `max(1, round(renderSize * resolutionScale))` 上执行；输出 `RGBA16F`，RGB=in-scattering，A=transmittance；不再直接吃 scene color。
- composite 在全分辨率读取 scene color + low-res fog + depth；4 个低分辨率 tap 用 bilinear × depth weight（容差 `max(2% center view depth, 0.05)`）归一化，再输出 `sceneColor * T + S`。
- temporal history 为低分辨率 `RGBA16F` 双缓冲：
  - motion 采样沿用 TAA 的 source-space 约定：`sourceUv-(m.x,-m.y)`；
  - 用 current/previous depth 做 depth rejection，历史权重上限 0.95；
  - no-history / offscreen / temporal off 时不读 history；
  - `temporalJitter` 开启时按 low-res pixel + frame index hash 抖动 step 中心；关闭或 temporal off 时固定居中采样。
- point/spot：
  - 优先读 32-slice cluster light list；不可用时回退 tile light list，再回退 punctual 全列表；
  - 每个 sample 按 range attenuation、spot cone、Cornette-Shanks phase 积分；
  - `punctualShadows` 开启且 light 标记 cast shadow 时，采样现有 punctual shadow atlas（`-rayDir` 作为 view-bias normal）；
  - 默认最多 8 个 punctual lights/sample。
- uniform 扩展为 6×vec4：`params3=(directionalShadows,punctualLights,temporal,punctualShadows)`、`params4=(lowResWidth,lowResHeight,historyWeight,historyValid)`、`params5=(maxPunctualLights,temporalJitter,useCluster,0)`。
- `render performance` 新增 `fog_scale`、`fog_lowres=WxH`、`fog_temporal`、`fog_history`、`fog_lights`；`postprocess_ms` 仍只统计 M3 postprocess，fog 不计入。
- fog 仍位于 scene color 之后、M3 postprocess 之前；关闭时仍不创建 fog pass/history，fog off 与 M4.0 基线一致。

### 设置与 CLI

- `config/volumetric_fog.json` 已切换到 M4.1 profile（`resolutionScale=0.5`、temporal、punctual lights/shadows）。
- 新增 CLI：`--fog-resolution-scale=<0.25..1>`/`--fog-scale`、`--fog-temporal`/`--no-fog-temporal`、`--fog-temporal-weight=<0..0.95>`、`--fog-jitter`/`--no-fog-jitter`、`--fog-punctual-lights`/`--no-fog-punctual-lights`、`--fog-punctual-shadows`/`--no-fog-punctual-shadows`、`--fog-max-punctual-lights=<0..16>`。
- 保留 M4.0 的 `--fog-profile`、`--fog-mean-free-path`、`--fog-base-height`、`--fog-maximum-height`、`--fog-anisotropy`、`--fog-depth-extent`、`--fog-steps`、`--fog-shadows`。

### M4.1 验收结果

RTX 2060 SUPER；图像 `m28_blend_view.gltf` + `uffizi-large.exr`，性能 RelWithDebInfo 1080p 300 帧。

| 用例 | 结果 |
|---|---|
| fog off vs M4.0 | D3D11 **96.87 dB**（29 像素 >0.5），Vulkan 与 M4.0 fog off 一致；无 fog low-res/history pass |
| no temporal D3D/VK | 1280×720 **64.40 dB**；passes=42 |
| temporal D3D/VK | 1280×720 **62.10 dB**；`fog_lowres=640x360`、`fog_temporal=true`、`fog_history=true` |
| temporal off vs on, static + no jitter | **0 diff**（history 与 no-history 路径一致，验证 reprojection 方向正确） |
| no-point fog vs M4.0 | scale 1.0 **66.39 dB**、scale 0.5 **65.59 dB**（0.5 时 low-res `640×360`） |
| fog + full post 1080p D3D/VK | **66.32 dB**；passes=65、`post_passes=24` |
| point-light fog D3D/VK | dense test **103.52 dB**；point/spot scatter 均可见 |
| full post 1080p 300 帧 | D3D11 **154.6 fps** / Vulkan **156.4 fps**；post wall **0.381 / 0.343 ms** |
| fog-only 1080p 300 帧 | D3D11 **170.0 fps** / Vulkan **189.5 fps**；无 fog **232.7 / 224.0 fps** |

验证：

- `hbg_tests` **171/171，18,037 assertions**；`ctest` **2/2**；`hbg_idl_check` 通过。
- D3D11/Vulkan `--gfx-test` 通过；render-test `liveResourceCount=0`；fog off 无 history/fog pass。
- `fog_lowres` 在 1080p/0.5 为 `960×540`，1280/0.5 为 `640×360`；`fog_scale=0.5`、`fog_temporal`、`fog_history`、`fog_lights` 可见。
- `--fog-temporal-weight`、`--fog-jitter`、`--fog-punctual-lights`、`--fog-punctual-shadows`、`--fog-max-punctual-lights` 均有单测覆盖。

图像/日志：

- `m41_fog_def_d3d/vk.png`、`m41_fog_def_notemp_vk.png`、`m41_scale05_nopoint_d3d.png`、`m41_scale1_nopoint_d3d.png`。
- `m41_pointfog_on/off_d3d.png`、`m41_pointdense_on/off_d3d.png`、`m41_pointdense_vk.png`、`m41_spot2fog_on/off_d3d.png`。
- `m41_orbit16_d3d.png`、`m41_orbit16_off_d3d.png`、`m41_perf_*.png`、`m41_perf_*.log`。

M4.1 边界：

- 仍是 2D screen-space S/T，不是 HDRP 3D VBuffer/froxel；低分辨率 upsample 是 depth-aware 4-tap，不是 froxel filtering。
- temporal 是 2D low-res history；没有 HDRP VBuffer 的 3D reprojection、phase 分离和反馈体素；动态光源/严重 disocclusion 仍可能 ghost。
- point/spot 是逐 step 的 cluster/tile 近似积分，不是 HDRP 的 ray-cone + importance sampling；没有 area light/box light。
- punctual shadow 复用现有 atlas 和单次请求，不是 HDRP volumetric ultra-low 邻域；无 local fog/VFX fog/volumetric clouds。
- 无 fog DebugUI/Debug view；RenderDoc/F11 仍缺失。

---

## M4.2 Local fog volumes

### HDRP 参考

- `Lighting/VolumetricLighting/LocalVolumetricFog.cs`：`LocalVolumetricFogArtistParameters` 的 `albedo`/`meanFreePath`/`positiveFade`/`negativeFade`/`invertFade`/`falloffMode`/`distanceFadeStart`/`distanceFadeEnd`/`volumeMask`/`textureTiling`；`ConvertToEngineData()` 计算 `rcpPosFaceFade`/`rcpNegFaceFade`、`rcpDistFadeLen` 和 `endTimesRcpDistFadeLen`。
- `Lighting/VolumetricLighting/HDRenderPipeline.VolumetricLighting.cs:855-920`：收集可见 local fog volume，按 OBB 缩放/包围球距离剔除，构建 OBB AABB 并上传可见 volume 数据/索引；M4.2 只保留 sidecar 上传和前 8 个体积，不做 HDRP 的 froxel 可见性缓冲。
- `Editor/Material/FogVolume/ShaderGraph/ShaderPassVoxelize.hlsl:105-165`：voxel 中心转到 OBB 空间，`ComputeVolumeFadeFactor` 做正面/负面 blend、距离 fade 和 exponential falloff。
- `com.unity.render-pipelines.core/ShaderLibrary/VolumeRendering.hlsl:432-466`：`ComputeVolumeFadeFactor`、`ApplyExponentialFadeFactor`、`ExtinctionFromMeanFreePath`；M4.2 直接把同一组公式用于 screen-space raymarch 的 sample。
- HDRP 的 density mask 是 3D Texture3D；M4.2 使用最多 4 张 2D 投影 mask，作为当前基础设施可承载的中间切片，不声称等价于 HDRP 3D mask/froxel。

### 实现

```text
src/render/atmosphere/local_fog_volume.h/.cpp   sidecar 数据、OBB/球体 falloff、sanitize
src/asset/scene_loader.cpp                      localFogVolumes + maskTexture 解析
shaders/passes/volumetric_fog.fs.sc             局部密度/散射积分 + 2D density mask
config/uniforms/fog.idl                         params6 + 8 组 local volume + mask 参数
```

- 新增 `LocalFogVolume`：`shape=box|sphere`、`position`、`rotation`（度，ZYX，与 decal 一致）、`size`（完整尺寸）、`albedo`、`meanFreePath`、`positiveFade`/`negativeFade`（归一化 0..1，HDRP 语义）、`invertFade`、`falloffMode=linear|exponential`、`distanceFadeStart/End`、`maskTexture`、`maskTiling`、`maskScroll`、`maskPlane=xy|xz|yz`、`maskIntensity`、`maskInvert`。
- box 使用 HDRP 的 OBB 变换和正面/负面 face fade；sphere 使用归一化椭球半径，`positiveFade.x` 作为径向边缘宽度；两者都应用距离 fade 和 HDRP 的 `pow(fade, 2.2)` exponential 曲线。
- 每个 raymarch sample 累加 local extinction 和 `albedo * extinction` scattering；与全球 height fog 合并为 `totalExtinction`，用 `scatteringCoefficient / totalExtinction` 得到有效 albedo，再乘方向光/点聚光/SH radiance。局部体积使用全局相位，因为 HDRP 本身标注 local fog anisotropy 尚未开放。
- sidecar `maskTexture` 相对 sidecar 目录加载；最多 4 张唯一 mask，绑定到 fog shader 的 slot 10-13；无 mask 时用白色 fallback 且 intensity=0。mask UV 由局部坐标按 `maskPlane` 投影、`maskTiling/maskScroll` 变换；采样使用 `texture2DLod(...,0)`，避免在 raymarch loop 内产生 gradient 指令。
- `fog.idl` 新增 `params6=(localVolumeCount,localMaskCount,localVolumesEnabled,maxLocalVolumes)`，以及 8 组 `localObbRight/Up/Extents/Center`、`localScattering`、`localFaceFadePos/Neg`、`localDistanceFade`、`localMaskTiling/Scroll`。
- `ForwardRenderer::init()` 上传 sidecar mask 纹理；`ensureLocalFogMaskTextures()`/`releaseLocalFogMaskTextures()` 管理资源。`render performance` 新增 `fog_local`、`fog_local_masks`。
- CLI 新增 `--fog-local-volumes`/`--no-fog-local-volumes`；`config/volumetric_fog.json` 增加 `localVolumes=true`。侧车示例：

```json
{
  "localFogVolumes": [{
    "name": "mist",
    "shape": "box",
    "position": [0.0, 1.2, -1.2],
    "rotation": [0.0, 0.0, 0.0],
    "size": [4.5, 2.8, 3.6],
    "albedo": [0.8, 0.9, 1.0],
    "meanFreePath": 3.5,
    "positiveFade": [0.25, 0.25, 0.25],
    "negativeFade": [0.25, 0.25, 0.25],
    "falloffMode": "linear",
    "distanceFadeStart": 0.0,
    "distanceFadeEnd": 40.0,
    "maskTexture": "fog_mask.png",
    "maskPlane": "xz",
    "maskTiling": [2.0, 2.0, 1.0],
    "maskIntensity": 0.75
  }]
}
```

### M4.2 验收结果

RTX 2060 SUPER；`m42_local.gltf` 使用 `m28_blend_view.gltf` 的几何/相机并带 1 个 local fog volume；主 fog 校验使用 `config/volumetric_fog.json`、`--no-fog-temporal` 或 16 帧 temporal；性能为 RelWithDebInfo 1080p 300 帧。

| 用例 | 结果 |
|---|---|
| M4.1 no-local 回归（带 sky，1280×720） | 对 `m41_fog_def_d3d` **94.77 dB**（max 2，52 px >0.5） |
| local box D3D/VK（1280×720） | **82.34 dB**；`fog_local=1`、`fog_local_masks=1` |
| local box on/off（D3D） | **32.90 dB**（max 96，120,089 px >0.5） |
| local sphere D3D/VK | **82.33 dB**；sphere on/off **51.22 dB** |
| density mask on/off（D3D） | **47.54 dB**（max 15，94,750 px >0.5） |
| local temporal D3D/VK（16 帧） | **62.71 dB**；`fog_lowres=640x360`、`fog_history=true` |
| local full post D3D/VK（16 帧，quality） | **64.91 dB**；passes=65、`post_passes=24` |
| 1080p 300 帧 fog-only | local D3D11 **215.3 fps** / Vulkan **206.7 fps**；同场景 no-local **213.6 / 208.8 fps** |
| 1080p 300 帧 full post | local D3D11 **179.8 fps** / Vulkan **170.4 fps**；post wall **0.337 / 0.333 ms**；no-local D3D **181.2 fps** |

验证：

- `hbg_tests` **177/177，18,103 assertions**；`ctest` **2/2**；`hbg_idl_check` 通过。
- D3D11/Vulkan `--gfx-test` 通过；local volume/mask 资源在 shutdown 后 `liveResourceCount=0`。
- `--fog-local-volumes`/`--no-fog-local-volumes`、`--fog-scale`、temporal/punctual 标志组合无崩溃；`fog_local`/`fog_local_masks` 可见。
- 截图/日志：`m42_m41_regress_sky_d3d.png`、`m42_box_d3d/vk.png`、`m42_box_off_d3d.png`、`m42_nomask_d3d.png`、`m42_sphere_d3d/vk.png`、`m42_sphere_off_d3d.png`、`m42_local_temporal_d3d/vk.png`、`m42_full_d3d/vk.png`、`m42_perf_*.log`。

M4.2 边界：

- 仍是 screen-space 2D raymarch，不是 HDRP 3D VBuffer/froxel；没有局部雾的 voxelization、3D filtering、phase 分离或 VBuffer history。
- 不是 Unity 组件导入：sidecar 静态中心/旋转/尺寸，无层级 scale、动画、VFX/local fog material、priority/blending mode（只做 additive density）。
- density mask 是 2D 投影，最多 4 张唯一纹理；不是 HDRP Texture3D，也没有 3D atlas/scroll 体素采样。
- sphere 是归一化椭球近似，不是 HDRP 原生 sphere local fog；局部 anisotropy 未使用（HDRP 也未开放 local fog anisotropy）。
- 只上传前 8 个 enabled volume，未做 CPU frustum culling/OBB 可见列表；无 runtime 热重载；无 local fog Debug view；RenderDoc/F11 仍缺失。

---

## M4.3 M4 验收、性能、文档

状态：`[x]`

目标：完成 M4.0–M4.2 的双后端矩阵、性能、debug/边界和文档回填，使用最终 RelWithDebInfo 构建验收；不新增 M4 功能。

参考：HDRP `RenderPipeline/HDRenderPipeline.RenderGraph.cs:258`（volumetric lighting 在 transparent 之前）、`Lighting/AtmosphericScattering/Fog.cs:84-166`、`Lighting/VolumetricLighting/HDRenderPipeline.VolumetricLighting.cs:855-920`、`com.unity.render-pipelines.core/ShaderLibrary/VolumeRendering.hlsl:432-466`。M4.3 只核对已有实现的 HDRP 公式/顺序，不声称等价于 HDRP 3D VBuffer/froxel。

验收矩阵（1280×720，RelWithDebInfo，主场景 `m28_blend_view.gltf` / `m42_local.gltf` + Uffizi sky；M4.3 数值统一带 sky，不能与 M4.2 的无 sky 特写 PSNR 直接比较）：

| 用例 | 结果 |
|---|---|
| fog off vs M3.8（D3D/VK） | 两侧均 **0 diff**；无 fog pass、`fog=false`、passes=40 |
| M4.1 no-local 回归（D3D） | 对 `m41_fog_def_d3d` **96.21 dB**（max 1，43 px >0.5）；VK 对 `m41_fog_def_notemp_vk` **0 diff** |
| global no-local D3D/VK | **62.71 dB**；`fog_local=0`、`fog_lowres=640x360` |
| temporal D3D/VK | **64.41 dB**；`fog_temporal=true`、`fog_history=true` |
| local box D3D/VK | **62.72 dB**；`fog_local=1`、`fog_local_masks=1` |
| local box on/off D3D | **33.40 dB**；可见局部密度/光柱变化 |
| local sphere D3D/VK | **62.71 dB**；sphere on/off **53.24 dB** |
| local temporal D3D/VK | **62.71 dB**；local + history/reprojection 同时启用 |
| density mask on/off D3D | **48.76 dB** |
| forward local D3D/VK | **62.74 dB**；forward/deferred **50.68 dB**（路径光照差异，双后端一致） |
| full post + local D3D/VK | **64.69 dB**；passes=65、`post_passes=24`、TAA/DOF/MB/Bloom/fog/local 同时启用 |
| full post local on/off | D3D **36.89 dB**、VK **36.88 dB** |
| point/spot fog D3D/VK | point **84.44 dB**（on/off **37.61 dB**）、spot **96.75 dB**（on/off **50.79 dB**） |
| transparent 场景 D3D/VK | **93.46 dB**；无崩溃、无资源泄漏 |
| `--debug-view=normal` fog/local on/off | **0 diff**；debug 输出不混入 fog |

性能（1920×1080，300 帧，`--no-vsync`；完整后处理用 quality + TAA/DOF/MB/Bloom，fixed exposure）：

| 路径 | D3D11 fps | Vulkan fps | notes |
|---|---:|---:|---|
| fog-only，no fog | 271.2 | 319.0 | 无 fog pass |
| fog-only，local | 177.5 | 200.7 | 16 steps、0.5 scale、local mask |
| full post，no fog | 211.3 | 220.1 | 24 post passes |
| full post + local（3 次） | 153.1–171.8 | 156.3–176.2 | post wall **0.367–0.394 / 0.333–0.371 ms** |
| full post + local off（D3D） | 158.1 | — | 落入 local 波动范围，local 几何/采样成本很小 |

所有路径均高于 60 fps；性能存在单次运行波动（尤其 fog-only），因此 full local 报告 3 次范围，不把单次峰值当结论。`postprocess_ms` 仍是 CPU graph-record wall，不包含 fog GPU 时间。

验证：

- `hbg_tests` **177/177，18,104 assertions**；`ctest 2/2`；`hbg_idl_check`、D3D11/Vulkan `--gfx-test`、docs links、`git diff --check` 通过。
- 所有验收/性能日志无 `leaked`；render-test 正常 shutdown。
- `--fog-steps` 的 CLI 范围修正为 **4..64**（与 sanitize 一致）；`--fog-steps=3` 拒绝，`4`/`64` 接受。
- local/mask 资源在 D3D/VK 均可用；fog off 不创建 history/fog pass。

M4.3 边界：

- 仍是 screen-space 2D raymarch + 2D low-res history；不是 HDRP 3D VBuffer/froxel，也没有 voxelization/3D filtering/phase-separated VBuffer history。
- local fog 是静态 sidecar；2D 投影 density mask 最多 4 张，不是 HDRP Texture3D；只做 additive，无 blending mode/priority、VFX/local fog material、runtime 热重载、CPU frustum culling；最多 8 个 enabled volume。
- fog composite 在 transparent 之后，透明物体使用 opaque depth 近似，不是 HDRP 在 transparent forward 中采样 VBuffer 的路径；本项目 transparent shader 不重复应用 fog。
- 无 fog DebugUI/专用 fog debug view；用 diagnostics、CLI/JSON、双后端截图验收。RenderDoc/F11 仍未安装。
- M4.2 的无 sky 特写 PSNR（box 82.34 等）与 M4.3 带 sky 的集成矩阵不是同一输入，不能横向比较。

截图/日志（`C:\Users\chenfuling\AppData\Local\Temp\codemaker`）：`m43_*_d3d/vk.png`、`m43_*_d3d/vk.log`、`m43_accept.json`、`m43_accept_extra.json`、`m43_perf_*.log`。

---

## 测试矩阵

| 用例 | 期望 |
|---|---|
| `fog.enabled=false` | 无 fog pass，输出与 M3.8 基线一致 |
| `enabled=true`, 默认参数 | 高度雾 + 方向光阴影；双后端一致 |
| `meanFreePath` 400 → 50 | 密度/遮蔽增强 |
| `baseHeight`/`maximumHeight` | 低空雾层变化 |
| `anisotropy` -0.5/0.0/0.8 | 前向/后向散射变化 |
| `directionalShadows=false` | 光柱/遮挡消失，画面更均匀 |
| render scale 0.5 | fog 在 render resolution，final 仍 target resolution |
| forward/deferred | 共用 fog，差异可解释 |
| NaN/负值 | sanitize 后有限值 |
| fog shader 缺失 | 不添加 pass，保持 M3 输出并告警 |
| `resolutionScale` 0.25/0.5/1.0 | low-res 尺寸随 render size 缩放；composite 保持 target size |
| temporal off/on + 静态 no-jitter | history 路径不改变像素 |
| temporal orbit | 无持续 ghost；motion 方向正确 |
| `punctualLights=false` | fog 不采样 light list，point/spot 贡献消失 |
| `punctualShadows=false` | point/spot shadow 贡献消失，light attenuation 保留 |
| `maxPunctualLights=0` | 不采样 punctual list |
| temporal/history invalid | 只写当前 low-res fog，不读 history |
| `localFogVolumes` box/sphere | sidecar 解析并上传；`fog_local=1` |
| box 内部/边缘/外部 | 中心密度最大、face fade 递减、外部为 0 |
| sphere 半径边界 | 归一化椭球外为 0，边缘按 positiveFade.x 衰减 |
| `falloffMode=linear/exponential` | exponential 使用 `pow(fade,2.2)` |
| `distanceFadeStart/End` | 超过 End 的 sample 无 local 贡献 |
| `maskTexture` 缺失/有效 | 缺失为常量密度；有效时 mask on/off 有可见差异 |
| `--no-fog-local-volumes` | local 贡献为 0，M4.1 no-local 基线保持 |
| local volume 超过 8/ mask 超过 4 | 只上传上限并告警，不崩溃 |
| `--fog-steps=3` / `4` | 3 拒绝；4 接受并按 4..64 sanitize |
| `--debug-view=normal` + fog/local | debug 输出不混入 fog；开/关 0 diff |
| 透明场景 + fog | D3D/VK 一致且不崩溃；透明 fog 为 opaque-depth 近似，非 HDRP transparent VBuffer |
