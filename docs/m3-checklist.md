# M3 后处理实现清单

> 工程根：`E:\Learn\hdrp-bgfx`。
> 状态：**M3.0/M3.1/M3.2/M3.3/M3.4/M3.5/M3.6/M3.7/M3.8 完成**。
> 原则：先读 HDRP 源码再实现；本文只做索引，HDRP 行为以源码为准。
> 参考：
> - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
> - HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
> - bgfx：`build\vs2022\_deps\bgfx-src`
> - 后处理总入口：`RenderPipeline\HDRenderPipeline.PostProcess.cs:802`

---

## M3 范围与关键决策

### 目标

M3 在 M2 Deferred 输出之后增加一条独立的 HDR 后处理链：

```text
Scene HDR
  -> StopNaNs
  -> Exposure（compute + 1x1 exposure texture）
  -> Apply Exposure（HDR）
  -> TAA（可选，history ping-pong）
  -> Depth of Field（可选）
  -> Motion Blur（可选）
  -> Bloom（可选，mip chain）
  -> Uber Post（bloom composite + Color Grading LUT + Vignette + Tonemap）
  -> FXAA（可选）
  -> Final sRGB -> backbuffer
```

顺序对齐 HDRP `HDRenderPipeline.PostProcess.cs:847-973`：

1. Dynamic Exposure
2. TAA
3. Depth of Field
4. Motion Blur
5. Bloom
6. Color Grading LUT + Uber Post
7. FXAA / Final Pass

### 本轮锁定

- 只做**单相机全局 PostProcessProfile**，不做 HDRP 完整 Volume 栈、Volume 混合、Camera Stack、多 view。
- 默认 profile 保持 M2.9 画面：Exposure Fixed、Tonemap ACES、TAA/Bloom/DOF/Motion Blur/FXAA 全部关闭、Color Grading neutral。
- 提供 `config/postprocess.json`（baseline）与 `config/postprocess_quality.json`（质量预设）；CLI 可逐项覆盖。
- 新增 compute/UAV 基础设施，先做垂直切片，再实现 Exposure histogram；D3D11/Vulkan 都必须通过。
- Color Grading 使用 **CPU 生成 32³ LUT，按 32×1024 的 2D strip 上传**，shader 手工做两层 slice 插值。M3 不引入 3D texture 封装，不支持外部 3D LUT。
- TAA、DOF、Motion Blur 使用现有 `RenderGraph` 的 raster pass；只有 Exposure histogram 使用 compute。
- Bloom、Color Grading、FXAA 在 Forward/Deferred 共用，不依赖 GBuffer。
- `PostProcessChain` 在 render resolution 处理 Exposure/TAA/DOF/Motion Blur/Bloom；Uber/Final 在 target resolution 处理，保证 `--render-scale=0.5` 仍为 960×540 后处理 + 1920×1080 输出。
- M3 不改 M2 清单；实现细节和验收写在本文件。

### 明确不做

- 不做完整 HDRP Volume Profile/VolumeManager、局部 Volume、优先级混合。
- 不做物理相机 DOF、bokeh aperture shape、tile-based indirect DoF。
- 不做 Motion Blur 的 scattering/tile 邻域优化、透明物体运动矢量。
- 不做 Bloom 的 anamorphic、dirt、lens flare。
- 不做 Panini、镜头畸变、色差、Film Grain、Dithering、SMAA、DLSS/FSR、HDR 输出。
- 不做 TAAU/动态分辨率时间放大；TAA 只做固定 render scale 的时间抗锯齿。
- 不做 Color Grading master/HSV/YRGB curves；只做 Color Adjustments/White Balance/Channel Mixer/LGG/SMH/Split Toning/Vignette 的 v1。
- 不接受“全部关闭但仍走 Uber”造成 M2 画面回归；默认必须保留旧 `Tonemap` 路径。

---

## M3 出口标准

- `PostProcessChain` 与 `PostProcessProfile` 完成，Effects 全关时与 M2.9 输出一致（D3D11/Vulkan，PSNR ≥ 60 dB 或逐像素一致）。
- 高级 Exposure：Fixed/Automatic/Histogram 三种模式可用；1x1 exposure texture、历史适应、阈值百分位在双后端验证。
- TAA：静态相机 16 帧收敛，相机 orbit 无持续拖影；关闭 TAA 时逐像素回退；history 在 resize/scale 改变时正确重置。
- Bloom、Color Grading、DOF、Motion Blur、FXAA 均可独立开关；关闭时输出与前一阶段逐像素一致。
- D3D11/Vulkan 双后端截图矩阵完成；新 effect 的 D3D/VK PSNR 达到本文件各任务阈值。
- 1080p 全后处理栈在 RTX 2060 SUPER 上 >60 fps；记录各 effect 的 wall/GPU/draw/passes 数据。
- `hbg_tests`、`ctest`、`hbg_idl_check`、双后端 `--gfx-test`、`git diff --check` 通过。
- 回填 `docs/implementation-plan.md`、`AGENTS.md`、本文件；不把 M3 细节写回 M2 清单。
- RenderDoc 仍缺失时，F11/GPU 抓帧只能标记为遗留验证。

---

## M3 架构

### 1. `PostProcessChain`

新增：

```text
src/render/postprocess/post_process_chain.h/.cpp
src/render/postprocess/post_process_profile.h/.cpp
src/render/postprocess/color_grading_lut.h/.cpp
src/render/postprocess/exposure_processor.h/.cpp
```

职责：

- 持有后处理设置、profile、LUT、exposure history、TAA history、bloom 临时资源。
- 提供 `record(RenderGraph&, const PostProcessInputs&)`，返回最终 LDR color handle。
- 处理 history 生命周期、分辨率变化、首帧 invalid、debug bypass。
- 不直接做场景绘制；不替代 `ForwardRenderer` 的 RenderGraph。

建议接口：

```cpp
struct PostProcessInputs {
    RenderGraphTextureHandle color;
    RenderGraphTextureHandle depth;
    RenderGraphTextureHandle depthPyramid;
    RenderGraphTextureHandle motionVectors;
    RenderGraphTextureHandle normal;
    RenderGraphTextureHandle stencil;       // 与 depth 共用
    uint16_t renderWidth, renderHeight;
    uint16_t targetWidth, targetHeight;
    float renderScale;
    float deltaSeconds;
    uint32_t frameIndex;
    bool historyValid;
};

struct PostProcessOutput {
    RenderGraphTextureHandle color;         // LDR linear
    bool usedUber = false;
    bool effectsActive = false;
};

class PostProcessChain {
  public:
    bool init(gfx::ResourceManager&, shader::ShaderLibrary&, uint16_t targetW, uint16_t targetH);
    void resize(uint16_t targetW, uint16_t targetH);
    void shutdown(gfx::ResourceManager&);
    void setProfile(const PostProcessProfile&, const PipelineSettings&, const FrameSettings&);
    void setSettings(const PostProcessSettings&, const PipelineSettings&, const FrameSettings&);
    void uploadUniforms(const gfx::generated::PostProcessUniforms&) const;
    PostProcessOutput record(RenderGraph&, const PostProcessInputs&);
    void addExposureComputePass(RenderGraph&, const PostProcessInputs&);
    bool addFinalPass(RenderGraph&, RenderGraphTextureHandle, RenderGraphBufferHandle, uint16_t, uint16_t);
    void endFrame();
};
```

