# hdrp-bgfx 总体实现方案（收敛版）

> 工程根：`E:\Learn\hdrp-bgfx`。
> 范围：Windows + D3D11/Vulkan、Forward + Deferred、SSR/SSAO/SSGI/体积雾、glTF、暂无编辑器。
> 实现状态：M0 见 `docs/m0-checklist.md`，M1 见 `docs/m1-checklist.md`，M2/M3 见对应清单，M4.0–M4.3 见 `docs/m4-checklist.md`；M5.0/M5.1/M5.2/M5.3/M5.4 已完成，见 `docs/m5-checklist.md`；M6.0/M6.1/M6.2/M6.3/M6.4 已完成，见 `docs/m6-checklist.md`；M7.0–M7.8 完成并验收（buffer path 默认开启），见 `docs/m7-checklist.md`；M8.0–M8.7 已完成，见 `docs/m8-checklist.md`。

---

## 文档结构

- `docs/implementation-plan.md`：总体架构、数据契约、帧顺序、路线和风险，不记录逐任务验证细节。
- `docs/m0-checklist.md`：M0.1–M0.7 工程骨架、gfx 封装、IDL、glTF、Forward PBR、基础光照/阴影、调试设施。
- `docs/m1-checklist.md`：M1.1–M1.10 RenderGraph、FrameSettings、ViewState、DrawList、Prepass、光照 v1、阴影 v1、Sky/IBL、最终输出、Debug；常用命令也维护在这里。
- `docs/m2-checklist.md`：M2.0–M2.9 Deferred 基础、GBuffer、Deferred Lighting、Cluster、SSAO/SSR/SSGI、Decals、Reflection Probes、验收。
- `docs/m3-checklist.md`：M3.0–M3.8 后处理基础设施、Exposure/Histogram、Uber/Color Grading、Bloom、TAA、DOF、Motion Blur、FXAA、验收。
- `docs/m4-checklist.md`：M4.0 Global Volumetric Fog v1、M4.1 质量/优化、M4.2 局部雾体、M4.3 验收。
- `docs/m5-checklist.md`：M5.0 shadow 设置/atlas/culling/alpha test、M5.1 软阴影 bias/micro shadow、M5.2 contact shadows、M5.3 per-light shadow controls/cache、M5.4 验收。
- `docs/m6-checklist.md`：M6.0 GPU light-list 基础设施/screen AABB、M6.1 GPU big-tile/FPTL、M6.2 GPU cluster、M6.3 集成/回退/debug、M6.4 验收；v1 为 texture-backed，不做 GPU shadow indirect。
- `docs/m7-checklist.md`：M7.0–M7.8 完成：buffer path 默认开启；indirect/GPU shadow/async 保持 opt-in。alpha/cache/cascade 组合双后端 0 diff；1080p 300 帧 full post 双后端 >60 fps。
- `docs/m8-checklist.md`：M8.0–M8.7 已完成：32/64/128 设置、CPU reference、动态 stride、GPU producers/consumers、cluster 32/cell 与 overflow 诊断、texture/CPU capacity clamp 与 consumer 审计、CLI/UI 诊断与 GPU cluster overflow 数值 readback、1080p 性能与默认 64 决策、最终双后端验收；M9 规划与实现记录见 `docs/m9-checklist.md`（M9.0–M9.6 核心完成；M9.7 部分完成）。
- 后续每个大模块新建独立清单：`docs/m9-checklist.md`……，不把新模块写回旧清单。
- `docs/vfx/README.md`：VFX 运行时 V1 规划索引（独立 V 编号，不与 M13 冲突）。
- `docs/vfx/architecture.md`：独立库/适配层边界、编译与运行时流水线、渲染集成、binding/stage 表、测试与风险。
- `docs/vfx/asset-format.md`：`.vfx.json` schema 与 `.vfxb` 二进制格式（dxbc+spirv、确定性规则）。
- `docs/vfx/backend-api.md`：公开 C++ API、Device/CommandSink/ShadingProvider、hdrp-bgfx 映射。
- `docs/vfx/source-alignment.md`：Unity VFX Graph / HDRP / hdrp-bgfx 的 `file:line` 对齐与偏差登记。
- `docs/vfx/v0-checklist.md`：V0 技术 spike 任务/参考/验收。
- `docs/vfx/v1-checklist.md`：V1.0b–V1.4 逐任务清单/参考/验收。
- `docs/vfx/v2-editor-outline.md`：编辑器后续边界（占位，不实现）。

---

## 当前进度（2026-11-13）

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
- `M1.10 Debug`：核心完成。 5 个全屏模式；硬件深度/线性化 depth pyramid、normal、HDRP hue-wheel motion、CPU 64×64×32 LightCluster（M2.3）、SSAO（M2.4）、SSR（M2.5）、SSGI（M2.6）、方向 punctual shadow atlas、opaque material debug 的 Albedo/Roughness；`DebugUniforms` 32B、debug-only atlas clear、camera motion 采样 depth pyramid 修复 D3D DSV/SRV 冲突。
- `M2.0 Deferred 基础`：核心完成。`pipeline.json=both`、`--lit-shader-mode/--deferred`、FrameSettings 分支。  RT GBuffer 资源（复用 scene depth）、GBuffer Clear + 4RT/depth FBO、D24S8/stencil 能力 fallback、RenderGraph per-pass viewport、deferred pass 顺序日志。
- `M2.1 GBuffer`：核心完成。GBuffer clear MRT shader、 6 变体 Lit GBuffer shader、Lit opaque GBuffer draw list、deferred depth-only prepass、depth equal/no write + stencil bit0、RT2 normal debug、 RT 双后端渲染；补零 tangent 保护以启用 derivative fallback；修正 888 normal buffer 解码高低 4bit 顺序。
- `M2.2 Deferred Lighting`：核心完成。fullscreen deferred_lighting shader、独立 GBuffer decode/direct lighting 头、stencil bit0 全屏光照、方向光+point/spot+阴影、SH9/IBL、forward-only opaque pass、transparent 保持 M1；M2.2 后的普通 deferred 画面已完整光照。
- `M2.3 Cluster LightList`：核心完成。CPU 64×64×32 log-depth cluster、`R32_UINT` count+32 索引 atlas、`u_cluster_params/u_cluster_depthParams`、transparent/forward-only 用 cluster、deferred/forward opaque 继续 FPTL、`--no-light-cluster` 回退、debug slice（-2 legacy FPTL / -1 depth / 0..31 fixed）。
- `M2.4 SSAO`：核心完成。半分辨率 8×6 GTAO-lite（normal-plane occupancy）。 ×5 depth/normal bilateral 去噪、全分辨率 2×2 bilateral upsample；Deferred Lighting stage 11 只乘 ambient/IBL；`supportSSAO`/`FrameSettings::SSAO`/`--ssao`/`--no-ssao`、 ×1 白 fallback、`--debug-view=ssao`。
- `M2.5 SSR`：核心完成。全分辨率 `ssr_trace -> ssr_composite`；GBuffer/depthPyramid 重建。 2 步 world-distance march + crossing/thickness、fresnel/roughness/edge 权重、additive 分 sceneColor；命中额外加屏幕反射，miss 保持 deferred IBL；`supportSSR`/`FrameSettings::SSR`/`--ssr`/`--no-ssr`、`--debug-view=ssr`、D3D11/Vulkan 验证。
- `M2.6 SSGI`：核心完成。半分辨率 32 rays × 8 steps、当前帧 sceneColor/depthPyramid/GBuffer2；`ssgi_trace -> ssgi_denoise -> ssgi_composite`； ×9 depth-aware denoise + 9×9 upsample，additive stencil 合成；SH/ambient fallback，`ambientBias=0.5` 部分扣除已有 ambient；`supportSSGI`/`FrameSettings::SSGI`/`--ssgi`/`--no-ssgi`、`--debug-view=ssgi`、D3D11/Vulkan 验证。
- `M2.7 Decals`：核心完成。 RT DBuffer（albedo/encoded normal/packed MAOS）、sidecar 静态 box decal、全屏 box-clip draw、angle fade、GBuffer `GBufferDecals` 变体合成；`supportDecals`/`FrameSettings::Decals`/`--decals`/`--no-decals`、`--debug-view=decals`、D3D11/Vulkan 验证。
- `M2.8 Reflection Probes`：核心完成。sidecar 最多 4 个静态 equirect probe，CPU 选择最近 2 个；`worldToProbe` box projection、blendDistance、per-probe intensity/maxMip、stage 14/15 纹理、sky fallback；`FrameSettings::ReflectionProbe`/`--reflection-probes`/`--no-reflection-probes`；forward/deferred 共用；修正 forward instanced 的 environment/probe 绑定。D3D11/Vulkan 验证。
- `M2.9 验收/性能`：完成。deferred GBuffer debug 新增 metallic/AO/emissive；Albedo/Roughness 改读 GBuffer，不再 material 重绘；修正 normal debug V 翻转；`ForwardRenderer::FrameDiagnostics` + deferred MRT/stencil 日志；`render-test` 输出 avg frame/CPU/GPU/draw/passes； 1080p D3D11/VK base 145.9/348.5fps、full-feature 115.5/115.3fps；默认 `frame_settings.json` 分 deferred。
 - `M3.0 PostProcess 基础`：完成。`PostProcessChain`/`PostProcessProfile`/`PostProcessHistoryState` 接入；`config/postprocess.json` effects-off baseline + `--postprocess-profile` + TAA/FXAA/Bloom/DOF/Motion Blur CLI overrides； ×1 exposure ping/pong；`PostProcessUniforms` 增加 frame/screen/histogram；`TextureDesc::computeWrite`、`Encoder::setImage`、compute program 加载、`PostProcessExposure` 1×1 compute；`Final` LDR→sRGB shader 可加载但不默认调用；effects off 在 D3D11/Vulkan 与 M2.9 `m28_blend_view` 截图逐像素一致；`--gfx-test` 双后端 compute dispatch 通过。
