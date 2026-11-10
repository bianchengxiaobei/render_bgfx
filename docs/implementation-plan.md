# hdrp-bgfx 总体实现方案（收敛版）

> 工程根：`E:\Learn\hdrp-bgfx`。
> 范围：Windows + D3D11/Vulkan、Forward + Deferred、SSR/SSAO/SSGI/体积雾、glTF、暂无编辑器。
> 实现状态以 `docs/m0-checklist.md` 为准。

---

## 当前进度（2026-11-10）

- `M0.1 工程骨架与窗口`：已完成。D3D11/Vulkan 双后端开窗、清屏、打印 caps、响应 resize。
- `M0.2 gfx 封装`：核心完成。句柄池、Texture/Buffer/Uniform/Program/FrameBuffer/Encoder/ResourceManager；两帧延迟销毁、FBO refCount 复用、shutdown 泄漏告警。
- `M0.3 IDL / uniform codegen`：完成。`tools/idl_gen.py` 从 `config/uniforms/*.idl` 生成 C++ binder + shader 声明；`UniformBlock` 上传与 shader 反射校验；CMake 增量生成 + `hbg_idl_check`。
- `M0.4 glTF 资产层`：核心完成。cgltf 解析网格/材质/贴图/节点/相机/`KHR_lights_punctual`，MikkTSpace 生成 tangent，`SceneLoader` 输出 `RenderScene`，支持 `.hdrp.json` sidecar。
- `M0.5 前向 PBR + Shader 库`：核心完成。`hbg_shader` 读取 manifest 的 dxbc/spirv 变体，`MaterialSystem` 上传 VB/IB/材质/贴图，`ForwardRenderer` 画到 HDR target；M1.9 起最终输出支持固定/自动曝光、ACES/Neutral/None 与 sRGB 编码；新增 `--scene/--render-test/--frames/--screenshot`。
- `M0.6 光照与阴影 v0`：核心完成。`GpuLightData`（RGBA32F 4×32）+ `LightBuilder` 上传 1 方向光 + 最多 31 punctual；`ShadowManager` 方向光阴影 + PCF 3×3；新增 `shadowcaster`、`light_loop`、`shadows`、`debug_light_data` shader。
- `M0.7 调试与验收设施`：核心完成。`hbg_imgui` + 自写 bgfx backend、DebugUI 面板、Profiler、wireframe line pass、F1/F11/F12、`--debug-wireframe/--no-shadows`。
- `M1.1 RenderGraph`：核心完成。logical texture/buffer handle、raster/compute pass builder、attachment/read/write 声明、死 pass flood-fill、transient 池/FBO 池、ViewId 分配、校验与 diagnostics；ForwardRenderer 已拆成 Shadow → Scene Clear → Prepass/MV/DepthPyramid → Forward → Wireframe → Tonemap/Debug pass。
- `M1.2 FrameSettings`：核心完成。128 位 `BitSet128` + HDRP 同 bit 索引、Camera/Reflection 默认值、OverrideMask、PipelineSettings 能力门控、JSON 解析/序列化、`--pipeline-settings/--frame-settings`。
- `M1.3 ViewState`：核心完成。jittered/non-jittered view/proj/VP、逆矩阵、上一帧矩阵、相机位置、frustum planes；Halton(2/3) jitter；`HistoryBuffer<T>`；`ViewResolution` 分离 render/target 与 RT scale；全局 CB 扩展。
- `M1.4 DrawList`：核心完成。HDRP queue/pass tag/frustum cull/sort key；opaque/transparent/shadow/wireframe 独立列表；opaque/shadow instancing；submesh index range。
- `M1.5 Prepass`：核心完成。DepthOnly/DepthForwardOnly opaque 深度+法线（888 oct + perceptual roughness）、Objects/Camera Motion Vectors（RG16F）、RG32F min/max depth pyramid（scratch）；RenderGraph/FBO 支持 per-attachment mip；相机运动矢量改为采样 depth pyramid，避免 D3D 深度纹理同时作为 DSV/SRV；新增 `--debug-view` 与 `--camera-orbit`。
- `M1.6 光照 v1`：核心完成。`LightListBuilder` 按 16×16 FPTL tile 与 64×64 big-tile 两阶段 CPU 构建；`R32_UINT` 每 tile count + 最多 32 个绝对 GpuLight 索引；LightLoop 方向光单独循环、punctual 走 tile list，list 不可用时回退；ForwardRenderer 上传/绑定/显式 graph read。
- `M1.7 阴影 v1`：核心完成。方向光 4 级联（2048 atlas，4×1024）、point 6×256/spot 512 punctual atlas、shelf pack、`ShadowContext` RGBA32F 8×32、每 request 一个 shadow pass、stage 6/8/9 绑定、PCF 3×3 默认 + 5×5 分支；`LightBuilder` 回填方向光/punctual shadowIndex；glTF 点光/聚光默认参与阴影。
- `M1.8 Sky/IBL`：核心完成。HDR/EXR equirect + 6:1 cube strip、独立 2048×1024 sky 与 1024×512×7mip IBL、CPU GGX prefilter、SH9 ambient（7×vec4 packing）、UE4 analytic EnvBRDF、prepass 后 fullscreen sky pass、stage 10 环境采样、`FrameSettings SkyReflection` 门控、`--sky/--sky-intensity/--sky-rotation`。
- `M1.9 最终输出`：核心完成。固定 EV100 / 自动 4×4 center-weighted 测光、HDRP `PhysicalCamera.hlsl` 曝光换算、HDRP `Color.hlsl` ACES approximate / Neutral / None、sRGB 输出、`--debug-exposure` 覆盖、DebugUI 最终输出面板、`FrameSettings` Exposure/Tonemapping 门控。
- `M1.10 Debug`：核心完成。11 个全屏模式；硬件深度/线性化 depth pyramid、normal、HDRP hue-wheel motion、CPU tile LightCluster、方向/punctual shadow atlas、opaque material debug 的 Albedo/Roughness；`DebugUniforms` 16B、debug-only atlas clear、camera motion 采样 depth pyramid 修复 D3D DSV/SRV 冲突。
- 当前验证：`ctest` 86 个用例、15842 assertions + `hbg_idl_check` 通过；双后端 `--gfx-test`；DamagedHelmet 标准/prepass、重复 helmet instancing、shadow scene、point/spot LightList 与阴影、透明场景、`--render-scale=0.5`、ImGui/debug light-data 截图；shadow scene graph `37/37`，point `33/33`，spot `28/28`；M1.8 双后端 sky graph `28/28`，Uffizi/DefaultHDRI 天空、金属反射、SkyReflection 开关；M1.9 固定/自动曝光、ACES/Neutral、D3D11/Vulkan 一致性；M1.10 depth/normal/motion/cluster/双 atlas/albedo/roughness 双后端截图，shadow atlas debug graph `38/38`、material `27/27`；Sponza 103 draw items / 27,013,502 triangles（本地贴图缺失）。
- 遗留验收：alphaMask 外部场景、Khronos 参考视觉对齐、sampler wrap/压缩纹理、shadow 视觉细化；RenderDoc 抓帧需安装 RenderDoc 后补。M1.5 边界：透明 prepass/MV、TAA/motion blur、moving object prevWorld authoring 未接；深度金字塔为 RG32F min/max + scratch。M1.6 边界：LightList 为 CPU 屏幕空间 AABB v1，不是 HDRP GPU compute/cluster；无 depth fine pruning/per-voxel clusters/light layers/categories/probes/decals。M1.7 边界：固定 2048 atlas/1024 级联/256 point face/512 spot，无 shadow cache、per-request frustum cull、alphaMask shadow、shadow mask/screen-space/contact shadow；PCF 默认 3×3，未接 PCSS/EVSM/VSM 和 volume 配置。M1.8 边界：IBL 为 1024×512 equirect 7 mips、sky 2048×1024 raw，CPU GGX 32 samples/mip；无 cubemap BSDF LUT、反射探针/SSR/SSGI、动态天空/cloud/backplate/distortion；SH9 静态，SkyReflection 只关 specular。M1.9 边界：自动曝光为最终 pass 内 4×4 center-weighted 瞬时测光，不是 HDRP GPU histogram/adaptation；无 Bloom/ColorGrading/自定义 LUT，ACES 为 HDRP approximate、无 HDR 输出。M1.10 边界：LightCluster 为 CPU 16×16 tile 占用图，不是 HDRP ray cluster；shadow atlas 为 debug-only clear + raw depth 对比度视图；Material debug 仅 opaque/depth prepass；Motion 无箭头网格/Intensity 子模式；DepthPyramid 固定 mip0。
- 下一任务：`M2 Deferred`（M1 1080p 60fps 性能验收待补）。
- 验证命令：`cmake --preset vs2022` → `cmake --build --preset vs2022-debug` → `ctest --test-dir build/vs2022 -C Debug --output-on-failure`；冒烟 `hbg --render-test=<scene.gltf> --frames=3 --screenshot=out.png`；设置 `--pipeline-settings=config/pipeline.json --frame-settings=config/frame_settings.json`；RT scale `--render-scale=0.5`；UI `hbg --scene=<scene.gltf> --frames=3 --screenshot=ui.png`；调试 `--debug-light-data`/`--debug-wireframe --no-shadows`；prepass/debug `--debug-view=depth|depth-pyramid|normal|motion-vectors|light-cluster|directional-shadow-atlas|punctual-shadow-atlas|albedo|roughness`；Sky/IBL `hbg --render-test=<scene.gltf> --sky=<hdri.hdr|exr> --sky-intensity=1 --sky-rotation=0 --frames=3 --screenshot=sky.png`；最终输出 `--exposure-mode=fixed|auto --exposure-compensation=<ev> --tonemap=aces|neutral|none --debug-exposure=<ev>`。