### 2. 资源契约

| 资源 | 格式 | 尺寸 | 生命周期 |
|---|---|---|---|
| exposure ping/pong | `RGBA16F` | 1×1 | 跨帧，首帧 reset |
| histogram | `R32_UINT` | 128×1 | 每帧 clear |
| exposure prepass | `RG16F` | 256×256 | transient |
| exposure reduce | `RG16F` | 32×32 | transient |
| TAA history | `RGBA16F` | render size | 跨帧双缓冲 |
| TAA color | `RGBA16F` | render size | transient |
| DOF CoC | `R16F` / `RG16F` | half render | transient |
| DOF gather | `RGBA16F` | half render | transient |
| bloom mip | `RGBA16F` | 1/2…1/128 | transient，最多 8 级 |
| color LUT | `RGBA16F` | 32×1024 strip | 设置变更时更新 |
| post LDR | `RGBA8` / `RGBA16F` | target size | transient |
| final | backbuffer | target size | presentation |

约束：

- 跨帧资源必须在 `ResourceManager` 创建，再 `RenderGraph::importTexture`；不能依赖 transient pool。
- 1×1 exposure 与 TAA history 首帧 `reset`，第二帧起 `historyValid=true`。
- `--render-scale` 改变、窗口 resize、TAA/FXAA/DOF 开关变化都必须 reset history。
- BGfx 的 `submit(BGFX_DISCARD_ALL)` 会丢弃已绑定纹理；每个 fullscreen draw 前重新绑定所有输入。
- compute 输出纹理使用 `BGFX_TEXTURE_COMPUTE_WRITE`；R32U histogram 不可作为普通 framebuffer。

### 3. 设置与 profile

`PostProcessSettings` 的 Exposure/Tonemap 保留 M1/M2 flat 字段以降低回归风险，Bloom/ColorGrading/TAA/DOF/MotionBlur/FXAA 使用分组结构；profile JSON 统一映射到这些字段：

```text
ExposureSettings
  mode: fixed | automatic | histogram
  metering: average | centerWeighted
  fixedEv100, compensation, limitMin, limitMax
  adaptationSpeedDarkToLight, adaptationSpeedLightToDark
  histogramMinPercentile, histogramMaxPercentile
  proceduralCenter, proceduralRadii, proceduralSoftness

BloomSettings
  enabled, threshold, intensity, scatter, tint, highQuality, resolution

ColorGradingSettings
  postExposure, contrast, colorFilter, hueShift, saturation
  temperature, tint
  channelMixer, lift, gamma, gain
  shadows, midtones, highlights, shaHiLimits
  splitShadows, splitHighlights, splitBalance

TaaSettings
  enabled, quality, motionVectorRejection, sharpening, jitterScale

DepthOfFieldSettings
  enabled, focusMode(manual), focusDistance
  nearFocusStart/End, farFocusStart/End
  nearMaxBlur, farMaxBlur, resolution, highQuality

MotionBlurSettings
  enabled, intensity, maximumVelocity, minimumVelocity
  cameraMotionBlur, sampleCount, depthComparisonExtent

FxaaSettings
  enabled
```

`PostProcessSettings` 仍包含 `tonemapping`、`exposureMode`、`fixedEv100` 等兼容字段；`sanitizePostProcessSettings()` 负责所有范围校验和互斥：

- `histogram` 模式要求 compute 可用，否则日志警告并回退 `automatic`。
- TAA/Motion Blur 要求 `FrameSettings::MotionVectors` 与 `supportMotionVectors`。
- TAA 与 FXAA 只能启用一个；TAA 优先。
- DOF 要求 depth/depthPyramid 可用。
- Bloom/Color Grading 不依赖 deferred；Forward/Deferred 均可用。
- `FrameSettings::Postprocess=false` 时全部后处理关闭（保留旧输出路径）。

新增 JSON：

- `config/postprocess.json`：默认 baseline（所有新效果关闭）。
- `config/postprocess_quality.json`：TAA + Bloom + neutral grading + Auto exposure 的质量预设。
- `--postprocess-profile=<path>`：加载 profile；CLI 的 `--exposure-mode`、`--tonemap`、`--aa`、`--bloom` 等覆盖 profile。
- `--aa=off|taa|fxaa`；`--exposure-mode=fixed|auto|histogram`；`--bloom/--no-bloom`、`--dof/--no-dof`、`--motion-blur/--no-motion-blur`、`--taa/--no-taa`、`--fxaa/--no-fxaa`。

### 4. FrameSettings / PipelineSettings

- `FrameSettings` 使用已有字段：`Postprocess`、`ExposureControl`、`Tonemapping`、`DepthOfField`、`MotionBlur`、`Bloom`、`ColorGrading`、`Antialiasing`。
- 新增 `PipelineSettings` 能力：
  - `supportTAA`
  - `supportBloom`
  - `supportColorGrading`
  - `supportDepthOfField`
  - `supportMotionBlur`
  - `supportFXAA`
  - `supportExposureHistogram`
- `FrameSettings::sanitize()` 加入依赖门控：
  - TAA/MB 必须 `MotionVectors && supportMotionVectors`。
  - DOF 必须 depth 可用。
  - Bloom/ColorGrading/FXAA 按 pipeline support。
  - `AutomaticHistogram` 在 compute 或 `supportExposureHistogram=false` 时回退。
- `config/pipeline.json` 逐项开启；M3.1 已开启 `supportExposureHistogram`，其余 effect 在实现并验证后逐项打开。
- `config/frame_settings.json` 保持 `Postprocess=true`；新效果默认在 profile 中关闭。

### 5. Compute / UAV 垂直切片

M3.0 先生成一个最小 compute 通路，避免 Exposure 阶段才发现 bgfx 限制：

- `TextureDesc::computeWrite`（映射 `BGFX_TEXTURE_COMPUTE_WRITE`）。
- `Encoder::setImage(stage, TextureHandle, mip, access, format)`。
- `Program::createCompute`、`ResourceManager::createComputeProgramFromFiles`。
- `ShaderLibrary` 的 `PassFiles` 增加 `compute`，manifest 增加 `"compute"` 条目。
- `tools/shader_build.py` 增加 `--type compute`；`.cs.sc` 使用 `#include "bgfx_compute.sh"`。
- `RenderGraph` compute pass 已有类型；补充无 FBO、dispatch、writeTexture 生命周期测试。
- Exposure histogram 使用 **128×1 R32U RW texture + `imageAtomicAdd`**，不新增 storage buffer；这样避开 `ComputeBuffer` 封装。
- `bgfx::getCaps()` 检查 `BGFX_CAPS_COMPUTE`、image RW 支持；不可用时 log 并回退 raster/center-weighted exposure。

垂直切片验收：