- `M3.1 高级 Exposure`：完成。Fixed/Automatic/Histogram； 56×256 log-luminance prepass → 32×32 → 1×1、128-bin R32U histogram + `imageAtomicAdd`、 0“ 0% percentile、时间适应、 ×1 exposure ping-pong、`Apply Exposure` HDR pass；Fixed 保持旧 Tonemap 且不增加 pass；`--exposure-mode=fixed|auto|histogram`、DebugUI、`--debug-view=exposure`。D3D11/VK histogram 输出 PSNR 70.4 dB、Automatic 69.6 dB；Fixed D3D11 逐像素一致；RelWithDebInfo 1080p 300 帧 histogram D3D11 97.2 fps / Vulkan 201.4 fps、Automatic D3D11 121.2 fps / Vulkan 257.2 fps。compute sampler stage 0 不能与输出 image stage 0 同用，输出 image 用 stage 1/2；数值 texture readback 仍受 vendored bgfx 限制，以 D3D/VK debug 图逐像素一致验收。
- `M3.2 Uber/Color Grading`：完成。`ColorGradingSettings`/`VignetteSettings`、CPU 64³ LUT -> 64×4096 RGBA16F strip（2,097,152 B）、White Balance/Contrast/Color Filter/Split Toning/Channel Mixer/SMH/LGG/Hue/Saturation、Uber HDR pass、Final sRGB、`--color-grading`、DebugUI、`--debug-view=color-grading`。LUT 变更只上传一次；neutral fixed ACES 与修正后的 M2.9 Tonemap PSNR 60.36 dB，D3D/VK Uber 64.28 dB； 1080p 300 帧 grading D3D11 107.3 fps / Vulkan 183.1 fps。修正全屏 Tonemap/ApplyExposure source 采样 Y 翻转，旧 M2.9 截图等价于旋转 180°。
- `M3.3 Bloom`：完成。HDRP threshold（gamma 2.2 + soft knee）、half/quarter base、最多 8 mip transient chain、mip0 9-tap separable blur、 ×3 tent/bicubic upsample + scatter、luminance-normalized tint、Uber composite（`2^intensity-1`）；`--bloom` 参数、DebugUI、`--debug-view=bloom`。Bloom off 与 M3.2 逐像素一致；D3D/VK bloom-on 64.50 dB、debug 73.96 dB； 1080p 300 帧 D3D11 97.9 fps / Vulkan 297.0 fps。
- `M3.4 TAA`：完成。HDRP jitter/Halton 序列、双缓冲 `RGBA16F` history、motion reproject、history bilinear、3×3 min/max clamp、depth/luma/motion rejection、可选 post-sharpen；TAA 在 Apply Exposure 后、Bloom/DOF 前，TAA off 与 M3.3 逐像素一致。D3D/VK TAA 64.22 dB、TAA history debug 65.84 dB、 1080p 300 帧 D3D11 96.2 fps / Vulkan 280.8 fps；不做 TAAU/透明 MV/Blackman-Harris。
- `M3.5 Depth of Field`：完成。Manual ranges near/far focus、signed CoC、half/quarter/full CoC+gather、 /12-tap depth-aware gather、full-res sharp combine、max blur、`--dof-*`、DebugUI、`--debug-view=dof`；DOF off 与 M3.4 逐像素一致。D3D/VK DOF 68.43 dB、farfocus 66.55 dB、HQ 68.59 dB、CoC debug 逐像素一致、TAA+DOF+Bloom 68.53 dB； 1080p 300 帧 DOF D3D11 162.7 fps / Vulkan 233.7 fps，full stack 119.5 / 260.5 fps；不做物理相机 tile-based/CoC reprojection。
- `M3.6 Motion Blur`：完成。HDRP 语义 intensity/max velocity 200 px/min velocity 2 px/cameraMotionBlur/sampleCount 8/depth extent 1；单 raster pass 3×3 local max 近似 tile neighborhood； /16 taps、depth compare、top/back layer blend、camera MV 可选扣除；`--motion-blur-*`、DebugUI。off 0 diff、静态 0 diff、D3D/VK **58.39 dB**、TAA+DOF+MB+Bloom **63.25 dB**；1080p 300 帧 MB D3D11 281.6 fps / Vulkan 353.0 fps，full stack 202.0 / 270.3 fps；不做 tile min/max/scattering/merge、transparent MV、special camera clamp。
- `M3.7 FXAA + Final`：完成。`AntialiasingMode`/`resolveAntialiasingMode` 固定 TAA 优先；`final.fs.sc` 编译 FXAA off/FXAA 两个变体，FXAA port 自 HDRP `FXAA.hlsl` 非 HDR 分支（luma edge、dirReduce/span clamp、4-tap blend），在 sRGB 前执行；FXAA 开启时强制 Uber Post 产生 LDR linear，FinalPass 统一 sRGB。`--aa=fxaa`/`--fxaa`/`--no-fxaa`、profile、DebugUI、`--debug-view=fxaa`、`supportFXAA=true`。off 与 M3.6 逐像素一致；D3D/VK FXAA **64.27 dB**、FXAA+DOF+Bloom+MB **66.55 dB**；1080p 300 帧 FXAA D3D11 288.9 fps / Vulkan 251.0 fps，full stack 219.1 / 204.6 fps；不做 SMAA/TAAU、alpha FXAA、HDR output encoding。
- `M3.8 M3 验收`：完成。`render performance` 增加 `postprocess_ms`（CPU graph-record wall，avg）、effect flags（exposure/uber/bloom/taa/dof/motion_blur/fxaa）和既有 `post_passes`、`gpu_frame_ms`；D3D11/Vulkan 后 25 张截图（exposure/bloom/grading/TAA/DOF/MB/FXAA/all/debug）。effects off D3D/VK **64.22 dB**，`m3_base_off_d3d.png` 与 M3.7 基线 **0 diff**；full 1080p 300 帧重复 3 次 **195.7–212.3 fps (D3D11) / 204.4–215.4 fps (Vulkan)**，forward **222.2 / 199.1 fps**，post wall 线 **0.33– .39 ms**（ .5 render scale 与 TAA **960×540**、half DOF **480×270**、final **1920×1080**，D3D/VK **105.66 dB**。RenderDoc/F11 仍未安装，保留双后端截图 + bgfx stats + graph diagnostics 验收。
- `M4.0 Global Volumetric Fog v1`：完成。`VolumetricFogSettings`/JSON profile/`--fog-profile`、`fog.idl`、`volumetric_fog.fs.sc`；单 pass screen-space 16-step height fog ray march，HDRP `ComputeHeightFogMultiplier`/Cornette-Shanks 相位和 primary directional shadow；在 scene color 后、postprocess 前合成，`FrameSettings::AtmosphericScattering/Volumetrics` + `supportVolumetrics` 门控。fog off 与 M3.8 基线 D3D11 **96.87 dB**/Vulkan **0 diff**；fog on 1280×720 D3D/VK **64.36 dB**，full post 1080p **69.09 dB**，forward D3D/VK **64.36 dB**（ .5 scale **64.35 dB**；1080p 300 帧 fog-only D3D11/Vulkan **203.5/199.6 fps**（无 fog **264.5/320.0**），full post+fog **106.8–140.2/142.3–160.0 fps**。边界：不是 HDRP 3D VBuffer，无 temporal/低分辨率/局部雾/点光体积光 debug view。详见 [m4-checklist.md](m4-checklist.md)。
- `M4.1 Fog 低分辨率/Temporal/Punctual`：完成。`resolutionScale=0.5` 的 low-res raymarch 输出 `(inScattering.rgb, transmittance)`，`volumetric_fog_composite` 全分辨率 4-tap depth-aware upsample；low-res history 双缓冲 + motion reprojection + depth rejection + 可选 step jitter；point/spot 用 cluster/tile/FPTL fallback、可选 punctual shadow，默认最多 8 灯。temporal off 静态 no-jitter 与 temporal on **0 diff**；no-point fog 对 M4.0 scale 1.0 **66.39 dB**、scale 0.5 **65.59 dB**；D3D/VK no temporal **64.40 dB**、temporal **62.10 dB**，full post **66.32 dB**；1080p 300 帧 fog-only D3D11/Vulkan **170.0/189.5 fps**（无 fog **232.7/224.0**），full post+fog **154.6/156.4 fps**，post wall **0.381/0.343 ms**。边界：2D screen-space + 2D history，不是 HDRP 3D VBuffer；point/spot 是逐 step 近似，不含 area/box light、local fog/VFX/clouds。详见 [m4-checklist.md](m4-checklist.md)。
- `M4.2 Local fog volumes`：完成。`LocalFogVolume` sidecar（box/sphere、OBB/椭球 falloff、HDRP face/distance fade、linear/exponential、 D 投影 density mask），`fog.idl` 8 组 local volume + 4 mask 槽，fog raymarch 合并 local extinction/scattering；`--fog-local-volumes`/`--no-fog-local-volumes`、`fog_local`/`fog_local_masks`。M4.1 no-local regression **94.77 dB**；local box D3D/VK **82.34 dB**、sphere **82.33 dB**、mask on/off **47.54 dB**、temporal **62.71 dB**、full post **64.91 dB**；1080p 300 帧 fog-only local D3D/VK **215.3/206.7 fps**（同场景 no-local **213.6/208.8**），full post **179.8/170.4 fps**，post wall **0.337/0.333 ms**。边界：静态 sidecar、 D mask 不是 Texture3D、只做 additive、无 VBuffer/voxelization。详见 [m4-checklist.md](m4-checklist.md)。
- `M4.3 M4 验收`：完成。最终 D3D11/Vulkan 矩阵：fog off 与 M3.8 双后端 **0 diff**，M4.1 no-local 回归 D3D **96.21 dB**/VK **0 diff**；global temporal **64.41 dB**、local box **62.72 dB**、sphere **62.71 dB**、mask on/off **48.76 dB**、forward local **62.74 dB**、full post+local **64.69 dB**、point/spot **84.44/96.75 dB**、transparent **93.46 dB**；`--debug-view=normal` fog on/off **0 diff**。1080p 300 帧 full post+local 3 次 **153.1–171.8 / 156.3–176.2 fps**，post wall **0.367–0.394 / 0.333–0.371 ms**；fog-only local **177.5/200.7 fps**。`--fog-steps` 对齐 sanitize 为 **4..64**。测试 **177/177，18,104 assertions**。详见 [m4-checklist.md](m4-checklist.md)。
- `M5.0 Shadow settings/atlas/culling/alpha test`：完成。新增 `ShadowSettings`/`config/shadows.json`、`--shadow-*` CLI；`GpuShadowContext` 10 texels、context 64 行；cascade 1“ 、directional/point/spot resolution、atlas 1024/2048/4096、 4 requests；`DrawList::buildCulled` 做 per-request frustum cull 并暴露 `shadow_drawn/shadow_culled`；shadow caster 接 alpha mask material variant、base color texture 和 cutoff；`pcf7x7/pcss` 在 M5.0 先回退 5×5。验证：**184 cases / 18,212 assertions**；shadow off D3D/VK **109.54 dB**；cascade2/1024 atlas D3D/VK **109.54 dB**；culling 前后 **0 diff**；cutout cube shadow atlas 有镂空。HDRP `useScreenSpaceShadows` 是 ray tracing only，M5 不伪装 DXR。详见 [m5-checklist.md](m5-checklist.md)。