---

## 0. 范围锁定与前置结论

- 后端：D3D11 + Vulkan（先跑通这两个，D3D12 后置）。
- 资产：glTF 用 cgltf，贴图用 bimg，运行时调试面板用 ImGui（不算编辑器）。
- HDRP 的 HLSL 不能直接给 bgfx 用：bgfx 的 shaderc 是 GLSL 风格方言，最终生成各后端二进制。必须把 HDRP 的 SurfaceData/BSDF/LightLoop/Shadow 数学部分重写成 shaderc 方言。
- 图形阶段不能读 StructuredBuffer，因此 HDRP 的 LightList / ShadowContext / LightData / ProbeData 全部改为 2D/3D Data Texture + `texelFetch`；Compute 内部仍可用 Storage Buffer。
- bgfx 单队列，HDRP 的 AsyncCompute 相关 FrameSettings 直接置空，不做假异步。
- 坐标系：右手系 Y-up，逆时针为正面（与 glTF 一致）；投影矩阵按 `bgfx::getCaps()->homogeneousDepth` 生成对应深度范围。
- 命名空间 `hbg`；C++20；类型 `PascalCase`，函数 `camelCase`，成员 `m_`，常量 `k_`。
- 资源全部句柄化：`TextureHandle` / `BufferHandle` / `ProgramHandle` / `FrameBufferHandle` 是 `{index, generation}`，禁止在 `gfx` 层之外出现裸 `bgfx::*Handle`。
- 所有 GPU 常量结构由 IDL 生成；渲染代码里不允许手写 uniform 名字。
- 失败处理：`HBG_ASSERT`（debug）+ `HBG_FATAL`（log + abort）；shader 编译错误在构建期失败，不允许运行时静默。
- **源码优先（铁律）**：动手实现任何模块之前，必须先阅读对应 HDRP 源码与 bgfx/bx 源码；计划文档只作索引，冲突时以源码为准。参考位置：
  - HDRP：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
  - HDRP Core：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
  - bgfx/bx/bimg：本工程构建目录 `build\vs2022\_deps\bgfx-src`
  - 每个任务在 `docs/m0-checklist.md` 记录实际参考的 `file:line`，实现后回填。