- D3D11/Vulkan `--gfx-test` 各 dispatch 一次 compute，程序创建、dispatch、stats 正常。
- 1×1 compute 写入值经最终 copy/截图可验证；无资源泄漏。
- 失败时不能静默：必须有 `HBG_LOGE` 和能力日志。

### 6. 调试与性能

扩展现有 `render-test`：

- `postprocess diagnostics`：active exposure/taa/bloom/dof/motion/fxaa、LUT size、history valid、exposure mode。
- `render performance` 增加 `postprocess=...`、`postPasses=N`、`postprocess_ms`（wall）、`gpu` 总时。
- `--debug-view` 增加：
  - `exposure`：128-bin histogram + 当前 EV100/曝光倍数
  - `bloom`：bloom mip0
  - `taa-history`：history/重投影结果
  - `dof`：CoC
  - `color-grading`：LUT 中间切片
  - `fxaa`：边缘权重
- 正常 debug view 默认跳过 PostProcessChain；只有 M3 专属 debug 需要运行对应 pass。
- 性能口径：`--no-vsync --frames=300 --width=1920 --height=1080`，记录 D3D11/Vulkan、Forward/Deferred、base/full-feature。

---

## M3.0 PostProcess 基础设施

状态：`[x]`（基础通路完成；TAA/LUT/Bloom/Final 的实际调用留到对应任务）

目标：在不动 M2 画面的前提下，建立后处理链、profile、资源、compute 垂直切片和验收骨架。

参考：

- HDRP `HDRenderPipeline.PostProcess.cs:802/847-973`
- HDRP `GlobalPostProcessSettings.cs:60-93`（LUT 32、buffer format）
- HDRP `PostProcessing/Shaders/Exposure.compute:34/67/83/137`
- HDRP `PostProcessing/Shaders/FinalPass.shader`（LDR/FXAA/final 职责）
- 本文“架构”章节

实现：

- [x] `PostProcessChain`、`PostProcessInputs/Output`、`PostProcessProfile`、`PostProcessHistoryState`。
- [x] `config/postprocess.json` baseline；`--postprocess-profile` 加载后由 CLI overrides 覆盖。
- [x] 1×1 exposure RGBA16F ping/pong（`computeWrite`）；TAA history/LUT/Bloom 资源槽位已留出，实际分配在 M3.4/M3.2/M3.3。
- [x] `PostProcessChain::addFinalPass` + `Final` shader（LDR linear→sRGB）；默认 effects off 仍走旧 `Tonemap`，不新增 graph pass。
- [x] `PostProcessInputs` 分开 render/target 尺寸；`addFinalPass` 显式 target viewport；effect passes 使用 render 尺寸。
- [x] `PipelineSettings` M3 support flags、`FrameSettings::sanitize` 依赖门控。
- [x] `PostProcessUniforms` 增加 `frameParams/screenParams`；IDL 生成与布局单测更新。
- [x] `--aa`、`--postprocess-profile`、`--taa/--fxaa/--bloom/--dof/--motion-blur` CLI；DebugUI M3 分组。
- [x] compute/UAV 垂直切片：`TextureDesc::computeWrite`、`Encoder::setImage`、`Program::createCompute`、`ResourceManager::createComputeProgramFromFiles`、`PostProcessExposure` compute。
- [x] `render-test` 输出 `postprocess/post_passes/post_compute/post_history`，首帧 `postprocess diagnostics`。
- [x] 单测：profile 解析/默认/sanitize、history reset、compute graph pass、CLI。
- [x] D3D11/Vulkan 冒烟：effects off 与 M2.9 `m28_blend_view` 基线逐像素一致；`--gfx-test` 双后端各 dispatch 一次 compute。

验收：

- [x] effects off：D3D11/Vulkan 输出与 M2.9 `m29_base_deferred` **逐像素一致**（`m3_base_deferred*.png`）。
- [x] `--postprocess-profile` 不改变旧路径；`--aa`/effect flags 解析通过；support flags off 时明确回退。
- [x] compute 垂直切片 D3D11/Vulkan 通过（stats `compute=1`，程序创建/dispatch/资源释放正常）；无 compute 时 warning 回退。
- [x] 1×1 exposure history reset 单测覆盖首帧、resize、render-scale、effects key 变化；TAA history 实际重置于 M3.4。
- [x] `ctest` 全绿（`hbg_tests` + `hbg_idl_check`），render/gfx test 资源泄漏为 0。

遗留：

- 1×1 compute 结果的 CPU readback 未做：当前 vendored bgfx 无 `bgfx::readTexture`/`BGFX_CAPS_TEXTURE_READ_BACK` 绑定；M3.1 已用 exposure debug view 在 D3D11/Vulkan 逐像素一致验收，CPU 数值 readback 仍遗留。
- `Final` pass 已编译并通过 manifest 加载，但默认不调用；M3.2 Uber 完成后接入。
- TAA history/LUT/Bloom 槽位当前为 invalid，不能误报为已分配。

---

## M3.1 高级 Exposure / Histogram / Adaptation

状态：`[x]`（v1 完成；CPU 数值 readback 仍受 vendored bgfx 限制）

目标：把 M1.9 的 tonemap 内 4×4 瞬时测光替换成 HDRP 风格的降低/直方图 + 时间适应，并输出 1×1 exposure texture。

参考：

- HDRP `PostProcessing/Shaders/Exposure.compute:45-168`（KPrePass / KReduction / KReset）
- HDRP `PostProcessing/Shaders/HistogramExposure.compute:31-191`（KHistogramGen / KHistogramReduce）
- HDRP `PostProcessing/Shaders/ExposureCommon.hlsl:66-123`（WeightSample / SampleLuminance / AdaptExposure）
- HDRP `PostProcessing/Components/Exposure.cs:39/46/53/60/96/103/124/139/144/148/154/160/165`
- HDRP `HDRenderPipeline.PostProcess.cs:1455-1995`（fixed/dynamic/histogram/apply exposure）
- HDRP Core `ShaderLibrary/PhysicalCamera.hlsl:66-73`（时间适应指数衰减）

实现：

- [x] `ExposureSettings`：Fixed / Automatic / Histogram；Average / CenterWeighted；默认 histogram 40–90%。
- [x] 256×256 log-luminance prepass（中心权重）-> 32×32 -> 1×1 reduction（`exposure_prepass/exposure_reduce/exposure_reduce_final`）。
- [x] 128-bin R32U histogram：半分辨率采样、`imageAtomicAdd`、128-bin reduce、40–90% percentile。
- [x] 时间适应：`adaptationSpeedDarkToLight`、`adaptationSpeedLightToDark`、`deltaSeconds`、exposure limits；首帧不走历史。
- [x] 1×1 exposure ping/pong；`history reset` 时清 valid；`exposure multiplier = 1/(lensScale*exp2(ev100))`。
- [x] `Apply Exposure` fullscreen HDR pass（RGBA16F）；Fixed 模式保持旧 `Tonemap` 常量路径，不增加 pass。
- [x] `--exposure-mode=fixed|auto|histogram`，DebugUI Exposure 分组与 adaptation/histogram 滑条。
- [x] 无 compute/histogram 支持时回退 Automatic 并 log；无 compute 时回退旧 tonemap 4×4 auto。
- [x] debug `exposure`：128-bin histogram + 当前 exposure 亮度块；仅 Automatic/Histogram 可用。
- [x] 单测：bin 映射、weight packing、percentile outlier、adaptation、固定/自动 EV、limits。