- `M5.1 Soft shadows/bias/micro shadows`：完成。`--shadow-quality`/filter mode 0” ；HDRP tent PCF 3×3/5×5/7×7； /9/16 bilinear taps）与 Fibonacci-disk PCSS（默认 blocker/filter 16/24，minFilterSize 0.01）；point depth blocker sampler 在 forward/deferred stage 12/13，fog 回退 tent；normal bias 使用 world texel + perspective 距离，slope bias 写 shadow caster view depth bias，shadowNearPlane 进入 point/spot；`cascadeFade` 在最大距离前淡出；directional micro shadow 通过 `shadowParams.y` 接入 forward/deferred，默认关。验证：**192 cases / 18,416 assertions**；medium D3D/VK **112.55 dB**，PCSS **74.58 dB**，spot PCSS **0 diff**，point PCSS **61.41 dB**，deferred PCSS D3D/VK **74.53 dB**，micro helmet on/off **41.31 dB**；Debug 1080p 300 帧 medium D3D/VK **79.3/70.8 fps**、high **63.2/53.3 fps**；RelWithDebInfo 同场景 medium **96.0/81.0 fps**、high **83.9/128.5 fps**。详见 [m5-checklist.md](m5-checklist.md)。
- `M5.2 Contact shadows`：完成。新增 `ContactShadowSettings`/`--contact-shadows`/`--contact-shadow-*`；forward 输出 stage 11 RGBA8 contact texture，deferred 写 GBuffer1.b/.a，half-res 2x2 展开；compute 仍 full-res depth 重建 world position，世界空间厚度判定，跳过前三个 step，IGN dither、距离 fade、屏幕 vignette；`GpuLightData.shadowFlags.z` 存 8-bit contact mask（directional bit0，punctual bit1..7），`hbgContactShadowVisibility` 只乘直接光；透明物件禁用。验证：**192 cases / 18,416 assertions**；D3D/VK full **88.43 dB**（1 pixel max21），half 逐像素一致，forward half **112.55 dB**；contact on/off **45.63 dB**，full/half **46.38 dB**；1080p RelWithDebInfo full10 **190.0–253.2 / 211.2–285.0 fps**（D3D/VK 多次单次测量；off 182.4、half 186.9、  samples 198.7、 6 samples 113.7 fps）。详见 [m5-checklist.md](m5-checklist.md)。
- `M5.3 Per-light controls/cached punctual shadows`：完成。`LightShadowSettings` + sidecar `lights[].shadow`（dimmer/fadeDistance/nearPlane/resolution/tint/penumbraTint/updateMode/contactShadows/softness/samples）；`GpuLightData` 扩到 6 texels，`light_loop`/`direct_lighting` 应用 dimmer + HDRP `ComputeShadowColor`，contact mask 继续在 texel3.z；point/spot atlas viewport、near plane、PCSS samples 支持 per-light； 0%– 00% fadeDistance 线性衰减并剔除超距 request；cached punctual v1 使用独立 cached atlas、内容 hash 失效、cached request 不重绘但保留空 view 以保持 bgfx depth texture 状态，`shadow_cache/shadow_cached` 诊断，debug punctual atlas 自动抑制 cache。验证：**199 cases / 18,574 assertions**；D3D/VK cached vs every-frame 静态 **0 diff**，D3D/VK per-light sidecar **103.52 dB**，sidecar 相对基线 **39.63 dB**；1080p 300 帧 D3D cache **243.1 vs 173.9 fps**（draw 54 vs 60，shadow_drawn 8 vs 14），Vulkan 单次 **292.0–334.6 vs 348.9 fps**（波动大，M5.4 复测）。详见 [m5-checklist.md](m5-checklist.md)。
- `M5.4 M5 验收`：完成。新增 `DebugDisplayMode::ShadowCascade` / `ShadowFilter` 与 `--debug-view=shadow-cascade|shadow-filter`（HDRP cascade 色、方向光 filtered visibility）；`--no-shadows` 无 shadow/contact pass（passes 30，shadow on 40，`shadow_drawn=0`、`contact=false`），D3D/VK **64.24 dB**（max 4）。M4.3 最终图带阴影，无可用 shadow-off 0-diff 参考，未伪造该指标、1280×720：filter D3D/VK max 4” ，PCSS vs medium **51.72 dB**；contact full D3D/VK **56.42 dB**（max 13），full vs off **52.05 dB**；alphaMask/opaque **21.46 dB**，alpha blend 不投影；cache on/off 双后端 **0 diff**；forward/deferred **49.97 dB**（contact 路径）。 1080p 300 帧 full post **180.5–242.1/218.3–288.5 fps**（D3D/VK； 4 post passes），shadow-only **205.3/396.3 fps**，cache off→on D3D **268.2→288.3/282.3**、VK **345.3→324.7/343.4 fps**。门禁 **199/18,580**、ctest 2/2、双后端 gfx、docs/diff。详见 [m5-checklist.md](m5-checklist.md)。
- 当前验证：`ctest` 203 个用例、 9,713 assertions + `hbg_idl_check` 通过（M1 基线 86/15842）；双后端 `--gfx-test`；M2.0/M2.1 D3D11/Vulkan deferred 2 帧、 RT GBuffer normal/normal-map/double-sided、alpha 半裁剪、`--render-scale=0.5`（640×360 GBuffer / 1280×720 presentation）、D32F fallback；M2.2 forward/deferred parity：基础球 67.39 dB、多光源+阴影 58.46 dB、Uffizi+IBL 64.00 dB、normal-map 67.34 dB、 .5 scale 59.46 dB、unlit pair 58.77 dB；D3D/Vulkan deferred light scene 92.38 dB、sky 57.72 dB；M2.3 1080p cluster `30×17×32` / `990×544` / 2,154,240 B；transparent scene cluster vs FPTL 两后端逐像素一致，debug slice D3D11/Vulkan 逐像素一致，随机 16 灯/FPTL union/depth range 单测通过；M2.4 1080p AO `960×540 trace + 1920×1080 upsample`，boxes scene AO debug D3D/Vulkan 91.34 dB、AO on/off 差值 98.08 dB；640×360 graph 30（开）/27（关），`--ssao`/`--no-ssao` 与 `--debug-view=ssao` 验证；M2.5 Uffizi 金属场景 SSR on/off 3.282 RMSE / 37.81 dB、D3D/Vulkan debug 90.24 dB、on 图 D3D/VK 58.39 dB，roughness 0.65 与 unlit 场景 on/off 逐像素一致，graph 43（开）/41（关），`--ssr`/`--no-ssr` 与 `--debug-view=ssr` 验证；M2.6 corner 场景 SSGI on/off 0.06597 RMSE / 23.61 dB，D3D/VK on 图 69.59 dB、debug 69.46 dB、off 102.13 dB，graph 42（开）/39（关），SSR+SSGI 44，forward 37（on/off 逐像素一致）；1080p trace 960×540 / gpuFrame=5，`--ssgi`/`--no-ssgi` 与 `--debug-view=ssgi` 验证；M2.7 3 个 box decal 的 DBuffer 3RT：on/off 0.053227 RMSE / 25.48 dB，D3D/VK on 101.09 dB、off 102.13 dB、debug 逐像素一致，graph 43（开） 39（关），单个 decal 41，forward on/off 37 逐像素一致，1280×720 DBuffer 11,059,200 B、 1080p 24,883,200 B / gpuFrame=5，`--decals`/`--no-decals` 与 `--debug-view=decals` 验证；M2.8 静态 probe：单 probe on/off 0.315103 RMSE / 10.03 dB，D3D/VK on 64.17 dB、off 61.70 dB，双 probe blend 64.22 dB、rotation 64.21 dB，forward/deferred on 56.52 dB、off 48.21 dB，`--no-reflection-probes` 与无 probe sidecar 逐像素一致，5 probe 告警并取前 4； 1080p graph 40/40；M2.9 deferred base D3D/VK 64.2224 dB、forward D3D/VK 64.2174 dB、forward/deferred 56.7363 dB、全特性 D3D/VK 69.6591 dB；GBuffer debug albedo/normal 逐像素一致，roughness/metallic/AO/emissive D3D/VK 49.79/49.80/39.56/76.16 dB；feature-off 与 forward feature flags 逐像素一致；render-scale 0.5 为 960×540 GBuffer / 1920×1080 presentation； 1080p D3D11 deferred/forward 145.9/140.5fps，full-feature 115.5fps；Vulkan 348.5/323.5fps，full-feature 115.3fps；DamagedHelmet 标准/prepass、重复 helmet instancing、shadow scene、point/spot LightList 与阴影、透明场景、ImGui/debug light-data 截图；shadow scene graph `37/37`，point `33/33`，spot `28/28`；M1.8 双后端 sky graph `28/28`，Uffizi/DefaultHDRI 天空、金属反射、SkyReflection 开关；M1.9 固定/自动曝光、ACES/Neutral、D3D11/Vulkan 一致性；M1.10 depth/normal/motion/cluster/双 atlas/albedo/roughness/SSAO 双后端截图，shadow atlas debug graph `38/38`、material `27/27`；Sponza 103 draw items / 27,013,502 triangles（本地贴图缺失）。
- 遗留验收：alphaMask 外部场景、Khronos 参考视觉对齐、sampler wrap/压缩纹理、shadow 视觉细化；RenderDoc 抓帧需安装 RenderDoc 后补。M1.5 边界：透明物体 MV、moving object prevWorld authoring 未接；TAA/motion blur 已在 M3.4/M3.6 接入；深度金字塔为 RG32F min/max + scratch。M1.6 边界：LightList 为 CPU 屏幕空间 AABB v1，不是 HDRP GPU compute/cluster；无 depth fine pruning/per-voxel clusters/light layers/categories/probes/decals。M1.7 边界：M5.0 已补可配置 atlas/cascade/resolution、per-request frustum cull 和 alphaMask shadow；仍无 shadow cache、shadow mask/screen-space/contact shadow；M5.1 已接 tent PCF/PCSS、bias、cascade fade 和 directional micro shadow；仍无 EVSM/VSM 和 volume 配置。M1.8 边界：IBL 为 1024×512 equirect 7 mips、sky 2048×1024 raw，CPU GGX 32 samples/mip；无 cubemap BSDF LUT、反射探针/SSR/SSGI、动态天空/cloud/backplate/distortion；SH9 静态，SkyReflection 只关 specular。M1.9 边界：自动曝光为最终 pass 内 4×4 center-weighted 瞬时测光，不是 HDRP GPU histogram/adaptation；M1.9 时无 Bloom/ColorGrading/自定义 LUT，M3.2 已补 ColorGrading LUT；ACES 为 HDRP approximate、无 HDR 输出。M1.10 边界：LightCluster 在 M2.3 升级为 CPU 64×64×32 cluster（`-2` 保留 16×16 FPTL legacy），不是 HDRP ray cluster；shadow atlas 为 debug-only clear + raw depth 对比度视图；Material debug 仅 opaque/depth prepass；Motion 无箭头网格/Intensity 子模式；DepthPyramid 固定 mip0。
- `M6.0 GPU light-list 基础设施 + screen AABB`：完成。`PipelineSettings::supportGpuLightList` M6.0 开发期默认 false（M6.4 已切 true）、`--gpu-light-list/--no-gpu-light-list`；新增 `light_screen_aabb.cs.sc`、RGBA32F `2*32×1` `lightScreenBounds`、`LightListBuilder::projectLightScreenBounds` CPU reference；compute pass 在 Camera Motion Vectors 后、GBuffer 前，stage 5 证 `lightData`、stage 1 写 image，`allowCulling(false)`；compute/program/texture 不可用时一次性告警并保持 CPU list。验证：**201 cases / 18,607 assertions**；D3D11/Vulkan GPU off/on **0 diff**，GPU 开启 pass 数 29→ 0、`compute=1`，关闭恢复 passes=29；首帧日志与 `render performance` 输出 `gpu_light_list`/`light_list_passes`。详见 [m6-checklist.md](m6-checklist.md)。
- `M6.1 GPU big-tile + FPTL`：完成。新增 `light_big_tile.cs.sc`、`light_fptl.cs.sc`，AABB → big-tile → FPTL 三个 compute pass；big-tile/FPTL 都用 `gl_WorkGroupID` 一个 group 对应一个 tile，FPTL 的 coarse tile 为 `tile >> 2`，输出保持 `count + 32 indices`、升序。AABB view-proj/view rows 改为 6×1 RGBA32F `lightViewRows` 上传（pitch=6×16），屏幕尺寸/light counts 通过 `ClusterUniforms` 镜像结 compute。验证：D3D11 640×360 半分辨率 1/8/32/full、1280×720、1920×1080、deferred、forward、cluster/no-cluster、contact shadows、no shadows 的 CPU/GPU 均 **0 diff**；Vulkan CPU/GPU **0 diff**；`passes=3`。M6.1 时 GPU 开关仍默认关闭（M6.4 已切 true）。详见 [m6-checklist.md](m6-checklist.md)。
- `M6.2 GPU cluster`：完成。新增 `light_cluster.cs.sc`，AABB → big-tile → FPTL → cluster 四个 compute pass；一个 workgroup 对应一个 `(64×64 tile, 32 slice)`，从 big-tile list + `s_lightData`/`s_lightViewRows` 计算 CPU 等价深度范围，输出现有 `clusterLightList` 的 `count + 32 indices`。`m_clusterLightListTexture` 开启 `computeWrite`；GPU 开启时不再调用 CPU `LightListBuilder::build()`，失败时完整 CPU 回退。验证：debug slice `-1/0/5/15/31` D3D11/Vulkan CPU/GPU 0 diff；transparent 1280/1080、Vulkan deferred/fog/no-cluster、D3D11 contact/fog/no-cluster、 1080p FPTL opaque 全部 **0 diff**；`passes=4`。关键修正：compute 的 `u_cluster_params.w`/`u_pass_lightCounts`/`u_pass_lightScreenSize` 读取不可靠，slice 数固定 32，counts/screen 镜像分 `ClusterUniforms`；`lightViewRows` pitch 修正为 6×16。详见 [m6-checklist.md](m6-checklist.md)。
- `M6.3 集成/回退/debug`：完成。`updateLightList()` 拆为 `updateCpuLightList()` + `addGpuLightListPasses()`；GPU 开启时 CPU builder 逐帧 0，失败回退 CPU；DebugUI 增加 GPU light list toggle 与路径 pass/CPU build 状态；`FrameDiagnostics`/`render performance` 增加 requested/active/fallback、passes、`light_list_cpu_ms`；所有 list 纹理 `computeWrite=true`，一次性 warning。验证：deferred/transparent/contact/no-cluster/fog（opaque； 8/16 lights 同后端 CPU/GPU **0 diff**；debug `-2` D3D/VK 0 diff； .5 render scale 同后端 0 diff；1080p RelWithDebInfo 300 帧 forward 8/16/32、cluster on/off、deferred、transparent、fog 均完成，GPU `light_list_cpu_ms=0`。已知 `transparent + fog` 组合触发既有 Motion Vectors transient graph error，留 M6.4 边界。详见 [m6-checklist.md](m6-checklist.md)。
- `M6.4 M6 验收`：完成。修复 transparent-only + fog 的 Motion Vectors producer 缺失：`Camera Motion Vectors` 在无 depth pyramid 时改用 scene depth（HDRP `Prepass.cs:1651-1675` 直接使用 depthBuffer）；transparent+fog D3D/VK CPU/GPU **0 diff**，D3D/VK **84.91 dB**。`supportGpuLightList`/`config/pipeline.json` 默认分 **true**，`--no-gpu-light-list` 保留 CPU 回退；初始化时提前创建 big-tile 纹理，失败自动 fallback。最终矩阵：forward/deferred/transparent/fog/contact/8/16/32/no-cluster/0.5 scale/spot 同后端 CPU/GPU 全 **0 diff**，D3D/VK GPU on **84.91–93.52 dB**（spot 99.00）；M5.4 base GPU off D3D/VK **0 diff**。1080p 300 帧 full post； 2 lights + Uffizi，TAA/Bloom/DOF/MB）D3D CPU **99.6–114.7** / GPU **112.3–122.2** fps，Vulkan CPU **90.9– 4.0** / GPU **91.1– 9.2** fps，均 >60；默认 GPU 复测 D3D **121.9** / Vulkan **99.8** fps，`light_list_cpu_ms=0`。base 32 接近持平/略慢已记录，不以 GPU 路径声称所有 workload 更快。详见 [m6-checklist.md](m6-checklist.md)。
- `M7 实现`：M7.0–M7.8 完成。M7.0 buffer/indirect 基础设施、M7.1 buffer producers、M7.2 buffer consumers、M7.3 indirect dispatch、M7.4/M7.5 GPU shadow culling/indirect draw、M7.6 async 可行性验证、M7.7 集成/性能、M7.8 验收均完成。AABB/big-tile/FPTL/cluster 全走 `Buffer<uint>`，`light_list_cpu_ms=0`。D3D11/Vulkan 与 cluster debug slice `-2/0/5/15/31`、`m61_full` deferred/forward、`m61_transparent`、fog、contact、spot、32-light stress、0.5 scale、no-cluster 为 CPU **全部 0 diff**；GPU shadow culling+indirect vs CPU shadow **0 diff**；alpha/cache/cascade（cascade 1/2/4、cache 6 requests）在 forward/deferred、双后端 **0 diff**。默认决策：buffer path 默认 true，indirect/shadow GPU/async 保持 false/experimental、 1080p 300 帧 full post：D3D11 CPU/texture/buffer/indirect+shadow **109.5/121.4/121.8/119.8** fps，Vulkan **107.7/114.8/117.7/115.1** fps。`ctest` 203 cases/19,713 assertions + idl check 通过。关键修复：shaderc include 文件 BOM 导致伪 `#endif without #if`；debug light-cluster 重复 upload 导致 bgfx uniform fatal。
- `M8 实现`：M8.0/M8.1/M8.2/M8.3/M8.4/M8.5/M8.6/M8.7 完成。M8.0 落 `PipelineSettings::lightListCapacity`（32/64/128，非法值按档位回退）、`--light-capacity`、JSON、DebugUI requested/effective、`lightListParams2` IDL，以及 `kMaxGpuLights=128`/`kLightListStrideMax=129`；M8.1 落 `LightBuilder`/`LightListBuilder` 的 32/64/128 CPU reference、运行时 `capacity+1` stride、cluster 32/cell 和 `truncatedLightCount`；M8.2 落 128-slot AABB、4×32-bit mask、runtime `lightListParams2` stride、texture/buffer producer 与 consumer stride，并移除 GPU 路径的 32 容量 gate；M8.3 落 cluster 第 33 个起的 overflow count 打包（`count | (overflow << 8)`）、debug view 品红显示和 33-light 同位置边界测试；M8.4 落 texture/CPU 路径按 backend `maxTextureSize` 和 16383 pitch 上限的 128→64→32 capacity clamp（buffer 路径保持 128）、requested/effective/clamped 诊断，以及 fog/contact/debug consumer 的 32 硬编码移除；M8.5 落 DebugUI/CLI 完整诊断与 GPU cluster overflow 数值 readback（buffer/texture），1440p/4K 与 CPU reference 一致；M8.6 落 1080p 300 帧性能矩阵并把默认容量切到 **64**（32 安全回退、128 opt-in）：32-light 场景 capacity 32/64/128 无显著差异，64-light D3D11 **60.8–64.9 fps** / Vulkan **75.1–79.7 fps**，128-light D3D11 **40.0–43.8 fps** / Vulkan **48.2–51.3 fps**，contact 在 128/128-light 额外约 4.6–5.2 ms；64-light GPU vs CPU 双后端 **0 diff**、32-light M7 基线 **0 diff**。`ctest` 209 cases/20,225 assertions + IDL check 通过；32-light M7 buffer/texture/CPU **0 diff**；64/128 buffer/texture、cluster/no-cluster、forward/deferred/transparent/fog/contact、D3D11/Vulkan GPU vs CPU **0 diff**；1440p/4K texture clamp 后 GPU/CPU **0 diff**。M8 不实现 layered offset、16-bit packing、category/layer/feature variants、cookies；M8.6 已决定 64 为默认，128 保持 experimental。M8.7 最终验收：M7 32-light 基线、64/128 forward/deferred/fog/contact/debug/transparent/no-cluster、buffer/texture/CPU、D3D11/Vulkan 共 126 组 0 diff；33 同位置灯/129 候选截断/128 cluster overflow 与 CPU 读回一致；1080p texture/128 不 clamp，1440p/4K clamp 64 后 0 diff，buffer/128 保持 128；resize 资源重建无 validation error；hbg_tests 209/20,225 + ctest/IDL/gfx-test 通过。64-light forward D3D11 58.9–66.5 fps（median 64.1），Vulkan 69.1–76.3 fps；deferred D3D11 median 61.7、余量较小，32 回退保留。
- M9 实现（规划见 `docs/m9-checklist.md`）：M9.0–M9.6 核心完成。已落 16-bit packed FPTL/big-tile、确定性 offset layered cluster、category/feature 数据契约、forward uniform + deferred GBuffer alpha 通道 light layers、light-data texel 打包的 spot/point cookie 与 spot IES；M9.6 以 opt-in 实现 128 shadow requests/4096 atlas、CPU size-sorted shelf packing，默认仍 64/2048。M9.7 的 cap32/64/128 × 1080p/1440p/4K × 双后端三路径矩阵与透明 light layers 已通过；1080p 300 full post 实测 cap64 55.2 fps、cap128 29.2 fps，未达 cap64 >60 fps 门禁，仅剩性能项。RenderDoc 安装后补 F11/GPU 抓帧。
- M10 实现（规划见 `docs/m10-checklist.md`）：M10.0–M10.8 已完成。light-data 为 83 texel；64×64 GGX LTC 表追加在 light-data texture 第 128 行之后，通过现有 sampler 读取；Rectangle area 使用 LTC GGX + PolygonIrradiance（Lambert diffuse），Tube 使用 HDRP line LTC（`I_diffuse_line` + `ComputeLineWidthFactor`，height=0、无 area cookie），Disc 明确 unsupported；GPU category producer 已输出 Punctual/Area 双层；tile feature flags 嵌入 FPTL/big-tile/cluster list 并驱动 runtime category gating，`--cluster-stats-readback` 可逐 tile 校验 GPU flags 与 CPU reference；area/directional cookie、cookieScaleOffset/repeat、point IES 与 spot IES cutoff 已接通，sidecar 支持 `type:"area"`。M10.7 新增 rectangle-only 独立 area shadow atlas：默认 1024 D24S8、512 分辨率、spot-like cone、manual-depth PCSS；stage 13 在 area active 时复用为 area depth，point PCSS 回退 tent。M10.8 完成 area rect 1080/1440/4K × 双后端 × forward/deferred × buffer/texture/CPU 36 组 0 diff、4K/cap128 不 clamp、cookie/IES 双后端、M8.7/M9 旧场景 0 diff、1/4/16 area 性能记录与 GPU flags readback；跨后端 deferred 少量边缘精度差异、M9.7 性能与官方 O3 shader_build 挂起作为已知限制。 M10 收口后补 area Rectangle/Tube 屏空间 OBB（HDRP `HDGpuLightsBuilder.Jobs.cs`），CPU `LightListBuilder` 与 GPU `light_screen_aabb*` 同步，Point/Spot/Disc 保持 sphere fallback；area 1080 双后端 forward/deferred 0 diff，area16 cluster flags/offset 0 mismatch。
- M11 进行中（`docs/m11-checklist.md`）：M11.0 shader build 工具 v1 已实现（超时/进度/失败报告/manifest 校验/stale 清理，510 variants 增量缓存验证），M11.2 cached area shadow v1 + mixed 已实现（all-static 独立 cached area atlas；mixed 使用 dynamic atlas static-first persistent slots，静态请求 cache hit 不重绘；D3D11/Vulkan forward/deferred all-static 与 mixed 各 4/4 0 diff）；M11.1 sampler gate 已通过（stage 8 context 改为 40-uint/request storage buffer，释放 sampler；D3D11/Vulkan cached/every-frame 4/4 0 diff）；M11.1b cookie atlas v1 已实现（19 texels/light、64×128 atlas、8×8 tile/128 capacity、texel7 tile index；area/spot forward/deferred 双后端 buffer/texture/CPU 三路径 0 diff），M11.1c 独立 cookie atlas 已完成：1024/512/2048 RGBA8 `s_cookieAtlas`（stage 15，2 px padding、mip、HDRP scaleOffset），light data 20 texels/64×192，probe array stage14 释放 sampler；native vs legacy 8/8 组合有差异（forward 136,925/max82、deferred 136,924/max82），D3D/VK 1 px/max1，texture/buffer 0 diff，`hbg_tests` 231/29,299；按 HDRP `LightCookieManager` 的 `NeedsUpdate` 语义增加 atlas content-hash 上传缓存，并按 `PowerOfTwoTextureAtlas.RelayoutEntries` 做 largest-first 布局，限制 native 源 256、非二叉树 shelf、atlas 满白 fallback、无 streaming。M11.3 variant strategy 已完成评估：否决 4 类编译期 category variants / 29 Lit variants，保留 runtime tile feature flags；新增 `--no-light-list-feature-flags` 评估开关，area16/spot/point100 × D3D11/Vulkan × forward/deferred 及 texture/CPU gating on/off 全部 0 diff，1080p 300 帧 full post D3D11 gating 约省 1.27 ms。M11.4 1080p 60fps 已完成：按 HDRP `HDShadowContext.hlsl` 的 StructuredBuffer 语义把 shadow context 改为 `float4` vector load，D3D11 forward 63.7 fps、deferred 62.3 fps，Vulkan 64.6/63.2 fps；cap128 37.5/40.1 fps 保持 opt-in；Vulkan 新旧 shader 0 diff，D3D11 为 shadow 边缘 1–2 ULP（PSNR 56.86 dB），M11.5 已接受。M11.5 核心矩阵：M10 area 36/36 0、area shadow 4/4 0、M9/transparent 路径 16/16 0、M11.1c native cookie 8/8 有差异（D3D/VK 1 px/max1、path 0）、cached 4/4 0、mixed 4/4 0、variant 8/8 0；新增 D3D area-only typed context 修复 area shadow，M9 64 D3D 为 127,795 px/max11 精度例外。M8.7 base32 已复跑：Vulkan 0，D3D typed 19,074 px/max165（structured 诊断 0，但 GPU 约 2×，不默认）；M11.0 全量 clean O3/fixtures/perf baseline 待做。M11 明确不直接复制 HDRP 29 个 Lit variants（当前项目缺少 SSS/transmission/anisotropy/clearcoat/iridescence 材质），也不做 HDRP 本身没有的 GPU shadow atlas packing。
- 验证命令：`cmake --preset vs2022` → `cmake --build --preset vs2022-debug` → `ctest --test-dir build/vs2022 -C Debug --output-on-failure`；冒烟 `hbg --render-test=<scene.gltf> --frames=3 --screenshot=out.png`；设置 `--pipeline-settings=config/pipeline.json --frame-settings=config/frame_settings.json`；阴影 `--shadow-profile=config/shadows.json --shadow-filter=pcf5x5 --shadow-distance=200 --shadow-cascades=4 --shadow-atlas-size=2048`；RT scale `--render-scale=0.5`；UI `hbg --scene=<scene.gltf> --frames=3 --screenshot=ui.png`；调试 `--debug-light-data`/`--debug-wireframe --no-shadows`；prepass/debug `--debug-view=depth|depth-pyramid|normal|motion-vectors|light-cluster|directional-shadow-atlas|punctual-shadow-atlas|shadow-cascade|shadow-filter|contact-shadows|albedo|roughness|ssao|ssr|ssgi|decals`；Sky/IBL `hbg --render-test=<scene.gltf> --sky=<hdri.hdr|exr> --sky-intensity=1 --sky-rotation=0 --frames=3 --screenshot=sky.png`；体积雾 `--volumetric-fog --fog-profile=config/volumetric_fog.json`（可配 `--fog-mean-free-path/--fog-anisotropy/--fog-depth-extent/--fog-steps/--no-fog-shadows/--fog-scale/--no-fog-temporal/--no-fog-jitter/--no-fog-punctual-lights/--no-fog-punctual-shadows/--fog-max-punctual-lights/--fog-local-volumes/--no-fog-local-volumes`）；最终输出 `--exposure-mode=fixed|auto|histogram --exposure-compensation=<ev> --tonemap=aces|neutral|none --debug-exposure=<ev>`。