### 0.1 实际依赖

| 依赖 | 版本/方式 |
|---|---|
| bgfx.cmake | tag `v1.167.9621-609`（FetchContent） |
| bgfx | `1.167.9149`，commit `f446c319d65b5e9a4d4070100e3fc571f5b35ea5` |
| GLFW | `3.4` |
| doctest | `v2.4.11` |
| cgltf | `v1.15` |
| MikkTSpace | commit `3e895b49d05ea07e4c2133156cfa94369e19e409` |
| nlohmann/json | `v3.11.3` |
| ImGui | `1.92.8 WIP`，使用 bgfx 源码树 `3rdparty/dear-imgui` |
| enkiTS | 顺延 M1 |

---

## 1. 目录与类清单

```text
hdrp-bgfx/
├─ CMakeLists.txt  CMakePresets.json
├─ cmake/                 # Dependencies.cmake
├─ config/
│  ├─ uniforms/           # global.idl pass.idl material.idl object.idl prepass.idl
│  ├─ pipeline.json  frame_settings.json
├─ docs/
│  ├─ implementation-plan.md
│  └─ m0-checklist.md
├─ src/
│  ├─ core/               # math/vec/mat/bounds/log/assert/handle/profiler
│  ├─ gfx/                # device/texture/buffer/uniform/program/framebuffer/encoder/resource_manager
│  ├─ shader/             # shader_library/shader_key
│  ├─ render/
│  │  ├─ graph/           # render_graph
│  │  ├─ camera/          # view_state/history_buffer/resolution_group
│  │  ├─ pipeline/        # frame_settings/pipeline_settings
│  │  ├─ scene/           # render_scene/render_object/light
│  │  ├─ culling/         # frustum/draw_list/sort_key
│  │  ├─ material/        # material_system/render_queue/shader_pass
│  │  ├─ lighting/        # gpu_light_data/light_builder/light_list_builder
│  │  ├─ shadows/         # shadow_manager
│  │  ├─ passes/          # forward_renderer/prepass_utils
│  │  └─ debug/           # debug_display
│  └─ app/                # application/backend/window/debug_ui/gfx_test/render_test
├─ shaders/
│  ├─ common/             # common.sc/light_data.sc
│  ├─ lighting/           # light_loop.sc/shadows.sc
│  ├─ material/           # surface_data.sc/lit.sc/normal_buffer.sc
│  ├─ passes/             # forward/shadowcaster/prepass/motion/depth_pyramid/debug
│  └─ include/generated/  # IDL 产物
└─ tests/
   ├─ app/ asset/ core/ gfx/ render/
```