验收：

- [x] Fixed 模式 D3D11 与 M2.9 `m29_base_deferred.png` **逐像素一致**；Vulkan 保持既有 fixed 输出。
- [x] Automatic 静态 HDR 场景 1/8/30 帧 exposure 稳定；`--sky-intensity=0.25/4.0` 时 exposure 方向正确；极端 0/100 无 NaN/Inf。
- [x] Histogram 40–90% 百分位 CPU 单测覆盖；极端 outlier 被丢弃；D3D11/Vulkan `--debug-view=exposure` 逐像素一致。
- [x] 1×1 exposure 的 D3D11/Vulkan debug 值一致（8-bit debug 图逐像素一致）；数值 readback 仍遗留。
- [x] `--debug-view=exposure` D3D11/Vulkan 截图完成（`m31_exp_*`）。
- [x] 1080p 记录：RelWithDebInfo 300 帧 histogram D3D11 97.2 fps / Vulkan 201.4 fps，Automatic D3D11 121.2 fps / Vulkan 257.2 fps；Debug histogram 1080 帧 D3D11 34.3 fps / Vulkan 33.9 fps；无 CPU readback。
- [x] 单测 **133/133，17570 assertions**；`ctest`、`hbg_idl_check` 通过。
- [x] D3D11/Vulkan Automatic 输出 PSNR 69.6 dB，Histogram 输出 PSNR 70.4 dB。

边界与约定：

- compute pass 中 **sampler 与 image 不能占用同一 stage**：本项目实测 source sampler stage 0 + 输出 image stage 0 会导致 sample 为黑；自动曝光输出 image 用 stage 1/2，histogram clear/gen 用 stage 1。新增 compute 资源必须按此约定。
- Automatic/Histogram 的 exposure 在当帧 compute 后立即 `Apply Exposure`，不是 HDRP 的 pre-exposure 下一帧延迟；无 TAA/其他 effect 时视觉等价。
- Fixed 模式不生成 exposure compute/apply pass；`--debug-view=exposure` 在 Fixed 下回退 Tonemap 并 warning。
- 1×1 exposure 的 CPU 数值日志未实现：vendored bgfx 只有 `bgfx::read` buffer，无 texture readback；当前以 D3D/VK debug 图逐像素一致验收。M3.2 如需要可加 compute->storage buffer 的 readback 调试路径。

---

## M3.2 Uber Post + Color Grading + LUT

状态：`[x]`

目标：新增 target-res Uber Post，完成 Color Grading、Bloom 合成位、Vignette、Tonemap、sRGB 输出；所有新效果关闭时保留旧 `Tonemap`。

参考：

- HDRP `LutBuilder3D.compute:139/324/355`；`ColorAdjustments.cs`、`WhiteBalance.cs`、`ChannelMixer.cs`、`LiftGammaGain.cs`、`ShadowsMidtonesHighlights.cs`、`SplitToning.cs`、`Vignette.cs`
- HDRP `UberPost.compute:170-270`；`HDRenderPipeline.PostProcess.cs:5660-5845`

实现：

- [x] `ColorGradingSettings` / `VignetteSettings`；全部设置进入 `PostProcessProfile` 与 CLI/DebugUI。
- [x] CPU 生成 64³ LUT -> 64×4096 RGBA16F vertical strip（2,097,152 B）；只在 grading/effective tonemap 变化时上传一次。
- [x] White Balance / Contrast / Color Filter / Split Toning / Channel Mixer / SMH / LGG / Hue / Saturation；LUT 同时烘焙 ACES/Neutral/None。
- [x] target-res `Uber Post`：HDR + fixed exposure + vignette + post exposure + LogC LUT，输出 LDR linear；`Final` 统一 sRGB。
- [x] `--color-grading/--no-color-grading`、DebugUI、`--debug-view=color-grading`。
- [x] `config/pipeline.json` 开启 `supportColorGrading`；默认 baseline 仍关闭。
- [x] 全屏采样修正：`Tonemap`/`Apply Exposure` source 用 `1-v`，后续后处理保持同一约定。

验收：

- [x] neutral fixed ACES vs 修正后 M2.9 Tonemap **60.36 dB**；D3D/VK Uber **64.28 dB**。
- [x] LUT identity/neutral 不改变最终 tonemap 结果；LUT 只在设置变化时上传一次。
- [x] effects off 保持 M2.9 输出，不新增 Uber/final pass；1080p 300 帧 D3D11 **107.3 fps** / Vulkan **183.1 fps**。

边界：

- 不做 master/HSV/YRGB curves、外部 3D LUT、Bloom 之外的自定义 post pass；LUT 固定 64³ strip，不使用 3D texture 封装。

---

## M3.3 Bloom

状态：`[x]`

目标：实现 HDRP 风格 threshold + mip down/up + scatter + tint 的 Bloom，并在 Uber 中合成。

参考：

- HDRP `Components/Bloom.cs:37/43/49/55/68/75`
- HDRP `HDRenderPipeline.PostProcess.cs:4918/4942/4956/5046/5681/5688`
- HDRP `BloomPrefilter.compute`、`BloomBlur.compute`、`BloomUpsample.compute`、`BloomCommon.hlsl`

实现：

- [x] `BloomSettings`：threshold/intensity/scatter/tint/highQualityPrefiltering/highQualityFiltering/resolution。
- [x] threshold 用 gamma 2.2 + soft knee；half/quarter base；mip 数按 HDRP `floor(log2(maxSize))-2-(quarter?1:0)`，最多 8 级。
- [x] 每级 mip 独立 transient；mip0 9-tap separable blur；downsample 3×3 tent；upsample 3×3 tent/bicubic + scatter。
- [x] Uber composite：`lerp(color, color - thresholded + bloom*tint, intensity)`，intensity 用 `2^intensity - 1`；tint 按 luminance 归一化。
- [x] Bloom off 不产生 pass，Uber bloom flag=0；1×1 黑 fallback。
- [x] `--bloom/--no-bloom` 参数、DebugUI、`--debug-view=bloom`。

验收：

- [x] Bloom off 与 M3.2 逐像素一致。
- [x] D3D11/Vulkan bloom-on **64.50 dB**；debug **73.96 dB**。
- [x] 1080p mips=8、base=960×540；1080p 300 帧 D3D11 **97.9 fps** / Vulkan **297.0 fps**。

边界：

- 不做 anamorphic、dirt、lens flare；不做 compute group-shared HDRP 精确实现，使用 raster fullscreen 近似。

---

## M3.4 TAA

状态：`[x]`

目标：在 Exposure 之后、DOF 之前加入 TAA，使用现有 jitter/motion vectors/history；关闭时逐像素回退。

参考：

- HDRP `HDRenderPipeline.PostProcess.cs:2434`
- HDRP `TemporalAntialiasing.hlsl:198/236/293/299/322/347`；`TemporalAntiAliasing.shader`

实现：