---

## 0. 范围锁定与前置结论

- 后端：D3D11 + Vulkan（先跑通这两个，D3D12 后置）。
- 资产：glTF 用 cgltf，贴图用 bimg，运行时调试面板用 ImGui（不算编辑器）。
> 逐任务验证与 HDRP `file:line`：M0 见 `docs/m0-checklist.md`，M1 见 `docs/m1-checklist.md`。

| 任务 | 产出 | 状态 |
|---|---|---|
| M0.1 工程骨架 | CMake + C++20 + 依赖 + 双后端窗口
| `[x]` |
| M0.2 gfx 封装 | Device/Encoder/Texture/Buffer/FrameBuffer/ResourceManager | `[x]` |
| M0.3 IDL/常量 | `idl_gen.py`、C++/shader 生成、反射校验
| `[x]` |
| M0.4 glTF | cgltf、材质/贴图/相机/灯光、MikkTSpace、sidecar | `[x]` |
| M0.5 Forward PBR | MaterialSystem、HDR、ACES、render-test | `[x]` |
| M0.6 光照/阴影 v0 | LightData、方向光阴影、PCF | `[x]` |
| M0.7 调试设施 | ImGui、Profiler、wireframe、截图
| `[x]` |
| M1.1 RenderGraph | Handle/PassBuilder/compile/execute/裁剪/池
| `[x]` |
| M1.2 FrameSettings | 128 位、PipelineSettings、JSON | `[x]` |
| M1.3 ViewState | 矩阵/抖动/历史/RT scale/全局 CB | `[x]` |
| M1.4 DrawList | queue/pass tag/frustum cull/sort/instancing | `[x]` |
| M1.5 Prepass | depth/normal/motion/depth pyramid | `[x]` |
| M1.6 光照 v1 | CPU LightList、R32_UINT、Forward LightLoop | `[x]` |
| M1.7 阴影 v1 | 4 级联、point/spot、双 atlas、ShadowContext、PCF | `[x]` |
| M1.8 Sky/IBL | HDRI 天空、SH9 环境光、GGX mip、天空反射
| `[x]` |
| M1.9 最终输出
| 固定/简单自动曝光、ACES/Neutral、sRGB、Debug 覆盖 | `[x]` |
| M1.10 Debug | 深度/法线/速度/Cluster/阴影图集/Albedo/Roughness | `[x]` |
| M2.0 Deferred 基础 | LitShaderMode/双路径、GBuffer 资源、MRT/stencil、CLI、帧顺序 | `[x]` |
| M2.1 GBuffer | 4 RT、Lit GBuffer pass、深度 prepass、stencil、清屏
| `[x]` |
| M2.2 Deferred Lighting | 全屏光照、GBuffer 解码、阴影/IBL、forward-only 物体 | `[x]` |
| M2.3 Cluster LightList | 64×64×32 深度簇、CPU 构建、透明/Forward 使用 | `[x]` |
| M2.4 SSAO | 半分辨率 GTAO-lite、空间去噪、全分辨率 bilateral upsample、ambient 应用 | `[x]` |
| M2.5 SSR | 全分辨率 trace、IBL fallback、stencil 合成、SSR debug | `[x]` |
| M2.6 SSGI | 半分辨率屏幕空间间接漫反射、fallback、合成
| `[x]` |
| M2.7 Decals | DBuffer 3RT、sidecar decal、GBuffer 合成 | `[x]` |
| M2.8 Reflection Probes | 静态探针、box projection、2 探针混合、天空 fallback | `[x]` |
| M2.9 验收/性能 | Debug、双后端、图验证、性能、文档回填
| `[x]` |
| M3.0 PostProcess 基础 | PostProcessChain、profile、history、compute/UAV、FinalPass | `[x]` |
| M3.1 高级 Exposure | Histogram/自动曝光、时间适应、Apply Exposure | `[x]` |
| M3.2 Uber/Color Grading | Uber Post、 4³ LUT、Vignette、Tonemap | `[x]` |
| M3.3 Bloom | Threshold、mip down/up、scatter、Uber 合成 | `[x]` |
| M3.4 TAA | Jitter、history、motion reprojection、neighborhood clamp | `[x]` |
| M3.5 DOF | Manual focus、CoC、half-res gather、combine、debug CoC | `[x]` |
| M3.6 Motion Blur | Camera/object MV、采样、depth weight | `[x]` |
| M3.7 FXAA/Final | FXAA、sRGB、AA 互斥 | `[x]` |
| M3.8 M3 验收 | Debug、双后端、性能、文档回填
| `[x]` |
| M4.0 Global Volumetric Fog v1 | Height fog raymarch、方向光阴影、设置 CLI/双后端
| `[x]` |
| M4.1 Fog quality/optimization | 低分辨率重组、temporal、点/聚光体积光
| `[x]` |
| M4.2 Local fog volumes | 局部雾体、falloff、mask | `[x]` |
| M4.3 M4 验收 | 双后端矩阵、性能、debug、文档
| `[x]` |
| M5.0 Shadow settings/atlas/culling | 可配置 cascade/filter/resolution、per-request cull、alpha-tested caster | `[x]` |
| M5.1 Soft shadows/bias/micro | PCF5×5/7×7、PCSS、normal/slope bias、cascade fade、micro shadow | `[x]` |
| M5.2 Contact shadows | 屏幕空间 ray march、半分辨率、packed mask、light loop 应用 | `[x]` |
| M5.3 Per-light controls/cache | dimmer/fade/tint/near plane/resolution/update mode、静态 punctual cache | `[x]` |
| M5.4 M5 验收 | 双后端矩阵、性能、debug、文档、边界
| `[x]` |
| M6.0 GPU light-list 基础设施/screen AABB | GPU 开关/回退、compute pass、screen-space 光源包围盒
| `[x]` |
| M6.1 GPU big-tile/FPTL | 64×64 coarse list + 16×16 FPTL，deferred/forward opaque | `[x]` |
| M6.2 GPU cluster | 64×64×32 cluster list，替换 CPU cluster | `[x]` |
| M6.3 集成/回退/debug | 所有消费者、CPU fallback、诊断、性能接线 | `[x]` |
| M6.4 M6 验收 | D3D/VK、CPU/GPU 对照、性能、文档、边界
| `[x]` |
| M8.0–M8.7 M8 | 32/64/128 容量、动态 stride、cluster 32/cell、overflow 诊断、texture clamp、验收 | `[x]` M8.0–M8.7 完成 |