核心映射：

| 本工程 | HDRP |
|---|---|
| `RenderGraph` | RenderGraph |
| `ViewState` | HDCamera / ViewConstants |
| `DrawList` | RendererList |
| `LightListBuilder` | BuildGPULightList |
| `ShadowManager` | HDShadowManager / HDShadowAtlas |
| `MaterialSystem` | Material + shader 库 |
| `PostProcessChain` | RenderPostProcess |

---

## 2. 数据契约

### 2.1 GBuffer

| RT | 格式 | 内容 |
|---|---|---|
| RT0 | RGBA8 | baseColor + alpha |
| RT1 | RGBA8 | metallic/roughness/AO |
| RT2 | RGBA8 | 888 oct normal + roughness |
| RT3 | RGBA16F | emissive |
| Depth | D24S8/D32F | depth + stencil |

当前 forward-only；GBuffer pass tag 已完成，录制顺延。

### 2.2 GPU LightData

- `LightDataTex`：`RGBA32F`，4×32；每灯 4 texels：
  - `texel0`: position.xyz, range
  - `texel1`: color.rgb, intensity
  - `texel2`: direction.xyz, cosOuterAngle
  - `texel3`: shadowIndex, cosInnerAngle, layerMask, flags
- flags 低 2 位为 type（directional/point/spot），bit2 为 cast shadow。
- 方向光 shadowIndex=0（级联基址）；point/spot 为 ShadowContext 请求基址，point 加 0..5 face。

### 2.3 LightList

- v1 CPU FPTL：16×16 tile，64×64 big-tile 候选中间层；`R32_UINT` 2D 纹理布局 `width=33*tileCountX`、`height=tileCountY`，每 tile 为 1 count + 最多 32 个绝对 GpuLight 索引。
- 方向光不写入 tile list，shader 单独循环；punctual 由世界坐标投影到 tile 后 `texelFetch`。
- 后续可对齐 HDRP GPU compute：每类别独立列表、depth min/max fine pruning、32/64 log depth clusters。

### 2.4 ShadowContext

- `RGBA32F` 8×32，8 texels/request，最多 32 requests：
  - texel 0..3：world-to-shadow 矩阵列（C++ 按列写入）。
  - texel 4：`atlasScale.xy, atlasOffset.xy`。
  - texel 5：`depthBias, normalBias, invViewportWidth, invViewportHeight`。
  - texel 6：`type, faceIndex, 0, enabled`。
  - texel 7：保留。
- bgfx `mat4` 构造布局要求 shader `transpose` 后 `mul(M, position)`；已在 D3D11/Vulkan 验证。

### 2.5 全局常量

- Global CB 864 bytes / 19 descriptors：view/invView/proj/invProj/viewProj/invViewProj/nonJittered/prev、screenSize、cameraPos、prevCameraPos、taaJitter、taaFrameInfo、rtScale、dynamicResolutionScale、time、frustumPlanes[6]。
- Pass CB 240 bytes / 9 descriptors：shadowMatrix、shadowViewProj、shadowParams、shadowCascadeSplits、lightDirection、lightColor、ambientColor、lightCounts、lightListParams。
- Environment CB 128 bytes / 8 descriptors：skyParams(intensity,maxMip,enabled,reflection) + shAr/shAg/shAb/shBr/shBg/shBb/shCr（7×vec4 SH9 packing）；sky/IBL 纹理走 stage 10。