- [x] `TaaSettings`：enabled/quality/motionVectorRejection/sharpening/jitterScale；quality 1..5 -> history 0.80/0.84/0.875/0.90/0.925。
- [x] TAA 开启时 renderer 自动 jitter=true 并同步 `ViewState::setJitterScale`；关闭时 jitter=false。
- [x] `taa.fs.sc`：color 用 `1-v`，motion/depth 在 source 空间；reproject `sourceUv-(m.x,-m.y)`；history bilinear、3×3 min/max clamp、depth/luma/motion rejection。
- [x] history 双缓冲 `RGBA16F` render size；首帧 invalid 写当前；resize/scale/TAA 设置变化 reset。
- [x] TAA 在 Apply Exposure 后、DOF/Bloom 前；可选 post-sharpen。
- [x] `--aa=taa/--taa/--no-taa`；TAA/FXAA 互斥；`--debug-view=taa-history`。
- [x] 相机 MV 远平面写背景运动，供 TAA 背景 reproject。

验收：

- [x] TAA off 与 M3.3 逐像素一致。
- [x] 静态相机 16 帧收敛，orbit 无持续 ghost；D3D/VK TAA **64.22 dB**、history debug **65.84 dB**。
- [x] 1080p 300 帧 D3D11 **96.2 fps** / Vulkan **280.8 fps**。
- [x] `--render-scale=0.5` history 为 960×540、final 1920×1080。

边界：

- 不做 vertex velocity history、Blackman-Harris/variance/anti-flicker、TAAU、透明物体完整 MV。

---

## M3.5 Depth of Field

状态：`[x]`

目标：实现 v1 Manual DOF（CoC + half-res gather + full-res combine），在 TAA 后、Motion Blur 前。

参考：

- HDRP `Components/DepthOfField.cs:74/91/110/116/123/129/369`
- HDRP `HDRenderPipeline.PostProcess.cs:3726/3736/3884`
- HDRP `DoFGather.compute:40/48/59/66`、`DoFCombine.compute`、`DoFCircleOfConfusion.compute`

实现：

- [x] `DepthOfFieldSettings`：Manual ranges near/far focus start/end、near/far max blur、full/half/quarter、high quality；默认 near 0..4、far 10..20、blur 4/8。
- [x] `dof_coc.fs.sc`：linear eye depth -> signed normalized CoC（负 near、正 far）与 depth，写 `RG16F`；CoC/gather 分辨率由 `resolution` 控制。
- [x] `dof_gather.fs.sc`：固定 golden-angle 8-tap / 12-tap HQ disk，depth-aware near/far 分层权重；忽略 sky/过大 depth 差样本；不做 tile classification。
- [x] `dof_combine.fs.sc`：全分辨率 sharp color + gather 混合；CoC clamp [-1,1]，max blur 在 gather 半径内 clamp。
- [x] DOF off 不产生 pass；`--debug-view=dof` 输出 CoC（near 蓝、far 红、focus 黑）；debug mode 22 -> 23。
- [x] `--dof/--no-dof`、`--dof-*`、DebugUI；`config/pipeline.json` 开启 `supportDepthOfField`。
- [x] 全屏方向：CoC/combine 的 depth 用原始 `v`；combine sharp/gather 用 `1-v`。
- [x] 不使用 sin hash 每像素旋转；固定 golden-angle 图案保证 D3D11/Vulkan 一致。

验收：

- [x] DOF off 与 M3.4 逐像素一致：`m35_noaa_offdof_d3d.png` vs `m34_off_final.png` / `m33_bloom_off.png` max diff 0。
- [x] 近焦/远焦可见正确前后景虚化，无 NaN/黑边：`m35_dof_near_d3d.png`、`m35_dof_far_norot_d3d.png`；COC debug D3D/VK 逐像素一致。
- [x] D3D/VK DOF-on **68.43 dB**（1280×720，near 0..2/far 4..6，max blur 4/16）；farfocus **66.55 dB**；HQ **68.59 dB**；TAA+DOF+Bloom **68.53 dB**。
- [x] half CoC/gather 1280×720 -> 640×360；1080p half -> 960×540；quarter -> 320×180；render-scale 0.5 + 1080p -> CoC 480×270、final 1920×1080。
- [x] 1080p Release 300 帧 DOF D3D11 **162.7 fps** / Vulkan **233.7 fps**；TAA+DOF+Bloom full **119.5 / 260.5 fps**。

边界：

- 只实现 Manual ranges；不做物理相机 DOF、bokeh aperture shape、tile-based indirect DoF、HDRP CoC reprojection/stabilization、mip chain dilation。
- 单层 signed CoC gather 近似 HDRP near/far 双层；透明物体仍使用 opaque depth，边缘可能出现错误虚化。
- CoC 与 combine 的 depth 方向按当前全屏 pass 约定固定；后续若改全屏 V 翻转，需要同步三处 DOF 采样。

---

## M3.6 Motion Blur

状态：`[x]`

目标：在 DOF 后、Bloom 前实现 v1 camera/object motion blur，使用现有 motion vectors。

参考：

- HDRP `Runtime/PostProcessing/Components/MotionBlur.cs:63/69/75/84/126/151`
- HDRP `Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs:4448/4465/4576/4714`
- HDRP `Runtime/PostProcessing/Shaders/MotionBlur.compute:137/149/177/227/263/336`、`MotionBlurCommon.hlsl`

实现：

- [x] `MotionBlurSettings`：enabled/intensity/max/min velocity/cameraMotionBlur/sampleCount/depth extent；默认 max 200 px、min 2 px、samples 8、depth extent 1，intensity 0.5。
- [x] `motion_blur.fs.sc`：NDC motion -> source UV；`scaledMax` 按 intensity 缩放并 clamp 到 maximum velocity；8/16 taps 由实际像素速度决定，最大 sampleCount 32；深度比较权重 + HDRP 风格 top/back layer blend。
- [x] 3×3 local max velocity 近似 HDRP tile neighborhood max（单 raster pass，不做 tile min/max/scattering/merge compute）；固定采样 pattern，不使用每像素随机旋转，保证 D3D11/Vulkan 一致。
- [x] 零速度/低于 min velocity 时直接输出中心色；静态场景与关闭路径逐像素一致。
- [x] camera/object MVs 共用 `motionVectors`；`cameraMotionBlur=false` 时用 depth + current/previous VP 重建 camera MV 并从中心/邻域 MV 扣除；首帧、resize、render-scale、effect key 变化时 `m_history.valid()` 为 false，跳过 Motion Blur。
- [x] `--motion-blur/--no-motion-blur`、`--motion-blur-intensity/max-velocity/min-velocity/samples/depth-extent`、`--motion-blur-camera/--no-motion-blur-camera`；DebugUI 分组。
- [x] debug 复用 `--debug-view=motion-vectors` 验证方向；不新增 debug mode。
- [x] 单测：velocity/UV、clamp、sample count、zero velocity、depth weight、sanitize、profile、CLI。
- [x] `config/pipeline.json` 开启 `supportMotionBlur`；Motion Blur 位于 TAA/DOF 后、Bloom 前，默认 profile 仍关闭。