**M4.0 出口标准**：fog off 不改变 M3.8；fog on 双后端一致且有方向光阴影； 1080p full post >60fps；设置 CLI/单测/文档齐全。

**M4.1 出口标准**：low-res fog + depth-aware upsample；temporal no-jitter 静态不改变像素；point/spot 双后端一致；1080p full post >60fps；CLI/单测/文档齐全。

**M4.2 出口标准**：sidecar box/sphere 局部体积可加载；HDRP face/distance/exponential falloff 有单测；mask on/off 可见；D3D/VK 一致；M4.1 no-local 回归保持；close 后无资源泄漏。

**M4.3 出口标准**：M4.0–M4.2 双后端矩阵、性能、debug、文档和边界完成；fog off 与 M3.8 0 diff；M4.1 no-local 回归保持；full post+local 1080p >60fps；测试 IDL/gfx/docs 门禁通过。

**M5 出口标准**：shadow 设置/atlas/culling/alpha test 生效；HDRP 档位 PCF/PCSS 与 contact shadows 双后端一致；contact mask 不改变 ambient/透明；per-light dimmer/fade/tint 与静态 punctual cache 失效正确；shadow off 时 shadow/contact pass（M4.3 最终图带阴影，0-diff 参考不可用，见 M5.4）；full post 1080p >60fps；测试 IDL/gfx/docs 门禁通过。

