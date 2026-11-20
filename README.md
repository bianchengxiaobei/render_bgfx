# hdrp-bgfx

基于 **bgfx** 的 Windows 实时渲染器，用 C++20 复刻 Unity HDRP 的核心渲染概念和帧流程。项目以 glTF 场景为输入，当前重点验证 **D3D11 / Vulkan**、**Forward / Deferred**、GPU light list、阴影、后处理和体积雾。

> 这是一个学习/验证型渲染器，不是 Unity HDRP 的运行时替代品。HDRP 源码用作行为、数据布局和采样语义的参考。

## 功能

- **渲染路径**
  - Forward-only、Deferred-only、Forward + Deferred mixed
  - RenderGraph：transient texture/buffer、pass culling、view id、FBO 复用
  - HDRP 风格 FrameSettings / PipelineSettings JSON
  - Forward depth prepass、GBuffer、stencil 标记、Deferred Lighting
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
- **工具**
  - Debug UI、light cluster / shadow atlas / GBuffer / depth / motion vector 调试视图
  - `--screenshot`、`--render-test`、`--gfx-test`、`--asset-test`、`--cluster-stats-readback`
  - IDL 生成 uniform block，shader variant 构建脚本

## 当前状态

- M0–M8 已完成并验证。
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

首次配置时 CMake 会通过 `FetchContent` 拉取 bgfx、GLFW、cgltf、MikkTSpace、nlohmann/json、doctest。离线环境可设置 `HBG_BGFX_CMAKE_DIR`、`HBG_GLFW_DIR`、`HBG_CGLTF_DIR`、`HBG_MIKKTSPACE_DIR`、`HBG_NLOHMANN_JSON_DIR` 指向本地依赖。

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
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
- M12.2 shadow v2（进行中）：P0 带 shadows 跨后端分叉已修复——shadow.context 改为 typed StorageVertex/RGBA32F，atlas PCF texel 改为 atlasRect.xy * requestParams.zw（1/atlasSize），point face 加 6° guard；D3D/VK m26_ssgi_corner forward/deferred 0 diff。Area + point PCSS 共存：point depth 固定 stage 9，area stage 13，directional stage 12，三者可同时开启；m122_area_point_dir 全 PCSS D3D/VK 0 diff。Cached directional 已完成 per-cascade partial v1：按 request hash 只重绘变化 cascade，未变化 cascade 保留。EVSM v1 已接 `evsm` filter/mode 5：HDRP Chebyshev + exponential warp，现有 raw depth 上做 local 3x3 moment gather（不是 HDRP moment atlas）；m10_area_shadow deferred EVSM vs PCSS 60.63 dB、D3D/VK 100.24 dB，forward D3D/VK 103.01 dB。VSM v1 已接 `vsm` filter/mode 6：HDRP Chebyshev + 原始 depth moments，同样为 local 3x3 gather；area VSM vs PCSS 60.49 dB、vs EVSM 78.57 dB，D3D/VK 100.24/102.55 dB。MSM v1 已接 `msm` filter/mode 7：HDRP `SolveMSM` + Delta3/Delta4，4 个 raw-depth moments 做 local 3x3 gather；area MSM vs PCSS 60.48 dB、vs VSM 89.06 dB，D3D/VK 87.43/87.59 dB。IMS v1 已接 `ims` filter/mode 8：directional-only RGBA32F 4-moment atlas + per-slot SAT，3 个 compute pass，stage 12 在 IMS 下切 SAT；blocker search 用 integer SAT，最终 filter 用 bilinear SAT，并接入 HDRP light-angle/maxDepthBias/kernel 参数与 CLI；参数按 request 写入 shadow context texel9，cascade 不再共享首个 request。`m26_ssgi_corner` frames=1 IMS vs MSM 53.14 dB、vs PCF 45.75 dB，D3D/VK 67.08 dB；`m10_dir_shadow` no-contact 单 cascade IMS vs PCF 46.79 dB、4 cascade 28.69 dB（640），4 cascade D3D/VK 36.93 dB。HDRP 完整 static/dynamic mixed cached directional、transparent/SSS/cloud、fixed-precision 已完成（RGBA32U SAT）；advanced stage 仍待做。详见 [docs/m12.2-checklist.md](docs/m12.2-checklist.md)。
- M12.3 材质/GBuffer/Decal（进行中）：A03 v1 已完成——DBuffer 3RT→4RT RGBA8，albedo/normal/metallic/AO/roughness 独立 coverage，`channelMask` per-channel 与 `layerMask` receiver 过滤；GBuffer 增加 RGBA32F advanced 第 5 MRT。A02 deferred/forward/area advanced lighting v1 已完成；`ForwardAdvanced` 按 no-map/map、single/double-sided 选择变体；area rectangle/tube 增加 iridescence/clearcoat/transmission/SSS LTC 项。HDRP 29 Lit variants v1 已接：`--lit-variants` 使用 per-pixel stencil 分派；`--lit-variants-tile` 的 per-tile material flags + indirect compute 路径支持 SSAO 与 forward-only opaque（Vulkan stdscene 100.51 dB、advscene 94.49 dB、advall 100.51 dB/max1；D3D11 stdscene 100.51 dB/max1；feature-only 0 diff；D3D 58 raster + 29 compute dxbc 已完成）。D3D11 advanced 材质因 FXC no-opt compute 性能回退 raster；标准场景 1080p tile 107–125 fps、SSAO 139 fps。详见 [docs/m12.3-checklist.md](docs/m12.3-checklist.md)。
- GPU shadow atlas packing、async compute 仍是后续工作；默认 shadow 仍为 64 requests/2048 atlas。
- 方向光目前只支持 1 个主方向光。
- 项目尚未添加 `LICENSE` 文件；使用或分发前请先确认授权。