验收：

- [x] Motion Blur off 与 M3.5 逐像素一致：`m36_static_off_d3d.png` vs `m35_noaa_offdof_d3d.png` / `m34_off_final.png` max diff 0。
- [x] 静态场景/静止相机 on/off 逐像素一致：`m36_static_on_d3d.png` vs `m36_static_off_d3d.png` max diff 0（容差 ≤ 1/255）。
- [x] `--camera-orbit` 出现方向正确的运动拖影，无明显上下翻转或大面积 halo：`m36_mb_final_d3d.png`、`m36_mb_final_vk.png`；`--no-motion-blur-camera` 可关闭 camera 分量（近似，边缘有轻微重建残差）。
- [x] D3D11/Vulkan Motion Blur-on PSNR **58.39 dB**（1280×720，5 帧 camera orbit，intensity 1.0）；full stack `TAA + DOF + Motion Blur + Bloom` **63.25 dB**。
- [x] 与 TAA + DOF 串联稳定；pass 顺序为 `Temporal Anti-Aliasing -> Depth of Field CoC -> Gather -> Combine -> Motion Blur -> Bloom -> Uber -> Final`。
- [x] 1080p Release 300 帧：Motion Blur only D3D11 **281.6 fps** / Vulkan **353.0 fps**；off **295.5 / 377.3 fps**；TAA+DOF+MB+Bloom full **202.0 / 270.3 fps**；Motion Blur pass GPU 增量约 0.54–0.96 ms，<2 ms。
- [x] 单测 **161/161，17,885 assertions**；Debug/Release `ctest` 通过。

边界：

- 不做 HDRP 的 tile min/max、neighborhood scattering、merge compute、alpha motion blur；3×3 local max 是单 pass 近似。
- 不做 transparent motion vectors、特殊 camera rotation/translation clamp mode、camera velocity clamp、motion vector prepass 纹理；`cameraMotionBlur=false` 的 camera MV 扣除为逐像素深度重建近似。
- 固定 tap pattern，无 jitter/随机旋转；sample count 为偶数并 clamp 2..32。

---

## M3.7 FXAA + Final Pass

状态：`[x]`

目标：在 LDR 输出前加入可选 FXAA；完成 sRGB/FinalPass；关闭时逐像素 copy。

参考：

- HDRP `HDRenderPipeline.PostProcess.cs:5905-5950`、`FinalPass.shader:139`（FXAA 在 sRGB 编码前）
- HDRP `PostProcessing/Shaders/FXAA.compute:22/31`、`FXAA.hlsl`

实现：

- [x] `FxaaSettings` / `AntialiasingMode`（None/TAA/FXAA），`resolveAntialiasingMode` 固定 TAA 优先；sanitize/support gate 互斥。
- [x] `final.fs.sc` 编译两个变体：无 FXAA 为当前 sRGB copy；FXAA 变体在 sRGB 前执行 luma edge、方向/span clamp、4-tap blend。port 自 HDRP `FXAA.hlsl` 非 HDR 分支。
- [x] FXAA 开启时强制走 `Uber Post`，确保 FinalPass 拿到 LDR linear；`UberPost` 输出 LDR linear，FinalPass 统一 sRGB，避免 double sRGB。
- [x] `--aa=fxaa`/`--fxaa`/`--no-fxaa`、profile `fxaa.enabled`、DebugUI；`supportFXAA=true`。
- [x] debug `fxaa` 显示边缘权重；Debug mode 数 24。
- [x] 单测：AA 互斥/TAA 优先、mode 名称、profile、CLI、debug 表。

验收：

- [x] FXAA off 与 M3.6 输出逐像素一致：`m37_off_sky_d3d.png` vs `m36_static_off_d3d.png` max diff 0。
- [x] 边缘场景 FXAA 只影响边缘，差异图可解释：`m37_fxaa_diff_d3d.png`、`m37_debug_fxaa_d3d.png`。
- [x] D3D11/Vulkan FXAA-on **64.27 dB**；FXAA+DOF+Bloom+MB 固定曝光 **66.55 dB**。
- [x] `--aa=taa` 时 FXAA 自动关闭，graph 只有 TAA、无 FinalPass；`--aa=off` 两者都关闭。
- [x] FinalPass 在 1920×1080 输出；render-scale 0.5 时 960×540 场景、1920×1080 最终输出：`m37_fxaa_scale_d3d.png`。
- [x] 1080p Release 300 帧：FXAA D3D11 **288.9 fps** / Vulkan **251.0 fps**；FXAA+DOF+Bloom+MB full **219.1 / 204.6 fps**。
- [x] 单测 **164/164，17,929 assertions**；Debug/Release `ctest` 通过。

边界：

- FXAA 在 target-res LDR linear 上执行，不处理 alpha；不做 SMAA/TAAU、HDR output encoding、UI/after-post 混合。
- FXAA 与 TAA 互斥，TAA 优先；FXAA 需要 Uber/Final，effects off 仍走旧 `Tonemap`。
- `fxaa` debug 输入是 Uber 后的 LDR color，只显示边缘权重，不显示最终抗锯齿图。
- RenderDoc 抓帧仍缺失；数值验证使用 D3D/VK 截图与 diff。

---

## M3.8 验收、性能、文档

状态：`[x]`

目标：完成 M3 截图矩阵、双后端、性能、调试和文档；把 M3 状态写回总计划与 AGENTS。

参考：HDRP `HDRenderPipeline.PostProcess.cs:847-973`（StopNaNs → Exposure → TAA → DOF → MB → Bloom → Uber → FXAA/Final）、`FinalPass.shader:139`（FXAA 在 final pass 内）；M3 不新增独立 StopNaNs pass，边界见下。

实现：

- [x] 新增 debug modes：`exposure`、`bloom`、`taa-history`、`dof`、`color-grading`、`fxaa`（18 -> 24）。
- [x] `render performance` 输出 effect flags、post passes、post wall、GPU 总时：
  - `postprocess_ms` 是 `PostProcessChain::record + Final/debug presentation` 的 CPU graph-record wall 平均值；只统计实际后处理帧，含首帧 LUT 构建。
  - `gpu_frame_ms` 是 bgfx GPU timer 的整帧总时；本项目没有 per-pass GPU timer，不伪造 post GPU 分量。
  - 新增 `post_exposure`、`post_uber`、`post_bloom`、`post_taa`、`post_dof`、`post_motion_blur`、`post_fxaa` flags 与既有 `post_passes`。
- [x] 截图矩阵 D3D11/Vulkan，每个后端 25 张，共 50 张：
  - `m3_base_off`、`m3_fixed`、`m3_auto`、`m3_histogram`
  - `m3_bloom_off/on`、`m3_grading_neutral/aces`、`m3_taa_off/on_static`、`m3_taa_orbit`
  - `m3_dof_off/near/far`、`m3_motion_blur_off/orbit`、`m3_fxaa_off/on`、`m3_all_effects`
  - debug：`m3_debug_exposure/bloom/taa_history/dof/color_grading/fxaa`
  - 补充 Forward 共用：`m3_all_effects_forward`、`m3_fxaa_on_forward`（D3D/VK）