**M6 计划出口标准**：GPU screen AABB/big-tile/FPTL/cluster 为 CPU reference 的 list 语义一致；GPU on/off、D3D/VK、forward/deferred/transparent/fog/contact 可对照；GPU 开启时不再调用 CPU `LightListBuilder::build()`；full post 1080p >60fps 且无回退；测试 IDL/gfx/docs 门禁通过。M6 v1 为 texture-backed，不含 GPU shadow culling/indirect draw。

**M8 计划出口标准**；32-light M7 基线 0 diff；64/128 的 CPU/texture/buffer list 语义一致；128 走 buffer 或按 texture 宽度 clamp；cluster 保持 32/cell 且有 overflow 诊断；64 双后端 1080p full post >60fps；128 有真实性能记录；resize/4K/回退/测试/IDL/gfx/docs 齐全。M8 不实现 layered offset、16-bit packing、category/layer/cookie。

**M1 出口标准**：glTF + HDRI + 级联阴影 + 聚类灯光 + 运动矢量 + Tonemap + RenderGraph + FrameSettings；1080p 60fps（GTX 1660/RX 5600 级）。

---

## VFX 运行时（V1）规划

> 状态：**V0 技术 spike 已通过（D3D11 + Vulkan，2026-11-20）；V1.0b.1（JSON/schema）、V1.0b.2（attribute layout/codegen）、V1.0b.3（表达式 DAG/类型/折叠）、V1.0b.4（Backend 接口/Mock/hdrp-bgfx 适配）、V1.0b.5（compiler/`vfxbake`/最小 kernel + unlit quad）、V1.0b.6（`.vfxb` reader/Runtime/Effect/unlit 执行）、V1.0b.7（scene/CLI/RenderGraph pass，D3D11+Vulkan 出图）与 V1.0b.8（CPU reference、GPU readback、D3D11/Vulkan golden、文档回填）已完成；V1.1.1（常量/单次/周期 Burst、dt clamp、spawner CPU reference）与 V1.1.2（Gravity/Drag/Force/Attractor/Turbulence、CPU/GPU curl reference、双后端 golden）已完成；下一步 V1.1.3**。独立 V 编号，避免占用 M13 及后续 HDRP 边界任务。
> 文档入口：`docs/vfx/README.md`。