---

## 3. 帧顺序

```text
beginFrame
for each view:
  ViewState.update()
  graph:
    1  Shadow（每个 request 一个 raster pass）
    2  Scene Clear
    3  Forward Depth Prepass
    4  Objects Motion Vectors
    5  Camera Motion Vectors
    6  Depth Pyramid
    7  Sky（有环境贴图时；独立 fullscreen pass）
    8  Forward Opaque
    9  Forward Transparent
   10  Wireframe
   11  Presentation / Debug
UI
```

M1.7 后 shadow pass 数取决于 request：4 方向光 + 6 point faces/spot；FrameSettings 关闭 ShadowMaps 时完全跳过。

---

## 4. 任务分解

| 任务 | 产出 | 状态 |
|---|---|---|
| M0.1 工程骨架 | CMake + C++20 + 依赖 + 双后端窗口 | `[x]` |
| M0.2 gfx 封装 | Device/Encoder/Texture/Buffer/FrameBuffer/ResourceManager | `[x]` |
| M0.3 IDL/常量 | `idl_gen.py`、C++/shader 生成、反射校验 | `[x]` |
| M0.4 glTF | cgltf、材质/贴图/相机/灯光、MikkTSpace、sidecar | `[x]` |
| M0.5 Forward PBR | MaterialSystem、HDR、ACES、render-test | `[x]` |
| M0.6 光照/阴影 v0 | LightData、方向光阴影、PCF | `[x]` |
| M0.7 调试设施 | ImGui、Profiler、wireframe、截图 | `[x]` |
| M1.1 RenderGraph | Handle/PassBuilder/compile/execute/裁剪/池 | `[x]` |
| M1.2 FrameSettings | 128 位、PipelineSettings、JSON | `[x]` |
| M1.3 ViewState | 矩阵/抖动/历史/RT scale/全局 CB | `[x]` |
| M1.4 DrawList | queue/pass tag/frustum cull/sort/instancing | `[x]` |
| M1.5 Prepass | depth/normal/motion/depth pyramid | `[x]` |
| M1.6 光照 v1 | CPU LightList、R32_UINT、Forward LightLoop | `[x]` |
| M1.7 阴影 v1 | 4 级联、point/spot、双 atlas、ShadowContext、PCF | `[x]` |
| M1.8 Sky/IBL | HDRI 天空、SH9 环境光、GGX mip、天空反射 | `[x]` |
| M1.9 最终输出 | 固定/简单自动曝光、ACES/Neutral、sRGB、Debug 覆盖 | `[x]` |
| M1.10 Debug | 深度/法线/速度/Cluster/阴影图集/Albedo/Roughness | `[x]` |

**M1 出口标准**：glTF + HDRI + 级联阴影 + 聚类灯光 + 运动矢量 + Tonemap + RenderGraph + FrameSettings；1080p 60fps（GTX 1660/RX 5600 级）。

---

## 5. HDRP/Core 源码索引