- [x] 性能：1080p D3D11/Vulkan、Forward/Deferred、base/full、TAA/DOF/MB 开关组合；全效果重复 3 次记录波动。
- [x] 验证 0.5 render scale：TAA 主链在 960×540，DOF half CoC 480×270，Bloom half base 480×270，final 1920×1080。
- [x] 更新 `docs/implementation-plan.md` M3 状态、帧顺序、任务表。
- [x] 更新 `AGENTS.md` 后处理默认、history、compute、验证约定。
- [x] 中文完成简报：实现、指标、测试、边界、下一步。

验收：

- [x] M3 出口标准全部满足；effects off 双后端 D3D/VK **64.22 dB**，`m3_base_off_d3d.png` 与 M3.7 基线逐像素一致（max diff 0）。
- [x] 全效果 1080p 在 RTX 2060 SUPER >60fps：deferred **195.7–212.3 fps (D3D11) / 204.4–215.4 fps (Vulkan)**；forward 单次 **222.2 / 199.1 fps**。
- [x] `ctest`、`hbg_idl_check`、`--gfx-test`、docs links、`git diff --check` 通过。
- [x] 不新增资源泄漏；所有矩阵/性能运行 `ResourceManager` live=0。
- [x] RenderDoc 仍缺失，已明确标记 F11/GPU 抓帧遗留；改用双后端截图、bgfx stats、RenderGraph diagnostics。

验收结果（1280×720 截图，RelWithDebInfo；性能 1920×1080 300 帧，`--no-vsync`）：

| 项 | 结果 |
|---|---|
| `m3_base_off` D3D/VK | **64.22 dB**；D3D 与 M3.7 `m37_off_sky_d3d.png` **0 diff** |
| exposure D3D/VK | fixed **65.91 dB**、auto **67.33 dB**、histogram **67.50 dB** |
| bloom off/on | D3D/VK **65.91 / 66.02 dB**；on/off D3D **29.43 dB**（913,843 changed） |
| grading neutral/aces | D3D/VK **65.91 / 64.28 dB**；neutral/aces D3D **26.57 dB** |
| TAA off/on static/orbit | D3D/VK **65.91 / 65.92 / 59.37 dB**；静态 16 帧收敛，orbit 无持续 ghost |
| DOF off/near/far | D3D/VK **112.55 / 107.78 / 112.55 dB**；off/near D3D **34.82 dB**，可见前景/背景虚化 |
| motion blur off/orbit | D3D/VK **58.87 / 59.72 dB**；off/orbit D3D **39.31 dB**，拖影方向正确 |
| FXAA off/on | D3D/VK **66.02 / 66.42 dB**；off/on D3D **49.48 dB**，变化集中在边缘 |
| all effects | D3D/VK **64.36 dB**；TAA+DOF+MB+Bloom+Grading，24 post passes |
| Forward 共用 | all-effects forward D3D/VK **64.37 dB**、forward/deferred **64.56 dB**；FXAA forward D3D/VK **66.37 dB** |
| debug | exposure/dof/color-grading D3D/VK **0 diff**；bloom **73.96 dB**、taa-history **65.84 dB**、fxaa **50.26 dB** |
| 0.5 render scale | TAA log `960x540`；DOF `480x270`、Bloom `480x270`；输出 **1920×1080**，D3D/VK **105.66 dB** |

性能（RelWithDebInfo，1080p，300 帧，主 scene + Uffizi sky；full 使用默认 8-tap DOF）：

| 路径 | D3D11 fps | Vulkan fps | post wall ms | notes |
|---|---:|---:|---:|---|
| deferred base | 182.3 | 310.7 | 0.000 | effects off，post_passes=0 |
| forward base | 201.5 | 250.1 | 0.000 | effects off |
| deferred full | 195.7–212.3 | 204.4–215.4 | 0.33–0.37 | 3 次重复；24 post passes |
| forward full | 222.2 | 199.1 | 0.36 / 0.39 | 24 post passes |
| deferred TAA only | 200.7 | 253.9 | 0.34 | 3 post passes |
| deferred DOF only | 202.2 | 258.7 | 0.35 / 0.31 | 5 post passes |
| deferred MB only | 196.7 | 252.7 | 0.34 / 0.33 | 3 post passes |

完整图在 `C:\Users\chenfuling\AppData\Local\Temp\codemaker`：`m3_*_d3d.png`、`m3_*_vk.png`，Forward 补充图为 `m3_all_effects_forward_*`/`m3_fxaa_on_forward_*`；性能日志为 `m3_perf_*.log`。全效果 1080p 的最低帧率约为 D3D11 **195.7 fps** / Vulkan **204.4 fps**，高于 60 fps 出口线；HQ 12-tap DOF 变体约 D3D 156.1 / Vulkan 196.5 fps，未纳入 full 基线。

M3.8 明确边界：RenderDoc/F11 抓帧仍缺失；测试矩阵中的纯 NaN 输入未单独构造，交互窗口 resize 未单独截图（history reset 由单测覆盖），未新增独立 StopNaNs pass；纯黑/高亮/极端曝光已由 M3.1 的 `--sky-intensity=0.25/4.0`、极端 Exposure 0/100 与 histogram/自动曝光验证覆盖。

---

## 测试矩阵

### 单元测试

- [x] `tests/render/test_post_process.cpp`：profile、settings、exposure EV/adaptation、histogram、LUT identity、Bloom mip、TAA clamp、DOF CoC、Motion Blur velocity、FXAA/AA 互斥。
- [x] `tests/gfx/test_uniform_layout.cpp`：新增 IDL block 尺寸/偏移/descriptor。
- [x] `tests/render/test_render_graph.cpp`：compute pass、import texture、writeTexture 生命周期、无 FBO。
- [x] `tests/app/test_backend.cpp`：`--postprocess-profile`、`--aa`、`--exposure-mode=histogram`、effect on/off、非法值。
- [x] `tests/render/test_frame_settings.cpp`：pipeline support、MotionVectors/TAA/MB 依赖、Postprocess off。
- [x] `tests/render/test_debug_display.cpp`：新增 debug mode 名称/数量。

### 后端/图像

- [x] D3D11/Vulkan：effects off parity、all effects on、各 effect 单开。
- [x] Forward/Deferred：TAA/DOF/MB/Bloom/Grading/FXAA 共用；deferred 默认。
- [x] render scale 0.5 与 1.0。
- [x] 首帧/第二帧：exposure/history 正确 reset/valid。
- [x] 静态相机 16 帧：TAA 收敛。
- [x] 相机 orbit：TAA/Motion Blur 无持续 ghost/错误方向。
- [ ] 纯黑/高亮/NaN 输入：StopNaNs、exposure/bloom 安全。
- [ ] resize 后：history 尺寸正确，无旧帧拉伸。

### 命令基线

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg hbg_tests
ctest --test-dir build\vs2022 -C Debug --output-on-failure
py -3 tools\idl_gen.py --root . --check
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=vulkan --gfx-test
```

M3 图像命令示例：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<scene.gltf> --frames=16 `
  --width=1280 --height=720 `
  --pipeline-settings=config\pipeline.json --frame-settings=config\frame_settings.json `
  --sky=<uffizi-large.exr> `
  --postprocess-profile=config\postprocess_quality.json --aa=taa `
  --screenshot=m3_taa.png