### 决策

- 独立库 `vfx/`（C++20，无 bgfx/HDRP 依赖）+ hdrp-bgfx 适配层 `src/render/vfx/`。
- 资产：自有 `.vfx.json` → `vfxbake` 离线烘焙 `.vfxb`（dxbc + spirv），运行时不做 shader 编译。
- 驱动：引擎每帧手动 `Runtime::update(dt)` + `encodeSimulation` / `encodeOutput`。
- 输出：Unlit + Lit Quad；Lit 复用 hdrp-bgfx 的 `lighting/light_loop.sc` 与现有 light data/shadow/IBL。
- 平台：Windows，D3D11 + Vulkan；V1 只测性能，不设硬门槛。

### V1 范围

- 生成：常量速率、单次/周期 Burst。
- 运动：Gravity/Drag/Force/Attractor/Turbulence。
- 碰撞：解析平面、球；kill/bounce/stick。
- 外观：Color/Alpha/Size over Life、Curve/Gradient LUT、soft particles。
- 事件：CPU/GPU 事件、死亡生成/子发射器、prefix sum。
- 排序：KVP bitonic + merge；distance/custom。
- 图：子图展开、受限自定义 HLSL。
- 渲染：Unlit/Lit quad、Alpha/Additive、D3D11/Vulkan、forward/deferred。