| 模块 | HDRP/Core |
|---|---|
| RenderGraph | `RenderGraph.cs:1448/1490/1540`；`NativePassCompiler.cs:475/490/539`；`RenderGraphResourcePool.cs:54/65/197` |
| FrameSettings | `FrameSettings.cs:107/429/455/508/658/820`；`FrameSettingsDefaults.cs:7/92/149`；`RenderPipelineSettings.cs:31/259/337/350/387/399` |
| ViewState / jitter | `HDCamera.cs:24/32/1137/1436-1451/2107/2338`；`HDCamera.cs:1594/1625`；`ShaderVariablesGlobal.cs:40` |
| DrawList | `HDRenderQueue.cs:16-174`；`HDStringConstants.cs:4-120`；`HDUtils.cs:23`；`HDRenderPipeline.cs:177-199/3410-3500/3556-3560` |
| Prepass | `HDRenderPipeline.Prepass.cs:84-121/146-161/185-201/275-420/485-645/648-707/1651-1670`；`NormalBuffer.hlsl:20/44`；`Packing.hlsl:57/71/527/538`；`DepthPyramid.compute`；`MipGenerator.cs:62-118` |
| LightList | `HDRenderPipeline.LightLoop.cs:179-313/665-697`；`LightLoop.cs:106-108/558-565/635-653/1921-1954/1963-1966/2030-2043`；`LightLoopDef.hlsl:245-330`；`LightLoop.hlsl:187/252-312`；`lightlistbuild*.compute`；`HDGpuLightsBuilder.Jobs.cs:422-462/639-651` |
| 阴影管理 | `Lighting/Shadow/HDShadowManager.cs:615`；`HDShadowData :73`；`ReserveShadowResolutions :929`；`CreateShadowData :1014`；`LayoutShadowMaps :1072`；`PrepareGPUShadowDatas :1110`；`RenderShadows :1414` |
| 阴影 atlas/请求 | `HDShadowAtlas.cs:102/207/758`；`HDDynamicShadowAtlas.cs:39/72/129`；`HDShadowUtils.cs:45/101/161/298/374`；`HDShadowContext.hlsl`；`HDShadowSampling.hlsl:14-111`；`HDShadowAlgorithms.hlsl:36-144` |
| Sky/IBL | `Sky/HDRISky/HDRISkyRenderer.cs:90-161`；`HDRISky.shader`；`Sky/SkyUtils.hlsl:14-39`；`Sky/AmbientProbeConvolution.compute:51-215`；`Material/GGXConvolution/IBLFilterGGX.cs:129-194`；`GGXConvolve.shader`；Core `SphericalHarmonics.hlsl`/`ImageBasedLighting.hlsl` |
| Lit pass 名 | `Material/Lit/Lit.shader`（GBuffer:435、Forward:956、ShadowCaster:602、DepthOnly:640、MotionVectors:705） |
| Volume | Core `Runtime/Volume/VolumeManager.cs` |
| 最终输出 | `HDRenderPipeline.PostProcess.cs:1455-1585`；`PostProcessing/Shaders/Exposure.compute:15-153`；`ExposureCommon.hlsl:26-121`；Core `ShaderLibrary/PhysicalCamera.hlsl:12-74`；`ColorUtils.cs:39-55/224-288`；`Tonemapping.cs`；Core `Color.hlsl:608-786`；`FinalPass.shader:170-205` |
| 后处理 | `HDRenderPipeline.PostProcess.cs:802` |

---

## 6. M2~M6 路线

- **M2 Deferred**：GBuffer、Deferred Lighting、Cluster、SSR/SSAO/SSGI、Decals、Reflection Probes。
- **M3 后处理**：高级 Exposure/histogram、TAA、DOF、Motion Blur、Bloom、Color Grading、Uber、FXAA。
- **M4 阴影/光照完善**：cached/mixed shadow、PCSS/EVSM、area light shadow、screen-space/contact shadow、light layers。
- **M5 资产/材质**：glTF 完整特性、压缩纹理、skin/morph/animation、Sponza/Khronos 对齐。
- **M6 性能/工具**：Async compute、indirect、GPU cluster、RenderDoc 金图、PSNR/SSIM。

### 6.1 性能目标

- Shadow Atlas：2048² 起步；方向光级联单独 atlas；punctual atlas 动态布局。
- LightData：32 灯 × 4 texel × 16 B ≈ 2 KB；ShadowContext 8×32×16 B = 4 KB。
- DrawCall 目标 < 3000/帧；按 Program/材质排序减少状态切换。
- 验证：单元测试（Graph 编译、阴影打包、Volume 混合、排序键）+ 固定场景金图（PSNR/SSIM）+ RenderDoc + bgfx stats。

---

## 7. 最大风险与对策

1. **Shader 重写量最大**：只移植 Lit/Unlit/Decal/SSS/后处理数学，不追求全材质；用变体 Key 严格收敛。
2. **图形阶段无 Storage Buffer**：灯光/阴影/探针全走 Data Texture，注意布局紧凑和缓存命中。
3. **ShadowContext 矩阵布局易错**：C++ 按列写，shader `transpose` 后 `mul(M,v)`；新增请求时用 D3D11/Vulkan 双后端截图和 CPU 单测同时验证。
4. **点光/聚光阴影边界**：固定 face 分辨率与 dominant-axis 选择会有 face 边界；M2 再补 seam、PCSS 和 cached shadow。
5. **RenderDoc 缺失**：本机无法 F11 抓帧；M1 验收用双后端截图 + bgfx stats + RenderGraph diagnostics。