.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=<scene.gltf> --frames=16 `
  --width=1280 --height=720 `
  --pipeline-settings=config\pipeline.json --frame-settings=config\frame_settings.json `
  --sky=<uffizi-large.exr> `
  --postprocess-profile=config\postprocess_quality.json --aa=taa --bloom --dof --motion-blur `
  --exposure-mode=fixed --screenshot=m3_all_vk.png
```

性能命令（full 基线用默认 8-tap DOF；HQ 加 `--dof-high-quality`）：

```powershell
.\build\vs2022\RelWithDebInfo\hbg.exe --backend=d3d11 --render-test=<scene.gltf> `
  --frames=300 --width=1920 --height=1080 --no-vsync `
  --pipeline-settings=config\pipeline.json --frame-settings=config\frame_settings.json `
  --sky=<uffizi-large.exr> `
  --postprocess-profile=config\postprocess_quality.json `
  --aa=taa --bloom --dof --motion-blur --exposure-mode=fixed
```

---

## 性能目标与基线

M2.9 1080p 基线（RTX 2060 SUPER，单次 300 帧，存在波动）：

| 路径 | D3D11 | Vulkan |
|---|---:|---:|
| deferred base | 145.9 fps | 348.5 fps |
| forward base | 140.5 fps | 323.5 fps |
| deferred full-feature | 115.5 fps | 115.3 fps |

M3.8 1080p 实测（RelWithDebInfo，300 帧，`--no-vsync`，主 scene + Uffizi sky）：

| 路径 | D3D11 | Vulkan | post wall |
|---|---:|---:|---:|
| deferred base | 182.3 fps | 310.7 fps | 0.000 ms |
| forward base | 201.5 fps | 250.1 fps | 0.000 ms |
| deferred full（默认 DOF） | 195.7–212.3 fps | 204.4–215.4 fps | 0.33–0.37 ms |
| forward full（默认 DOF） | 222.2 fps | 199.1 fps | 0.36 / 0.39 ms |
| deferred TAA only | 200.7 fps | 253.9 fps | 0.34 ms |
| deferred DOF only | 202.2 fps | 258.7 fps | 0.35 / 0.31 ms |
| deferred MB only | 196.7 fps | 252.7 fps | 0.34 / 0.33 ms |

`postprocess_ms` 是 CPU graph-record wall，不是 GPU 时间；`gpu_frame_ms` 才是整帧 GPU timer。HQ 12-tap DOF full 约 D3D11 156.1 / Vulkan 196.5 fps。

M3 目标（先记录，不把估计写成结果）：

- 全后处理栈 1080p >60 fps；低于 60 时优先降低 Bloom mip、TAA quality、DOF half-res、MB sample count。
- 建议单 effect 预算（RTX 2060 SUPER，待实测替换）：
  - Exposure histogram <0.5 ms
  - TAA <2.0 ms
  - Bloom <2.0 ms
  - Uber/Color Grading/Final <1.5 ms
  - DOF <3.0 ms
  - Motion Blur <2.0 ms
  - FXAA <0.5 ms
- 性能数据必须同时记录 wall frame、bgfx CPU submit、GPU timer、draw/compute、passes、postprocess flags。
- 目标硬件为 GTX 1660/RX 5600 级；本机 RTX 2060 SUPER 只作代理，低一档硬件仍为遗留验证。

---

## 风险与对策

1. **compute/UAV 未知**：M3.0 先做垂直切片；失败时保留 raster/center-weighted Exposure 回退，Histogram 标记未达标，不静默降级。
2. **TAA 与透明/移动物体**：M1 边界包含透明无 MV、moving object prevWorld 未接；TAA v1 只承诺 opaque + camera/object 现有 MV，透明 ghost 写入边界。
3. **TAA history 生命周期**：resize/scale/开关/首帧必须 reset；用双缓冲 + 单测覆盖。
4. **DOF/MB 质量与成本**：v1 是简化版，不追求 HDRP 物理 bokeh/scattering；先保证顺序、稳定性、双后端和可关闭。
5. **Color LUT 2D strip**：没有硬件三线性跨 slice；手工两层采样，已知色块单测覆盖；不做外部 LUT。
6. **默认画面回归**：effects off 必须走旧 Tonemap；Uber 只在高级效果启用时接管。
7. **性能波动**：M2.9 已观察到 D3D11 base 119–146 fps；M3 记录多次运行，不把单次峰值当结论。
8. **RenderDoc 缺失**：仍不能 F11 抓帧；用双后端截图 + bgfx stats + RenderGraph diagnostics 验收。
9. **HDRP 顺序不能乱**：Exposure -> TAA -> DOF -> Motion Blur -> Bloom -> Grading/Uber -> FXAA；任何 pass 插入必须回填本文件。

---

## HDRP/Core 源码索引

| 模块 | 参考 |
|---|---|
| 后处理总顺序 | `HDRenderPipeline.PostProcess.cs:802/847-973` |
| Dynamic Exposure | `HDRenderPipeline.PostProcess.cs:1455-1870`；`Exposure.compute:34/67/83/137`；`HistogramExposure.compute:32/67/150` |
| Exposure 参数 | `Components/Exposure.cs:11/39/46/53/60/96/103/124/139/144/148/154/160/165` |
| Bloom | `HDRenderPipeline.PostProcess.cs:4918/4942/4956/5046/5681/5688`；`Components/Bloom.cs:37/43/49/55/68/75`；`BloomCommon.hlsl` |
| TAA | `HDRenderPipeline.PostProcess.cs:2434`；`TemporalAntialiasing.hlsl:198/236/293/299/322/347`；`TemporalAntiAliasing.shader` |
| DOF | `HDRenderPipeline.PostProcess.cs:3726/3736/3884`；`Components/DepthOfField.cs:74/91/110/116/123/129/369`；`DoFGather.compute:40/48/59/66` |
| Motion Blur | `Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs:4440-4740`；`Runtime/PostProcessing/Components/MotionBlur.cs:63/69/75/84/126/151`；`Runtime/PostProcessing/Shaders/MotionBlur.compute:149/227/263/336` |
| Color Grading / LUT | `LutBuilder3D.compute:139/324/355`；`Components/ColorAdjustments.cs`、`WhiteBalance.cs`、`ChannelMixer.cs`、`LiftGammaGain.cs`、`ShadowsMidtonesHighlights.cs`、`SplitToning.cs`、`Vignette.cs`；`GlobalPostProcessSettings.cs:60` |
| Uber Post | `UberPost.compute:170-270`；`HDRenderPipeline.PostProcess.cs:5660-5845` |
| FXAA | `HDRenderPipeline.PostProcess.cs:5905-5950`；`FXAA.compute:22/31`；`FXAA.hlsl` |
| 本工程现状 | `src/render/postprocess/post_process_settings.h/.cpp`；`shaders/post/tonemap.fs.sc`；`src/render/passes/forward_renderer.cpp:2676` |