### V1 不做

Mesh/Line/Ribbon/Point/Decal/SDF；Unity `.vfx` 导入；编辑器 UI；effect GPU instancing；自动 bounds readback；粒子投影阴影/motion vector；async compute；D3D12/Metal；多相机。

### 执行顺序

1. `docs/vfx/v0-checklist.md`：raw buffer、32B indirect、VS storage、unlit/lit 采样 spike；不通过不进入 V1。
2. `docs/vfx/v1-checklist.md`：V1.0b（最小垂直切片）→ V1.1（运动/碰撞/曲线）→ V1.2（事件/排序）→ V1.3（子图/自定义 HLSL）→ V1.4（Lit/发布）。
3. 每个任务回填实现位置、参考 `file:line`、偏差/限制；发现文档与源码冲突先改文档。

### 关键契约

- 64 threads/group；capacity 对齐 64：`Editor/NewCompiler/VfxDispatch.cs:11-21`。
- 属性布局/切片：`Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`、`ParticleSystemDataDescriptionWriter.cs:61-82`。
- 表达式：JSON 内联表达式 → `vfx::compileExpression` 类型/折叠/DAG 校验 → `VFX_*` 宏 HLSL；参考 `VFXExpressionGraph.cs:59-113`、`VFXExpressionMath.cs:9-604`；V1.0b.3 已接入 parser。
- 编译器：`vfx/include/vfx/compiler.h` → `vfx/src/compiler/codegen.cpp` 生成 spawner/deadListInit/init/update/outputUpdate/fillIndirect/VS/FS；`vfx/src/compiler/compiler.cpp` 调 shaderc；`binary.cpp` 写 `.vfxb`；`vfxbake` 支持 `--check`。当前最小 kernel 为 unlit quad，lit/sort/events/subgraph/custom HLSL 后续里程碑。
- quad：Unity 4 顶点 + 6 index（`VFXConfigPlanarPrimitive.template.hlsl:13-58`）；本项目 V1 非索引 **6 顶点/粒子**（V0 确认 4 顶点不能组成两个三角形），`numVertices=6*alive`；indirect draw 必须绑定非空 vertex stream（V0 dummy VB）。
- indirect 32B/条：`src/gfx/buffer.cpp:132-138`；`setStorageBuffer` offset 256B 对齐：`src/gfx/encoder.cpp:241-274`。
- 16 samplers / 16 compute bindings：`build/vs2022/_deps/bgfx-src/src/config.h:498-500`、`src/bgfx_p.h:211`；VFX lit 关闭 point/area sampler 后，texture 5/6/7/9/10/11/14/15 + buffer 12/13 可用。
- Lit 复用 `hbgEvaluateLightLoop`：`shaders/lighting/light_loop.sc:339`；SurfaceData：`shaders/material/surface_data_types.sc:6-37`。
- VFX pass 顺序：仿真在 prepass/sky 后、opaque 前；输出在 opaque 与 transparent 之间（forward）/deferred lighting 后（deferred）：`src/render/passes/forward_renderer.cpp:4670-4682,4731-4756,4828-4880`。

### 计划中的偏差（需在 `source-alignment.md` §5 维护）

非索引 6 顶点 quad、单 indirect draw、单 attribute group、简化 fillIndirect、每 effect 独立 buffer、手动 bounds、受限自定义 HLSL、VFX 输出在透明 pass 之前；V0 未接线方向光阴影。
